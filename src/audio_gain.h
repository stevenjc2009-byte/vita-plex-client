#pragma once
#include <stdint.h>
#include <stddef.h>
// Signed PCM gain with saturation prevents wraparound at either rail.
static inline void audio_gain(int16_t *samples,size_t count,int percent){
 if(percent<100 || percent>300)percent=100;
 if(percent==100)return;
 for(size_t i=0;i<count;i++){
  int value=(int)samples[i]*percent/100;
  samples[i]=(int16_t)(value>32767?32767:value<-32768?-32768:value);
 }
}
