#pragma once
#include <stddef.h>
#ifdef __vita__
#include <psp2/types.h>
#include <psp2/kernel/threadmgr.h>
SceUID performance_thread(const char *name,SceKernelThreadEntry entry,int priority,unsigned stack,int affinity);
#endif
void performance_apply(int mode);
void performance_restore(void);
void performance_describe(char *out,unsigned cap);
int performance_fourth_core(void);
