#pragma once
#include "settings.h"
int progress_begin(const settings_t *settings,const char *rating,unsigned duration);
void progress_update(unsigned position,int state);
int progress_finish(unsigned position);
int progress_retry(const settings_t *settings);
int progress_pending(void);
