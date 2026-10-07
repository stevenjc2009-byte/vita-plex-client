#pragma once
#include "settings.h"
#include "browse.h"
// 1 success, 0 unavailable, -1 cancelled, -2 exit. Output unchanged on failure.
int connection_probe(const settings_t *current,const plex_server_t *server,settings_t *connected);
