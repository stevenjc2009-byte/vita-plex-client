#pragma once
#include "mock.h"
#define SCE_SYSMODULE_NET 1
#define SCE_SYSMODULE_HTTP 2
#define SCE_SYSMODULE_SSL 3
#define SCE_SYSMODULE_HTTPS 4
#define SCE_HTTP_HEADER_ADD 0
#define SCE_HTTP_VERSION_1_1 1
#define SCE_HTTP_METHOD_GET 0
#define SCE_HTTP_METHOD_POST 1
#define SCE_HTTP_METHOD_PUT 2
#define SCE_O_TRUNC 8
typedef struct {unsigned size,flags;void *memory;} SceNetInitParam;
typedef struct {char *ptr;unsigned size;} SceHttpsData;
int sceKernelCreateMutex(const char*,unsigned,int,void*);
int sceKernelLockMutex(int,int,void*);int sceKernelUnlockMutex(int,int);int sceKernelDeleteMutex(int);
uint64_t sceKernelGetProcessTimeWide(void);
int sceSysmoduleUnloadModule(int);int sceNetInit(SceNetInitParam*);int sceNetTerm(void);
int sceNetCtlInit(void);int sceNetCtlTerm(void);int sceHttpInit(unsigned);int sceHttpTerm(void);int sceSslInit(unsigned);int sceSslTerm(void);
int sceHttpsLoadCert(int,const SceHttpsData**,void*,void*);
int sceHttpCreateTemplate(const char*,int,int);int sceHttpAddRequestHeader(int,const char*,const char*,int);
int sceHttpCreateConnectionWithURL(int,const char*,int);int sceHttpCreateRequestWithURL(int,int,const char*,unsigned);
int sceHttpSetResolveTimeOut(int,unsigned);int sceHttpSetConnectTimeOut(int,unsigned);int sceHttpSetSendTimeOut(int,unsigned);int sceHttpSetRecvTimeOut(int,unsigned);
int sceHttpSendRequest(int,const void*,unsigned);int sceHttpsGetSslError(int,int*,unsigned*);int sceHttpGetStatusCode(int,int*);int sceHttpReadData(int,void*,unsigned);
int sceHttpDeleteRequest(int);int sceHttpDeleteConnection(int);int sceHttpDeleteTemplate(int);int sceHttpAbortRequest(int);
int sceHttpSetAutoRedirect(int,int);int sceHttpGetResponseContentLength(int,unsigned long long*);
int sceIoClose(int);int sceIoRemove(const char*);
