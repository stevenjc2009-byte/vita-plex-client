#pragma once
// Framebuffer poster-grid browser (Vita). Owns its own 960x544 scanout
// buffer via sceDisplaySetFrameBuf - no GXM needed (proven by player.c).
// Host builds get a stub.
#include "browse.h"

// Blocking grid browser. Downloads missing thumbs over LAN HTTP into
// ux0:data/plex-client/art/. Returns selected index, -1 for back,
// -2 when the user pressed START (quit the app).
int gui_browse(const char *title, const browse_item_t *items, int n,
  const char *server, const char *token);
