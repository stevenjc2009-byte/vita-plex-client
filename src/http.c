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

int http_last_status(void) { return last_status; }

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
  if (body_len) body[0] = 0;
  int code = -1, tmpl = -1, conn = -1, req = -1, r;
  unsigned used = 0;

  tmpl = sceHttpCreateTemplate("PlexVita/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
  if (tmpl < 0) goto out;
  set_plex_headers(tmpl, client_id, accept);
  conn = sceHttpCreateConnectionWithURL(tmpl, url, SCE_TRUE);
  if (conn < 0) goto out;
  req = sceHttpCreateRequestWithURL(conn, method, url, 0);
  if (req < 0) goto out;
  // Fail fast on dead networks: stock timeouts are 30s connect /
  // 120s send+recv, which looks like a hang with zero feedback.
  sceHttpSetResolveTimeOut(req, 10 * 1000 * 1000);
  sceHttpSetConnectTimeOut(req, 10 * 1000 * 1000);
  sceHttpSetSendTimeOut(req, 15 * 1000 * 1000);
  sceHttpSetRecvTimeOut(req, 15 * 1000 * 1000);

  r = sceHttpSendRequest(req, NULL, 0);
  if (r < 0) goto out;

  int status = 0;
  if (sceHttpGetStatusCode(req, &status) < 0) goto out;
  last_status = status;
  // plex.tv PIN creation answers 201 Created, not 200 — accept any 2xx.
  if (status < 200 || status >= 300) goto out;

  for (;;) {
    if (used + 1024 >= body_len) break;
    int n = sceHttpReadData(req, body + used, body_len - used - 1);
    if (n < 0) goto out;
    if (n == 0) break;
    used += (unsigned)n;
    body[used] = 0;
  }
  code = 0;

out:
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
int http_download(const char *url, const char *path,
    void (*progress_cb)(unsigned received, unsigned total)) {
  int code = -1, tmpl = -1, conn = -1, req = -1, fd = -1, r;
  static char chunk[8192];
  unsigned received = 0, total = 0;
  unsigned long long len64 = 0;

  tmpl = sceHttpCreateTemplate("PlexVita/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
  if (tmpl < 0) goto out;
  conn = sceHttpCreateConnectionWithURL(tmpl, url, SCE_TRUE);
  if (conn < 0) goto out;
  req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
  if (req < 0) goto out;
  // Release assets 302-redirect to object storage.
  sceHttpSetAutoRedirect(req, 1);
  sceHttpSetResolveTimeOut(req, 10 * 1000 * 1000);
  sceHttpSetConnectTimeOut(req, 10 * 1000 * 1000);
  sceHttpSetSendTimeOut(req, 15 * 1000 * 1000);
  sceHttpSetRecvTimeOut(req, 20 * 1000 * 1000);

  if (sceHttpSendRequest(req, NULL, 0) < 0) goto out;
  int status = 0;
  if (sceHttpGetStatusCode(req, &status) < 0) goto out;
  if (status != 200) goto out;
  if (sceHttpGetResponseContentLength(req, &len64) == 0)
    total = (unsigned)len64;

  fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) goto out;

  for (;;) {
    r = sceHttpReadData(req, chunk, sizeof(chunk));
    if (r < 0) goto out;
    if (r == 0) break;
    if (sceIoWrite(fd, chunk, r) != r) goto out;
    received += (unsigned)r;
    if (progress_cb) progress_cb(received, total);
  }
  code = 0;

out:
  if (fd >= 0) sceIoClose(fd);
  if (req >= 0) sceHttpDeleteRequest(req);
  if (conn >= 0) sceHttpDeleteConnection(conn);
  if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
  return code;
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
int http_last_status(void) { return 0; }

#endif
