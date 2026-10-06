#include "performance.h"
#include <stdio.h>
#ifdef __vita__
#include <psp2/power.h>
static int old_cpu,old_gpu,captured,fourth=-1,cpu_error,gpu_error;
SceUID performance_thread(const char *name,SceKernelThreadEntry entry,int priority,unsigned stack,int affinity) {
  SceUID id=sceKernelCreateThread(name,entry,priority,stack,0,affinity,NULL);
  if(affinity==0x80000)fourth=id>=0;
  if(id<0 && affinity)id=sceKernelCreateThread(name,entry,priority,stack,0,0,NULL);
  return id;
}
void performance_apply(int mode) {
  if(!captured){old_cpu=scePowerGetArmClockFrequency();old_gpu=scePowerGetGpuClockFrequency();captured=1;}
  cpu_error=gpu_error=0;
  if(mode){cpu_error=scePowerSetArmClockFrequency(mode==2?500:444);gpu_error=scePowerSetGpuClockFrequency(222);
    if(cpu_error<0 && mode==2)cpu_error=scePowerSetArmClockFrequency(444);}
  else {if(old_cpu>0)scePowerSetArmClockFrequency(old_cpu);if(old_gpu>0)scePowerSetGpuClockFrequency(old_gpu);}
}
void performance_restore(void){if(captured){if(old_cpu>0)scePowerSetArmClockFrequency(old_cpu);if(old_gpu>0)scePowerSetGpuClockFrequency(old_gpu);captured=0;}}
void performance_describe(char *out,unsigned cap){snprintf(out,cap,"CPU %d MHz | GPU %d MHz | Core 4: %s | Clock errors: %X / %X",scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency(),fourth<0?"not probed":fourth?"available":"fallback",cpu_error,gpu_error);}
int performance_fourth_core(void){return fourth;}
#else
void performance_apply(int m){(void)m;}void performance_restore(void){}
void performance_describe(char *o,unsigned n){snprintf(o,n,"Desktop preview; Vita clocks unavailable");}
int performance_fourth_core(void){return 0;}
#endif
