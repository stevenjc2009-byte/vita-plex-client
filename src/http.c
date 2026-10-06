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
static SceUID net_memid = -1;
static int inited = 0;
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

int http_init(void) {
  if (inited) return 0;
  int r;
  r = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  if (r < 0) return r;
  r = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
  if (r < 0) return r;
  r = sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
  if (r < 0) return r;
  r = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
  if (r < 0) return r;

  // Net stack memory must come from its own memblock (not .bss).
  SceNetInitParam p;
  p.size = 2 * 1024 * 1024;
  p.flags = 0;
  net_memid = sceKernelAllocMemBlock("SceNetMemory", 0x0C20D060,
    p.size, NULL);
  if (net_memid < 0) return net_memid;
  if (sceKernelGetMemBlockBase(net_memid, &p.memory) < 0) return -1;

  r = sceNetInit(&p);
  if (r < 0) return r;
  r = sceNetCtlInit();
  if (r < 0) return r;
  r = sceHttpInit(2 * 1024 * 1024);
  if (r < 0) return r;
  r = sceSslInit(1 * 1024 * 1024);
  if (r < 0) return r;

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
    static SceHttpsData ca0, ca1;
    static const SceHttpsData *ca_list[2];
    ca0.ptr = (char *)plex_ca_root;
    ca0.size = plex_ca_root_len;
    ca1.ptr = (char *)plex_ca_int;
    ca1.size = plex_ca_int_len;
    ca_list[0] = &ca0;
    ca_list[1] = &ca1;
    sceHttpsLoadCert(2, ca_list, NULL, NULL);
  }

  inited = 1;
  return 0;
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
  unsigned used = 0;
  last_status = 0;
  last_error = 0;
  last_ssl_err = 0;
  last_ssl_detail = 0;
  if (!body || !body_len) { last_error = -1; return -1; }

  tmpl = sceHttpCreateTemplate("PlexVita/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
  if (tmpl < 0) { last_error = tmpl; goto out; }
  set_plex_headers(tmpl, client_id, accept);
  conn = sceHttpCreateConnectionWithURL(tmpl, url, SCE_TRUE);
  if (conn < 0) { last_error = conn; goto out; }
  req = sceHttpCreateRequestWithURL(conn, method, url, 0);
  if (req < 0) { last_error = req; goto out; }
  // Fail fast on dead networks: stock timeouts are 30s connect /
  // 120s send+recv, which looks like a hang with zero feedback.
  sceHttpSetResolveTimeOut(req, 10 * 1000 * 1000);
  sceHttpSetConnectTimeOut(req, 10 * 1000 * 1000);
  sceHttpSetSendTimeOut(req, 15 * 1000 * 1000);
  sceHttpSetRecvTimeOut(req, 15 * 1000 * 1000);

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

  for (;;) {
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
  code = 0;

out:
  if (code < 0 && body_len) body[0] = 0;
  if (req >= 0) sceHttpDeleteRequest(req);
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
static int download(const char *url, const char *path,
    void (*progress_cb)(unsigned received, unsigned total), volatile int *cancel) {
  int code = -1, tmpl = -1, conn = -1, req = -1, fd = -1, r;
  char chunk[8192];
  unsigned received = 0, total = 0;
  unsigned long long len64 = 0;
  int have_length = 0;
  last_status = last_error = last_ssl_err = 0;
  last_ssl_detail = 0;

  tmpl = sceHttpCreateTemplate("PlexVita/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
  if (tmpl < 0) { last_error = tmpl; goto out; }
  conn = sceHttpCreateConnectionWithURL(tmpl, url, SCE_TRUE);
  if (conn < 0) { last_error = conn; goto out; }
  req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
  if (req < 0) { last_error = req; goto out; }
  // Release assets 302-redirect to object storage.
  sceHttpSetAutoRedirect(req, 1);
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
    if (len64 > 0xFFFFFFFFu) { last_error = -1; goto out; }
    total = (unsigned)len64;
    have_length = 1;
  }

  fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) { last_error = fd; goto out; }

  for (;;) {
    if (cancel && *cancel) { last_error = -1; goto out; }
    r = sceHttpReadData(req, chunk, sizeof(chunk));
    if (r < 0) { last_error = r; goto out; }
    if (r == 0) break;
    int wrote = sceIoWrite(fd, chunk, r);
    if (wrote != r) { last_error = wrote < 0 ? wrote : -1; goto out; }
    received += (unsigned)r;
    if (progress_cb) progress_cb(received, total);
  }
  if (have_length && received != total) { last_error = -1; goto out; }
  code = 0;

out:
  if (fd >= 0) {
    sceIoClose(fd);
    if (code < 0) sceIoRemove(path); // never cache a partial poster/VPK
  }
  if (req >= 0) sceHttpDeleteRequest(req);
  if (conn >= 0) sceHttpDeleteConnection(conn);
  if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
  return code;
}
int http_download(const char *url, const char *path,
    void (*cb)(unsigned, unsigned)) { return download(url,path,cb,NULL); }
int http_download_art(const char *url,const char *path,volatile int *cancel) {
  return download(url,path,NULL,cancel);
}

#else

// Host stubs (self-test never performs network I/O).
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
