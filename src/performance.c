#include "performance.h"
#include "video.h"
#include <stdio.h>
#ifdef __vita__
#include <psp2/power.h>
static int old_cpu,old_gpu,captured,fourth=-1,cpu_error,gpu_error,mode_now;static SceUID power_callback=-1;static int power_error,resumed,reapply;
static int power_event(int id,int count,int flags,void *arg){(void)id;(void)count;(void)arg;if(flags&(SCE_POWER_CB_APP_RESUME|SCE_POWER_CB_SYSTEM_RESUME)){__atomic_store_n(&resumed,1,__ATOMIC_RELEASE);__atomic_store_n(&reapply,1,__ATOMIC_RELEASE);}return 0;}
void performance_poll(void){sceKernelCheckCallback();if(__atomic_exchange_n(&reapply,0,__ATOMIC_ACQ_REL) && mode_now)performance_apply(mode_now);}
int performance_take_resume(void){return __atomic_exchange_n(&resumed,0,__ATOMIC_ACQ_REL);}
SceUID performance_thread(const char *name,SceKernelThreadEntry entry,int priority,unsigned stack,int affinity) {
  int used_affinity=affinity;SceUID id=sceKernelCreateThread(name,entry,priority,stack,0,affinity,NULL);
  if(id<0 && affinity){used_affinity=0;id=sceKernelCreateThread(name,entry,priority,stack,0,0,NULL);}
  if(id<0 && priority!=0x10000100){used_affinity=affinity;id=sceKernelCreateThread(name,entry,0x10000100,stack,0,affinity,NULL);if(id<0 && affinity){used_affinity=0;id=sceKernelCreateThread(name,entry,0x10000100,stack,0,0,NULL);}}
  if(affinity==0x80000)fourth=id>=0 && used_affinity==affinity;
  return id;
}
void performance_apply(int mode) {
  if(!captured){old_cpu=scePowerGetArmClockFrequency();old_gpu=scePowerGetGpuClockFrequency();captured=1;power_callback=sceKernelCreateCallback("plex_power",0,power_event,NULL);power_error=power_callback<0?power_callback:scePowerRegisterCallback(power_callback);if(power_error<0 && power_callback>=0){sceKernelDeleteCallback(power_callback);power_callback=-1;}}
  mode_now=mode;
  cpu_error=gpu_error=0;
  if(mode){cpu_error=scePowerSetArmClockFrequency(mode==2?500:444);gpu_error=scePowerSetGpuClockFrequency(222);
    if(cpu_error<0 && mode==2)cpu_error=scePowerSetArmClockFrequency(444);}
  else {if(old_cpu>0)scePowerSetArmClockFrequency(old_cpu);if(old_gpu>0)scePowerSetGpuClockFrequency(old_gpu);}
}
void performance_restore(void){if(power_callback>=0){scePowerUnregisterCallback(power_callback);sceKernelDeleteCallback(power_callback);power_callback=-1;}if(captured){if(old_cpu>0)scePowerSetArmClockFrequency(old_cpu);if(old_gpu>0)scePowerSetGpuClockFrequency(old_gpu);captured=0;}}
void performance_describe(char *out,unsigned cap){snprintf(out,cap,"CPU %d MHz | GPU %d MHz | Cores used: %X | Core 4: %s | Errors: %X / %X / %X",scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency(),video_observed_cores(),(video_observed_cores()&8)?"observed":fourth<0?"not probed":fourth?"requested":"fallback",cpu_error,gpu_error,power_error);}
int performance_fourth_core(void){return fourth;}
#else
void performance_poll(void){}int performance_take_resume(void){return 0;}
void performance_apply(int m){(void)m;}void performance_restore(void){}
void performance_describe(char *o,unsigned n){snprintf(o,n,"Desktop preview; Vita clocks unavailable");}
int performance_fourth_core(void){return 0;}
#endif
