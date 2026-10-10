#include "audio_gain.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 int16_t source[]={-32768,-20000,-1,0,1,1000,20000,32767},copy[8];
 memcpy(copy,source,sizeof(copy));audio_gain(copy,8,100);assert(!memcmp(copy,source,sizeof(copy)));
 audio_gain(copy,8,200);assert(copy[0]==-32768 && copy[1]==-32768 && copy[2]==-2 && copy[3]==0 && copy[4]==2 && copy[5]==2000 && copy[6]==32767 && copy[7]==32767);
 int16_t tail[2048]={0};tail[0]=-1000;tail[1]=1000;audio_gain(tail,2048,300);assert(tail[0]==-3000 && tail[1]==3000);for(int i=2;i<2048;i++)assert(!tail[i]);
 memcpy(copy,source,sizeof(copy));audio_gain(copy,8,999);assert(!memcmp(copy,source,sizeof(copy)));
 puts("PCM boost preserves unity, saturates signed samples and keeps padded tails silent");return 0;
}
