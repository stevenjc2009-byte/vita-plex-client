#pragma once
#include <stdint.h>
// 1 found, 0 wait for a later playlist, 2 clean end, negative unsupported/invalid.
int hls_segment(const char *body,uint64_t wanted,char *uri,unsigned cap,uint64_t *sequence);
// Resolve against the same server only; inherit the encoded Plex token if needed.
int hls_resolve(const char *base,const char *uri,char *out,unsigned cap);

int hls_variant(const char *body,char *uri,unsigned cap);
