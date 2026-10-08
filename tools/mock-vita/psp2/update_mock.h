#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#define SCE_SYSMODULE_INTERNAL_PAF 8
#define SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL 24
typedef int SceUID;
typedef struct {int flags;int *result;int unused[2];} SceSysmoduleOpt;
typedef struct {unsigned st_mode;uint64_t st_size;} SceIoStat;
typedef struct {char d_name[256];} SceIoDirent;
int sceIoDopen(const char*);int sceIoDread(int,SceIoDirent*);int sceIoDclose(int);int sceIoRmdir(const char*);int sceIoRemove(const char*);int sceIoMkdir(const char*,int);int sceIoGetstat(const char*,SceIoStat*);
int sceSysmoduleIsLoadedInternal(int);int sceSysmoduleLoadModuleInternalWithArg(int,unsigned,void*,const SceSysmoduleOpt*);int sceSysmoduleUnloadModuleInternalWithArg(int,unsigned,void*,const SceSysmoduleOpt*);int sceSysmoduleLoadModuleInternal(int);int sceSysmoduleUnloadModuleInternal(int);
int scePromoterUtilityInit(void);int scePromoterUtilityExit(void);int scePromoterUtilityPromotePkgWithRif(const char*,int);void sceKernelExitProcess(int);
FILE *update_test_fopen(const char*,const char*);
