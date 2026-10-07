// SceHttp-based HTTP for Vita. No curl/ports needed.
// Init sequence mirrors the official vitasdk net_https sample
// (memblock net memory, sceNetCtlInit, sceSslInit) and server-cert
// verification stays ON for plex.tv.

#ifdef __vita__

#include "http.h"
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <psp2/libssl.h>
#include <psp2/sysmodule.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
static SceUID net_memid = -1;
static int inited = 0,net_ready,ctl_ready,http_ready,ssl_ready;
static unsigned modules;static int tls_failure;
static const char *init_stage="network initialization";
const char *http_init_stage(void){return init_stage;}
static SceUID request_lock=-1;
static int active_request=-1;
static int active_media_request=-1;
static volatile int cancelled;
static unsigned deadline_seconds=15;
void http_prepare(unsigned seconds){__atomic_store_n(&cancelled,0,__ATOMIC_RELEASE);deadline_seconds=seconds?seconds:15;}
void http_cancel(void){__atomic_store_n(&cancelled,1,__ATOMIC_RELEASE);
 if(request_lock>=0){sceKernelLockMutex(request_lock,1,NULL);if(active_request>=0)sceHttpAbortRequest(active_request);sceKernelUnlockMutex(request_lock,1);}}
static int expired(uint64_t start){return __atomic_load_n(&cancelled,__ATOMIC_ACQUIRE) || sceKernelGetProcessTimeWide()-start>(uint64_t)deadline_seconds*1000000;}
static void track(int request){if(request_lock>=0){sceKernelLockMutex(request_lock,1,NULL);active_request=request;sceKernelUnlockMutex(request_lock,1);}}
static void release_request(int request){if(request_lock>=0)sceKernelLockMutex(request_lock,1,NULL);if(active_request==request)active_request=-1;sceHttpDeleteRequest(request);if(request_lock>=0)sceKernelUnlockMutex(request_lock,1);}
void http_shutdown(void){
 if(ssl_ready){sceSslTerm();ssl_ready=0;}if(http_ready){sceHttpTerm();http_ready=0;}if(ctl_ready){sceNetCtlTerm();ctl_ready=0;}if(net_ready){sceNetTerm();net_ready=0;}
 if(net_memid>=0){sceKernelFreeMemBlock(net_memid);net_memid=-1;}
 if(request_lock>=0){sceKernelDeleteMutex(request_lock);request_lock=-1;}
 if(modules&8)sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTPS);if(modules&4)sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);if(modules&2)sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);if(modules&1)sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);modules=0;inited=0;
}
// Last HTTP status seen (or negative SceHttp error). Shown in the UI
// so failures are diagnosable instead of a bare "network error".
static int last_status = 0;
static int last_error = 0;
static int last_ssl_err = 0;
static unsigned last_ssl_detail = 0;

int http_last_status(void) { return last_status; }
int http_last_error(void) { return last_error; }
int http_last_ssl_err(void) { return last_ssl_err; }
unsigned http_last_ssl_detail(void) { return last_ssl_detail; }

