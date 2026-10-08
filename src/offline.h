#pragma once
#include "settings.h"
#include "browse.h"
#include <stdint.h>
#define OFFLINE_SLOTS 8
typedef struct {uint32_t magic,signature,bytes,total,next,complete,kind,position;char server[256],client[40];browse_item_t item;uint32_t checksum;} offline_entry_t;
int offline_load(int slot,offline_entry_t *out);
int offline_owned(const offline_entry_t *entry,const settings_t *settings);
int offline_find(const settings_t *settings,const char *rating);
int offline_delete(int slot);
int offline_position(int slot,unsigned position);
uint64_t offline_usage(void);
void offline_path(int slot,int complete,char out[160]);
unsigned offline_signature(const char *metadata,const settings_t *settings);
int offline_download(const settings_t *settings,const browse_item_t *item,const char *source,int kind,unsigned signature);
int offline_menu(settings_t *settings,char *notice,unsigned cap);
