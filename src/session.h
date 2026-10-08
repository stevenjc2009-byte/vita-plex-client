#pragma once
#include "settings.h"
#include "browse.h"
void session_download(const settings_t *settings,const browse_item_t *item,char *notice,unsigned cap);
int session_play(settings_t *settings,browse_item_t *item,char *notice,unsigned size);
