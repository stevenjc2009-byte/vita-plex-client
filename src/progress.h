#pragma once
#include "settings.h"
int progress_begin(const settings_t *settings,const char *rating,unsigned duration);
void progress_update(unsigned position,int state);
int progress_finish(unsigned position);
int progress_retry(const settings_t *settings);
int progress_pending(void);

int progress_pending_for(const settings_t *settings);
// others=1 discards only records outside this server/client; 0 discards all.
int progress_discard(const settings_t *settings,int others);
int progress_durable(void);

unsigned progress_position(const settings_t *s,const char *rating);

int progress_rebind(const settings_t *old,const settings_t *next);
int progress_record_local(const settings_t *settings,const char *rating,unsigned duration,unsigned position);
