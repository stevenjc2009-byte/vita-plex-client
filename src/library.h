#pragma once
#include "settings.h"
int library_scan(settings_t *settings,const char *section,char *body,unsigned body_size,char *notice,unsigned cap);

int library_scan_active(void);
int library_scan_poll(const settings_t *settings,char *body,unsigned size,char *notice,unsigned cap);
