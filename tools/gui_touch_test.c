#include <psp2/mock.h>
#include "gui.h"
#include "touch.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
static const unsigned char jpeg_fixture[]={255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,255,219,0,67,0,3,2,2,3,2,2,3,3,3,3,4,3,3,4,5,8,5,5,4,4,5,10,7,7,6,8,12,10,12,12,11,10,11,11,13,14,18,16,13,14,17,14,11,11,16,22,16,17,19,20,21,21,21,12,15,23,24,22,20,24,18,20,21,20,255,219,0,67,1,3,4,4,5,4,5,9,5,5,9,20,13,11,13,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,255,192,0,17,8,0,8,0,8,3,1,34,0,2,17,1,3,17,1,255,196,0,31,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,16,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,232,233,234,241,242,243,244,245,246,247,248,249,250,255,196,0,31,1,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,17,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,0,63,0,241,250,40,162,191,20,63,189,79,255,217};
static void *blocks[4];static int taps[8][2],tap_count,tap_at;
void touch_init(touch_state_t *s){memset(s,0,sizeof(*s));}int touch_poll(touch_state_t *s){assert(tap_at<tap_count);s->x=taps[tap_at][0];s->y=taps[tap_at++][1];return 1;}
static void tap(int x,int y){tap_count=1;tap_at=0;taps[0][0]=x;taps[0][1]=y;}
int sceKernelAllocMemBlock(const char*n,int t,unsigned size,SceKernelAllocMemBlockOpt*o){(void)n;(void)t;(void)o;for(int i=1;i<4;i++)if(!blocks[i]){blocks[i]=calloc(1,size);assert(blocks[i]);return i;}return -1;}
int sceKernelGetMemBlockBase(int id,void**out){*out=blocks[id];return 0;}int sceKernelFreeMemBlock(int id){free(blocks[id]);blocks[id]=NULL;return 0;}
int sceDisplaySetFrameBuf(SceDisplayFrameBuf*f,int m){(void)f;(void)m;return 0;}int sceDisplayWaitVblankStart(void){return 0;}void psvDebugScreenInit(void){}
int sceCtrlPeekBufferPositive(int p,SceCtrlData*d,int n){(void)p;(void)n;d->buttons=0;return 1;}int sceKernelDelayThread(unsigned n){(void)n;return 0;}
static SceKernelThreadEntry art_entry;
int sceKernelWaitThreadEnd(int t,void*a,void*b){(void)t;(void)a;(void)b;return 0;}int sceKernelDeleteThread(int t){(void)t;return 0;}int sceKernelStartThread(int t,unsigned n,void*p){(void)t;(void)n;(void)p;return art_entry(0,NULL);}
int performance_thread(const char*n,SceKernelThreadEntry f,int p,unsigned s,int a){(void)n;(void)p;(void)s;(void)a;art_entry=f;return 101;}void performance_poll(void){}
int sceIoMkdir(const char*p,int m){(void)p;(void)m;return 0;}void http_prepare(unsigned n){(void)n;}int http_download_art(const char*u,const char*p,volatile int*c){(void)u;(void)p;(void)c;return -1;}
static void cancel_wait(void);static volatile int *waiting;static void cancel_wait(void){*waiting=1;}
static void aspect_pixels(const char *file,int x,int y,int w,int h,int iw,int ih){
 FILE *f=fopen(file,"rb");int fw,fh,max;assert(f && fscanf(f,"P6 %d %d %d",&fw,&fh,&max)==3 && fw==960 && fh==544 && max==255);fgetc(f);
 unsigned char *pixels=malloc(960*544*3);assert(pixels && fread(pixels,1,960*544*3,f)==960*544*3);fclose(f);
 int dw=w,dh=w*ih/iw;if(dh>h){dh=h;dw=h*iw/ih;}
 for(int row=0;row<h;row++)for(int col=0;col<w;col++){
  unsigned char *p=pixels+((y+row)*960+x+col)*3;
  int inside=col>=(w-dw)/2 && col<(w-dw)/2+dw && row>=(h-dh)/2 && row<(h-dh)/2+dh;
  if(inside)assert(p[0]>200 && p[1]<50 && p[2]<50);else assert(p[0]==0x26 && p[1]==0x2b && p[2]==0x30);
 }free(pixels);
}
static void artwork_cases(void){
#ifdef _WIN32
 _mkdir("build/preview-art");
#else
 mkdir("build/preview-art",0700);
#endif
 const char *server="http://art-test",*thumb="/aspect-test";unsigned hash=2166136261u;
 for(const char*p=server;*p;p++)hash=(hash^(unsigned char)*p)*16777619u;
 for(const char*p=thumb;*p;p++)hash=(hash^(unsigned char)*p)*16777619u;
 char path[128];snprintf(path,sizeof(path),"build/preview-art/%08x.jpg",hash);
 browse_item_t item={0};strcpy(item.title,"Artwork proportions");strcpy(item.thumb,thumb);
 gui_view_t view={.title="Episodes",.subtitle="",.server=server,.token="mock-token",.items=&item,.n=1,.total=1};
 const int dims[3][2]={{16,8},{8,8},{8,16}};
 for(int k=0;k<3;k++){
  unsigned char jpeg[sizeof(jpeg_fixture)];memcpy(jpeg,jpeg_fixture,sizeof(jpeg));
  for(unsigned i=0;i+8<sizeof(jpeg);i++)if(jpeg[i]==255 && jpeg[i+1]==192){jpeg[i+6]=(unsigned char)dims[k][1];jpeg[i+8]=(unsigned char)dims[k][0];break;}
  FILE*f=fopen(path,"wb");assert(f && fwrite(jpeg,1,sizeof(jpeg),f)==sizeof(jpeg));fclose(f);
  tap(250,170);assert(gui_browse_view(&view)==0);assert(!gui_snapshot("build/art-grid-test.ppm"));aspect_pixels("build/art-grid-test.ppm",214,128,112,150,dims[k][0],dims[k][1]);
  tap(250,445);assert(gui_details(&item,server,"mock-token","")==0);assert(!gui_snapshot("build/art-details-test.ppm"));aspect_pixels("build/art-details-test.ppm",214,132,184,260,dims[k][0],dims[k][1]);
 }remove(path);remove("build/art-grid-test.ppm");remove("build/art-details-test.ppm");
}
int main(void){assert(!gui_init());gui_view_t view={.title="Libraries",.subtitle="",.items=NULL,.n=0,.libraries=1};tap(30,145);assert(gui_browse_view(&view)==GUI_VIEWS);tap(40,350);assert(gui_browse_view(&view)==GUI_SCAN);tap(40,410);assert(gui_browse_view(&view)==GUI_SETTINGS);
 browse_item_t library={0};snprintf(library.title,sizeof(library.title),"Movies");view.items=&library;view.n=view.total=1;tap(250,170);assert(gui_browse_view(&view)==0);
 const char *rows[]={"One","Two","Three"};tap(250,180);assert(gui_choice("Menu","",rows,3)==1);
 char text[32]="";tap_count=3;tap_at=0;taps[0][0]=800;taps[0][1]=510;taps[1][0]=230;taps[1][1]=215;taps[2][0]=420;taps[2][1]=510;assert(!gui_keyboard("Search",text,sizeof(text),0) && !strcmp(text,"\xc3\xa0"));
 assert(!gui_snapshot("build/keyboard-preview.ppm"));
 FILE *photo=fopen("build/gui-touch-photo.jpg","wb");assert(photo && fwrite(jpeg_fixture,1,sizeof(jpeg_fixture),photo)==sizeof(jpeg_fixture));fclose(photo);tap(40,515);assert(gui_photo("Photo fixture","build/gui-touch-photo.jpg")==GUI_BACK);assert(!gui_snapshot("build/photo-preview.ppm"));remove("build/gui-touch-photo.jpg");
 photo=fopen("build/gui-touch-photo.jpg","wb");assert(photo);fputs("broken JPEG",photo);fclose(photo);assert(gui_photo("Broken photo","build/gui-touch-photo.jpg")==-3);remove("build/gui-touch-photo.jpg");
 tap(250,445);assert(gui_details(&library,"http://test","token","")==0);volatile int done=0;waiting=&done;tap(500,300);assert(gui_wait(&done,cancel_wait)==GUI_BACK);
 artwork_cases();gui_shutdown();for(int i=1;i<4;i++)assert(!blocks[i]);puts("Actual GUI touch dispatch and landscape/square/portrait artwork proportions passed");return 0;}