static int load_module(int id,unsigned bit){if(sceSysmoduleIsLoaded(id)==0)return 0;int result=sceSysmoduleLoadModule(id);if(result>=0)modules|=bit;return result;}
int http_init(void) {
  if (inited) return 0;
  int r;
  init_stage="HTTP mutex";request_lock=sceKernelCreateMutex("plex_http_request",0,0,NULL);if(request_lock<0)return request_lock;
  init_stage="NET module";r = load_module(SCE_SYSMODULE_NET,1);
  if (r < 0) goto fail;
  init_stage="HTTP module";r = load_module(SCE_SYSMODULE_HTTP,2);
  if (r < 0) goto fail;

  // Net stack memory must come from its own memblock (not .bss).
  SceNetInitParam p;
  p.size = 2 * 1024 * 1024;
  p.flags = 0;
  init_stage="network memory";net_memid = sceKernelAllocMemBlock("SceNetMemory", 0x0C20D060,
    p.size, NULL);
  if (net_memid < 0){r=net_memid;goto fail;}
  if ((r=sceKernelGetMemBlockBase(net_memid, &p.memory)) < 0)goto fail;

  init_stage="network stack";r = sceNetInit(&p);
  if (r < 0) goto fail;net_ready=1;
  init_stage="Wi-Fi control";r = sceNetCtlInit();
  if (r < 0) goto fail;ctl_ready=1;
  init_stage="HTTP library";r = sceHttpInit(2 * 1024 * 1024);
  if (r < 0 && r!=(int)SCE_HTTP_ERROR_ALREADY_INITED)goto fail;http_ready=r>=0;
  init_stage="SSL module";r=load_module(SCE_SYSMODULE_SSL,4);if(r<0)goto tls_fail;
  init_stage="HTTPS module";r=load_module(SCE_SYSMODULE_HTTPS,8);if(r<0)goto tls_fail;
  init_stage="TLS library";r = sceSslInit(1 * 1024 * 1024);
  if (r < 0 && r!=(int)SCE_SSL_ERROR_ALREADY_INITED)goto tls_fail;ssl_ready=r>=0;

  // NOTE: no sceHttpsDisableOption — plex.tv keeps full cert/CN/CA
  // verification against the Vita CA store. LAN Plex traffic is
  // plain HTTP and never touches TLS.

  // plex.tv serves its leaf cert with no intermediate, which the Vita
  // cannot chain on its own: trust the DigiCert G2 pair explicitly.
  {
    extern const unsigned char plex_ca_root[];
    extern const unsigned int plex_ca_root_len;
    extern const unsigned char plex_ca_int[];
    extern const unsigned int plex_ca_int_len;
    extern const unsigned char plex_ca_isrg[];extern const unsigned int plex_ca_isrg_len;
    static SceHttpsData ca0, ca1,ca2;
    static const SceHttpsData *ca_list[3];
    ca0.ptr = (char *)plex_ca_root;
    ca0.size = plex_ca_root_len;
    ca1.ptr = (char *)plex_ca_int;
    ca1.size = plex_ca_int_len;
    ca_list[0] = &ca0;
    ca_list[1] = &ca1;ca2.ptr=(char*)plex_ca_isrg;ca2.size=plex_ca_isrg_len;ca_list[2]=&ca2;
    init_stage="TLS certificates";r=sceHttpsLoadCert(3, ca_list, NULL, NULL);if(r<0)goto tls_fail;
  }

  tls_failure=0;inited=1;return 0;
tls_fail:
  // A LAN HTTP server remains usable even if optional HTTPS setup fails.
  tls_failure=r;if(ssl_ready){sceSslTerm();ssl_ready=0;}
  if(modules&8){sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTPS);modules&=~8u;}if(modules&4){sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);modules&=~4u;}
  inited=1;return 0;
fail:
  http_shutdown();return r;
}

static void set_plex_headers(int tmpl, const char *client_id,
    const char *accept) {
  sceHttpAddRequestHeader(tmpl, "X-Plex-Product", "PlexVita",
    SCE_HTTP_HEADER_ADD);
  sceHttpAddRequestHeader(tmpl, "X-Plex-Version", "1.0",
    SCE_HTTP_HEADER_ADD);
  sceHttpAddRequestHeader(tmpl, "X-Plex-Platform", "PlayStation Vita",
    SCE_HTTP_HEADER_ADD);
  sceHttpAddRequestHeader(tmpl, "X-Plex-Device", "PS Vita",
    SCE_HTTP_HEADER_ADD);
  sceHttpAddRequestHeader(tmpl, "X-Plex-Client-Identifier", client_id,
    SCE_HTTP_HEADER_ADD);
  sceHttpAddRequestHeader(tmpl, "Accept", accept, SCE_HTTP_HEADER_ADD);
}

