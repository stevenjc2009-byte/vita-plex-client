#pragma once
#include "settings.h"
#include "browse.h"
typedef struct {unsigned start,end;int credits;} media_marker_t;
int media_direct_part(const char *xml,const settings_t *settings,char *key,unsigned cap);
int media_markers(const char *xml,unsigned duration,media_marker_t *out,unsigned cap);
int media_pass(const char *xml);
int media_managed(const char *xml);