static int run(const char *url, const char *client_id, const char *accept,
    int method, char *body, unsigned body_len) {
  if (body && body_len) body[0] = 0;
  int code = -1, tmpl = -1, conn = -1, req = -1, r;
  unsigned used = 0;uint64_t started=sceKernelGetProcessTimeWide();
  last_status = 0;
  last_error = 0;
  last_ssl_err = 0;
  last_ssl_detail = 0;
  if(!strncmp(url,"https://",8) && tls_failure){last_error=tls_failure;return -1;}
  if (!body || !body_len) { last_error = -1; return -1; }

  tmpl = sceHttpCreateTemplate("PlexVita/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
  if (tmpl < 0) { last_error = tmpl; goto out; }
  set_plex_headers(tmpl, client_id, accept);
  conn = sceHttpCreateConnectionWithURL(tmpl, url, SCE_TRUE);
  if (conn < 0) { last_error = conn; goto out; }
  req = sceHttpCreateRequestWithURL(conn, method, url, 0);
  if (req < 0) { last_error = req; goto out; }
  track(req);if(expired(started)){last_error=-2;goto out;}
  sceHttpSetAutoRedirect(req,0);
  int secure=!strncmp(url,"https://",8);
  // Fail fast on dead networks: stock timeouts are 30s connect /
  // 120s send+recv, which looks like a hang with zero feedback.
  sceHttpSetResolveTimeOut(req, (secure?3:2) * 1000 * 1000);
  sceHttpSetConnectTimeOut(req, (secure?5:2) * 1000 * 1000);
  sceHttpSetSendTimeOut(req, 3 * 1000 * 1000);
  sceHttpSetRecvTimeOut(req, (secure?10:3) * 1000 * 1000);

  r = sceHttpSendRequest(req, NULL, 0);
  if (r < 0) {
    last_error = r;
    last_ssl_err = 0;
    last_ssl_detail = 0;
    sceHttpsGetSslError(req, &last_ssl_err, &last_ssl_detail);
    goto out;
  }

  int status = 0;
  r = sceHttpGetStatusCode(req, &status);
  if (r < 0) { last_error = r; goto out; }
  last_status = status;
  // plex.tv PIN creation answers 201 Created, not 200 — accept any 2xx.
  if (status < 200 || status >= 300) goto out;
  unsigned long long expected=0;int known_length=!sceHttpGetResponseContentLength(req,&expected);
  if(known_length && expected>=body_len){last_error=-9;goto out;}

  for (;;) {
    if(expired(started)){last_error=-2;goto out;}
    if (used == body_len - 1) {
      char extra;
      int n = sceHttpReadData(req, &extra, 1);
      if (n != 0) { last_error = n < 0 ? n : -1; goto out; }
      break;
    }
    int n = sceHttpReadData(req, body + used, body_len - used - 1);
    if (n < 0) { last_error = n; goto out; }
    if (n == 0) break;
    used += (unsigned)n;
    body[used] = 0;
  }
  if(known_length && expected!=used){last_error=-10;goto out;}
  code = 0;

out:
  if (code < 0 && body_len) body[0] = 0;
  if (req >= 0) release_request(req);
  if (conn >= 0) sceHttpDeleteConnection(conn);
  if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
  return code;
}

int http_post_pins(const char *url, const char *client_id,
    char *body, unsigned body_len) {
  return run(url, client_id, "application/json",
    SCE_HTTP_METHOD_POST, body, body_len);
}

int http_get(const char *url, const char *client_id, const char *accept,
    char *body, unsigned body_len) {
  return run(url, client_id, accept, SCE_HTTP_METHOD_GET, body, body_len);
}
int http_put(const char *url,const char *client,char *body,unsigned cap){return run(url,client,"text/xml",SCE_HTTP_METHOD_PUT,body,cap);}
static int download(const char *url, const char *path,
    void (*progress_cb)(unsigned received, unsigned total), volatile int *cancel,unsigned maximum) {
  int code = -1, tmpl = -1, conn = -1, req = -1, fd = -1, r;
  char chunk[8192];uint64_t started=sceKernelGetProcessTimeWide();
  unsigned received = 0, total = 0;
  unsigned long long len64 = 0;
  int have_length = 0;
  last_status = last_error = last_ssl_err = 0;
  last_ssl_detail = 0;

  if(!strncmp(url,"https://",8) && tls_failure){last_error=tls_failure;return -1;}
  tmpl = sceHttpCreateTemplate("PlexVita/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
  if (tmpl < 0) { last_error = tmpl; goto out; }
  conn = sceHttpCreateConnectionWithURL(tmpl, url, SCE_TRUE);
  if (conn < 0) { last_error = conn; goto out; }
  req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
  if (req < 0) { last_error = req; goto out; }
  track(req);
  // Release assets 302-redirect to object storage.
  sceHttpSetAutoRedirect(req, cancel?0:1);
  sceHttpSetResolveTimeOut(req, (cancel ? 2 : 10) * 1000 * 1000);
  sceHttpSetConnectTimeOut(req, (cancel ? 2 : 10) * 1000 * 1000);
  sceHttpSetSendTimeOut(req, (cancel ? 3 : 15) * 1000 * 1000);
  sceHttpSetRecvTimeOut(req, (cancel ? 3 : 20) * 1000 * 1000);

  r = sceHttpSendRequest(req, NULL, 0);
  if (r < 0) { last_error = r; goto out; }
  int status = 0;
  r = sceHttpGetStatusCode(req, &status);
  if (r < 0) { last_error = r; goto out; }
  last_status = status;
  if (status != 200) goto out;
  if (sceHttpGetResponseContentLength(req, &len64) == 0) {
    if (len64 > maximum) { last_error = -1; goto out; }
    total = (unsigned)len64;
    have_length = 1;
  }

  fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) { last_error = fd; goto out; }

  for (;;) {
    if ((cancel && __atomic_load_n(cancel,__ATOMIC_ACQUIRE)) || expired(started)) { last_error = -2; goto out; }
    r = sceHttpReadData(req, chunk, sizeof(chunk));
    if (r < 0) { last_error = r; goto out; }
    if (r == 0) break;
    if((unsigned)r>maximum-received){last_error=-3;goto out;}
    int wrote = sceIoWrite(fd, chunk, r);
    if (wrote != r) { last_error = wrote < 0 ? wrote : -1; goto out; }
    received += (unsigned)r;
    if (progress_cb) progress_cb(received, total);
  }
  if (have_length && received != total) { last_error = -1; goto out; }
  code = 0;

out:
  if (fd >= 0) {
    if(sceIoClose(fd)<0)code=-1;
    if (code < 0) sceIoRemove(path); // never cache a partial poster/VPK
  }
  if (req >= 0) release_request(req);
  if (conn >= 0) sceHttpDeleteConnection(conn);
  if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
  return code;
}
int http_download(const char *url, const char *path,
    void (*cb)(unsigned, unsigned)) { return download(url,path,cb,NULL,32*1024*1024); }
int http_download_art(const char *url,const char *path,volatile int *cancel) {
  return download(url,path,NULL,cancel,512*1024);
}
void http_media_abort(void){
 if(request_lock>=0){sceKernelLockMutex(request_lock,1,NULL);if(active_media_request>=0)sceHttpAbortRequest(active_media_request);sceKernelUnlockMutex(request_lock,1);}
}
int http_media_fetch(const char *url,void *data,unsigned cap,unsigned *used,volatile int *cancel){
 int tmpl=-1,conn=-1,req=-1,r=-1,status=0;uint64_t start=sceKernelGetProcessTimeWide();
 if(!url || !data || !cap || !used || !cancel)return -1;*used=0;
 if(__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return -2;
 if(!strncmp(url,"https://",8) && tls_failure)return tls_failure;
 tmpl=sceHttpCreateTemplate("PlexVita/01.37",SCE_HTTP_VERSION_1_1,SCE_TRUE);if(tmpl<0){r=tmpl;goto out;}
 conn=sceHttpCreateConnectionWithURL(tmpl,url,SCE_TRUE);if(conn<0){r=conn;goto out;}
 req=sceHttpCreateRequestWithURL(conn,SCE_HTTP_METHOD_GET,url,0);if(req<0){r=req;goto out;}
 sceHttpSetAutoRedirect(req,0); // Never forward token-bearing URLs to another server.
 sceHttpSetResolveTimeOut(req,3000000);sceHttpSetConnectTimeOut(req,3000000);sceHttpSetSendTimeOut(req,3000000);sceHttpSetRecvTimeOut(req,2000000);
 if(request_lock>=0)sceKernelLockMutex(request_lock,1,NULL);
 active_media_request=req;int stopped=__atomic_load_n(cancel,__ATOMIC_ACQUIRE);
 if(request_lock>=0)sceKernelUnlockMutex(request_lock,1);
 if(stopped){r=-2;goto out;}
 r=sceHttpSendRequest(req,NULL,0);if(r<0)goto out;
 r=sceHttpGetStatusCode(req,&status);if(r<0)goto out;if(status!=200){r=-status;goto out;}
 unsigned long long length=0;int known_length=!sceHttpGetResponseContentLength(req,&length);if(known_length && length>cap){r=-9;goto out;}
 for(;;){
  if(__atomic_load_n(cancel,__ATOMIC_ACQUIRE) || sceKernelGetProcessTimeWide()-start>30000000ULL){r=-2;goto out;}
  if(*used==cap){unsigned char extra;r=sceHttpReadData(req,&extra,1);if(r==0)break;if(r>0)r=-9;goto out;}
  r=sceHttpReadData(req,(unsigned char*)data+*used,cap-*used);if(r<0)goto out;if(!r)break;*used+=(unsigned)r;
 }
 r=known_length && length!=*used?-10:0;
out:
 if(request_lock>=0)sceKernelLockMutex(request_lock,1,NULL);
 if(active_media_request==req)active_media_request=-1;
 if(req>=0)sceHttpDeleteRequest(req);
 if(request_lock>=0)sceKernelUnlockMutex(request_lock,1);
 if(conn>=0)sceHttpDeleteConnection(conn);if(tmpl>=0)sceHttpDeleteTemplate(tmpl);
 if(r<0)*used=0;return r;
}

#else

int http_put(const char*u,const char*c,char*b,unsigned n){(void)u;(void)c;(void)b;(void)n;return -1;}
void http_prepare(unsigned s){(void)s;}void http_cancel(void){}void http_shutdown(void){}
void http_media_abort(void){}
int http_media_fetch(const char*u,void*d,unsigned c,unsigned*n,volatile int*x){(void)u;(void)d;(void)c;(void)x;if(n)*n=0;return -1;}
// Host stubs (self-test never performs network I/O).
const char *http_init_stage(void){return "desktop";}
int http_init(void) { return 0; }
int http_post_pins(const char *u, const char *c, char *b, unsigned l) {
  (void)u; (void)c; (void)b; (void)l;
  return -1;
}
int http_get(const char *u, const char *c, const char *a,
    char *b, unsigned l) {
  (void)u; (void)c; (void)a; (void)b; (void)l;
  return -1;
}
int http_download(const char *u, const char *p,
    void (*cb)(unsigned, unsigned)) {
  (void)u; (void)p; (void)cb;
  return -1;
}
int http_download_art(const char *u,const char *p,volatile int *cancel) {
  (void)u; (void)p; (void)cancel; return -1;
}
int http_last_status(void) { return 0; }
int http_last_error(void) { return 0; }
int http_last_ssl_err(void) { return 0; }
unsigned http_last_ssl_detail(void) { return 0; }

#endif
