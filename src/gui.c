#include "gui.h"
#include "text.h"
#include "touch.h"
#include "http.h"
#include "plex_auth.h"
#include "debugScreen.h"
#include "performance.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#endif
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define W 960
#define H 544
#define COLS 5
#define PAGE 10
#define PW 112
#define PH 150
#define BG 0xFF161413u
#define PANEL 0xFF211E1Bu
#define TILE 0xFF302B26u
#define GOLD 0xFF0DA0E5u
#define WHITE 0xFFF2F0EFu
#define GREY 0xFFA5A09Au
#define BLACK 0xFF090909u
#if defined(__vita__) && !defined(PLEX_MOCK_VITA)
#define FONT_DIR "app0:assets/"
#define ART_DIR "ux0:data/plex-client/art/"
#else
#define FONT_DIR "assets/"
#define ART_DIR "build/preview-art/"
#endif
typedef struct { unsigned char *data, *widths, *pixels; unsigned w,h,count;const unsigned char *codes; } font_t;
static font_t fonts[3];
static unsigned *buffers[2], *fb;
static int draw_buffer;
#ifdef __vita__
static SceUID fb_ids[2] = {-1,-1}, art_tid = -1;
#endif
typedef struct { int width,height; unsigned char pixels[PW*PH*3]; } artwork_t;
static artwork_t *art[PAGE];
static volatile int art_stop, art_version;
static gui_view_t art_view;
static int art_page;

static unsigned short le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static int load_font(font_t *f, int size) {
  char path[80]; snprintf(path,sizeof(path),FONT_DIR "font%d.bin",size);
  FILE *file=fopen(path,"rb"); if(!file) return -1;
  if(fseek(file,0,SEEK_END)) { fclose(file); return -1; }
  long len=ftell(file); rewind(file);
  if(len<236 || len>4*1024*1024) { fclose(file); return -1; }
  f->data=malloc((size_t)len);
  if(!f->data || fread(f->data,1,(size_t)len,file)!=(size_t)len) {
    free(f->data); f->data=NULL; fclose(file); return -1;
  }
  fclose(file);
  f->w=le16(f->data+4); f->h=le16(f->data+6);
  if(!f->w || !f->h || f->w>64 || f->h>64){free(f->data);f->data=NULL;return -1;}
  if(!memcmp(f->data,"PFNT",4) && le16(f->data+8)==32 && le16(f->data+10)==224){f->count=224;f->codes=NULL;f->widths=f->data+12;f->pixels=f->data+236;}
  else if(!memcmp(f->data,"PFN2",4)){f->count=le16(f->data+8);if(!f->count || f->count>2048 || len<12L+5L*(long)f->count){free(f->data);f->data=NULL;return -1;}f->codes=f->data+12;f->widths=f->data+12+f->count*4;f->pixels=f->widths+f->count;}
  else {free(f->data);f->data=NULL;return -1;}
  if(len!=(long)(f->pixels-f->data)+(long)f->count*(long)f->w*(long)f->h){free(f->data);f->data=NULL;return -1;}return 0;
}
int gui_init(void) {
  if(buffers[0]) return 0;
  for(int i=0;i<2;i++) {
#ifdef __vita__
    fb_ids[i]=sceKernelAllocMemBlock("plex_ui",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,2*1024*1024,NULL);
    if(fb_ids[i]<0 || sceKernelGetMemBlockBase(fb_ids[i],(void**)&buffers[i])<0) {
      gui_shutdown(); return -1;
    }
#else
    buffers[i]=calloc(W*H,sizeof(unsigned));
    if(!buffers[i]) { gui_shutdown(); return -1; }
#endif
  }
  fb=buffers[0];
  if(load_font(&fonts[0],16) || load_font(&fonts[1],20) || load_font(&fonts[2],28)) {
    gui_shutdown(); return -1;
  }
  return 0;
}
static void stop_art(void) {
  __atomic_store_n(&art_stop,1,__ATOMIC_RELEASE);
#ifdef __vita__
  if(art_tid>=0) { sceKernelWaitThreadEnd(art_tid,NULL,NULL); sceKernelDeleteThread(art_tid); art_tid=-1; }
#endif
  for(int i=0;i<PAGE;i++) { free(art[i]); art[i]=NULL; }
}
void gui_shutdown(void) {
  stop_art();
#ifdef __vita__
  psvDebugScreenInit(); sceDisplayWaitVblankStart();
#endif
  for(int i=0;i<2;i++) {
#ifdef __vita__
    if(fb_ids[i]>=0) sceKernelFreeMemBlock(fb_ids[i]);
    fb_ids[i]=-1;
#else
    free(buffers[i]);
#endif
    buffers[i]=NULL;
  }
  for(int i=0;i<3;i++) { free(fonts[i].data); memset(&fonts[i],0,sizeof(fonts[i])); }
  fb=NULL;
}
static void present(void) {
#ifdef __vita__
  SceDisplayFrameBuf frame={sizeof(frame),fb,W,SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,W,H};
  sceDisplaySetFrameBuf(&frame,SCE_DISPLAY_SETBUF_NEXTFRAME);
  sceDisplayWaitVblankStart();
#endif
  draw_buffer^=1; fb=buffers[draw_buffer];
}
static void rect(int x,int y,int w,int h,unsigned color) {
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  if(x+w>W)w=W-x; if(y+h>H)h=H-y;
  if(w<=0 || h<=0 || !fb)return;
  for(int r=0;r<h;r++)for(int c=0;c<w;c++)fb[(y+r)*W+x+c]=color;
}
static void border(int x,int y,int w,int h,unsigned color) {
  rect(x,y,w,2,color);rect(x,y+h-2,w,2,color);rect(x,y,2,h,color);rect(x+w-2,y,2,h,color);
}
static unsigned font_index(const font_t *f,unsigned cp){if(!f->codes)return cp>=32 && cp<256?cp-32:'?'-32;
 unsigned lo=0,hi=f->count;while(lo<hi){unsigned mid=(lo+hi)/2;const unsigned char *p=f->codes+mid*4;unsigned v=p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned)p[3]<<24);if(v<cp)lo=mid+1;else hi=mid;}
 if(lo<f->count){const unsigned char *p=f->codes+lo*4;unsigned v=p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned)p[3]<<24);if(v==cp)return lo;}return cp=='?'?0:font_index(f,'?');
}
static int width(const char *s,int size) {
  font_t *f=fonts+size; int n=0;
  if(!s || !f->data)return 0;
  while(*s)n+=f->widths[font_index(f,text_next(&s))]; return n;
}
static void glyph(unsigned cp,int x,int y,int size,unsigned col) {
  font_t *f=fonts+size; if(!f->data)return;
  const unsigned char *pixels=f->pixels+font_index(f,cp)*f->w*f->h;
  for(unsigned r=0;r<f->h;r++)for(unsigned c=0;c<f->w;c++) {
    int xx=x+(int)c,yy=y+(int)r; unsigned a=pixels[r*f->w+c];
    if(!a || xx<0 || yy<0 || xx>=W || yy>=H)continue;
    unsigned old=fb[yy*W+xx], result=0xFF000000u;
    for(int shift=0;shift<24;shift+=8)result|=((((old>>shift)&255)*(255-a)+((col>>shift)&255)*a)/255)<<shift;
    fb[yy*W+xx]=result;
  }
}
static void text(const char *s,int x,int y,int size,unsigned col,int max) {
  if(!s || !fonts[size].data)return;
  int start=x, dots=width("...",size), truncated=width(s,size)>max;
  while(*s) {
    const char *before=s; unsigned cp=text_next(&s); unsigned advance=fonts[size].widths[font_index(fonts+size,cp)];
    if(x-start+(int)advance>(truncated?max-dots:max)) { s=before;break; }
    glyph(cp,x,y,size,col);x+=(int)advance;
  }
  if(*s && max>=dots) { for(int i=0;i<3;i++){glyph('.',x,y,size,col);x+=fonts[size].widths[font_index(fonts+size,'.')];} }
}
static void wrap(const char *s,int x,int y,int max,int lines) {
  char line[256];
  while(s && *s && lines-->0) {
    int n=0, last=-1;
    while(s[n] && s[n]!='\n' && n<250) {
      const char *end=s+n;text_next(&end);int bytes=(int)(end-(s+n));if(n+bytes>250)break;
      memcpy(line+n,s+n,(size_t)bytes);line[n+bytes]=0;
      if(width(line,1)>max)break;
      if(s[n]==' ')last=n;n+=bytes;
    }
    if(s[n] && s[n]!='\n' && last>0)n=last;
    if(!n){const char *end=s;text_next(&end);n=(int)(end-s);}
    memcpy(line,s,(size_t)n);line[n]=0; text(line,x,y,1,GREY,max);
    s+=n;while(*s==' ' || *s=='\n')s++; y+=28;
  }
}
static const char *kind(const browse_item_t *it) {
  if(!strcmp(it->type,"show"))return "SERIES";
  if(!strcmp(it->type,"season"))return "SEASON";
  if(!strcmp(it->type,"episode"))return "EPISODE";
  if(!strcmp(it->type,"album"))return "ALBUM";
  if(!strcmp(it->type,"artist") || !strcmp(it->type,"track"))return "MUSIC";
  if(!strcmp(it->type,"photo"))return "PHOTOS";
  if(!strcmp(it->type,"movie"))return "MOVIES";
  return it->is_directory?"FOLDER":"VIDEO";
}
static void shell(const char *title,const char *subtitle,int nav) {
  rect(0,0,W,H,BG);rect(0,0,176,H,PANEL);
  text("PLEX",22,17,2,GOLD,140);text("for PlayStation Vita",22,58,0,GREY,144);
  static const char *links[]={"Home","Libraries","Search","Refresh list","Scan library","Settings"};
  for(int i=0;i<6;i++) {
    if(nav==i){rect(12,124+i*52,152,42,TILE);rect(12,124+i*52,3,42,GOLD);}
    text(links[i],30,132+i*52,1,nav==-2?0xFF69645Fu:nav==i?WHITE:GREY,132);
  }
  text(nav==-2?"O       Back":"SELECT  Settings",22,451,0,GREY,148);
  text("START   Exit",22,481,0,GREY,148);
  text(title,202,18,2,WHITE,724);
  text(subtitle,202,65,0,GREY,720);
  rect(200,100,724,1,TILE);
}
static void poster(const artwork_t *img,int x,int y,int w,int h) {
  if(!img)return;
  int dw=w,dh=(int)((int64_t)w*img->height/img->width);
  if(dh>h){dh=h;dw=(int)((int64_t)h*img->width/img->height);}
  if(dw<1)dw=1;if(dh<1)dh=1;
  x+=(w-dw)/2;y+=(h-dh)/2;w=dw;h=dh;
  for(int r=0;r<h;r++)for(int c=0;c<w;c++) {
    int sy=r*img->height/h,sx=c*img->width/w;
    const unsigned char *p=img->pixels+(sy*img->width+sx)*3;
    if(x+c>=0 && x+c<W && y+r>=0 && y+r<H)
      fb[(y+r)*W+x+c]=p[0]|(p[1]<<8)|(p[2]<<16)|0xFF000000u;
  }
}
static void draw_grid(const gui_view_t *v,int nav) {
  shell(v->title,v->subtitle,nav);
  int per=v->libraries?6:PAGE,cols=v->libraries?3:COLS;
  int page=v->cursor/per;
  if(!v->n){text("No items to show",220,191,2,WHITE,620);wrap(v->notice && *v->notice?v->notice:"Refresh this library or try a different search.",220,249,610,4);}
  for(int cell=0;cell<per;cell++) {
    int i=page*per+cell;if(i>=v->n)break;
    const browse_item_t *it=v->items+i;
    int selected=i==v->cursor && nav<0;
    if(v->libraries) {
      int x=202+(cell%cols)*244,y=126+(cell/cols)*166;
      rect(x,y,228,144,TILE);
      rect(x+16,y+18,42,38,PANEL);text(kind(it),x+70,y+25,0,GOLD,148);
      // Film-strip / library mark drawn directly in the UI.
      rect(x+25,y+27,23,19,GREY);rect(x+29,y+30,15,13,TILE);
      text(it->title,x+16,y+77,1,WHITE,194);
      text("Open library  >",x+16,y+111,0,GREY,192);
      if(selected)border(x-3,y-3,234,150,GOLD);
    } else {
      int x=214+(cell%cols)*144,y=128+(cell/cols)*184;
      rect(x,y,PW,PH,TILE);
      artwork_t *img=__atomic_load_n(art+cell,__ATOMIC_ACQUIRE);
      if(img)poster(img,x,y,PW,PH);
      else {text(kind(it),x+10,y+38,0,GOLD,PW-18);wrap(it->title,x+10,y+64,PW-20,2);}
      if(it->view_count){rect(x,y,PW,22,PANEL);text("WATCHED",x+8,y+1,0,GOLD,PW-12);}
      if(selected)border(x-3,y-3,PW+6,PH+6,GOLD);
      if(it->view_offset && it->duration) {
        unsigned progress=(unsigned)(((uint64_t)PW*it->view_offset)/it->duration);if(progress>PW)progress=PW;
        rect(x,y+PH-3,(int)progress,3,GOLD);
      }
      text(it->title,x-1,y+PH+6,0,selected?WHITE:GREY,132);
    }
  }
  char info[80];int absolute=v->offset+(v->n?v->cursor+1:0);
  snprintf(info,sizeof(info),"%d / %d",absolute,v->total);text(info,828,65,0,GOLD,100);
  if(v->n && v->notice && *v->notice)text(v->notice,202,497,0,GOLD,716);
  text("Back",214,519,0,GREY,110);text("Search",342,519,0,GREY,110);text("Refresh",484,519,0,GREY,120);text("< Previous",634,519,0,GREY,140);text("Next >",810,519,0,GREY,110);
}
void gui_draw_grid(const gui_view_t *v){draw_grid(v,-1);}
void gui_message(const char *title,const char *message,const char *detail) {
  if(!fb)return;
  shell(title,"PLEX FOR VITA",-1);rect(224,150,676,274,PANEL);
  rect(250,179,40,4,GOLD);text(message,250,204,2,WHITE,620);
  wrap(detail,250,264,618,4);present();
}

static void cache_path(const char *server,const char *thumb,char out[120]) {
  uint32_t hash=2166136261u;
  for(const char *p=server;*p;p++)hash=(hash^(unsigned char)*p)*16777619u;
  for(const char *p=thumb;*p;p++)hash=(hash^(unsigned char)*p)*16777619u;
  snprintf(out,120,ART_DIR "%08x.jpg",hash);
}
#ifdef __vita__
static void cache_trim(unsigned reserve);
#endif
static artwork_t *load_art(const gui_view_t *v,const browse_item_t *it) {
  if(!it->thumb[0] || !v->server || !v->token)return NULL;
  char path[120],url[1800],enc[768],tok[384];cache_path(v->server,it->thumb,path);
  FILE *file=fopen(path,"rb");
#ifdef __vita__
  if(!file && !art_stop) {
    cache_trim(512*1024);
    plex_url_encode(it->thumb,enc,sizeof(enc));plex_url_encode(v->token,tok,sizeof(tok));
    snprintf(url,sizeof(url),"%s/photo/:/transcode?width=160&height=240&minSize=0&format=jpeg&url=%s&X-Plex-Token=%s",v->server,enc,tok);
    if(http_download_art(url,path,&art_stop)==0)file=fopen(path,"rb");
  }
#else
  (void)url;(void)enc;(void)tok;
#endif
  if(!file)return NULL;
  fseek(file,0,SEEK_END);long len=ftell(file);rewind(file);
  if(len<=0 || len>512*1024){fclose(file);remove(path);return NULL;}
  unsigned char *data=malloc((size_t)len),*img=NULL;artwork_t *result=NULL;int w,h,comp;
  if(data && fread(data,1,(size_t)len,file)==(size_t)len &&
      stbi_info_from_memory(data,(int)len,&w,&h,&comp) && w>0 && h>0 && w<=2048 && h<=2048)
    img=stbi_load_from_memory(data,(int)len,&w,&h,&comp,3);
  if(img && (result=malloc(sizeof(*result)))) {
    int dw=PW,dh=(int)((int64_t)PW*h/w);
    if(dh>PH){dh=PH;dw=(int)((int64_t)PH*w/h);}
    if(dw<1)dw=1;if(dh<1)dh=1;result->width=dw;result->height=dh;
    for(int y=0;y<dh;y++)for(int x=0;x<dw;x++)memcpy(result->pixels+(y*dw+x)*3,img+((y*h/dh)*w+x*w/dw)*3,3);
  }
  stbi_image_free(img);free(data);fclose(file);
  if(!img)remove(path);
  return result;
}
#ifdef __vita__
static int art_worker(SceSize argc,void *arg) {
  (void)argc;(void)arg;
  for(int cell=0;cell<PAGE && !art_stop;cell++) {
    int i=art_page*PAGE+cell;if(i>=art_view.n)break;
    artwork_t *img=load_art(&art_view,art_view.items+i);
    if(art_stop){free(img);break;}
    __atomic_store_n(art+cell,img,__ATOMIC_RELEASE);__atomic_add_fetch(&art_version,1,__ATOMIC_RELEASE);
  }
  return 0;
}
#endif
static void start_art(const gui_view_t *v,int page) {
  stop_art();art_stop=0;art_view=*v;art_page=page;
#ifdef __vita__
  sceIoMkdir("ux0:data/plex-client/art",0777);
  http_prepare(12);
  art_tid=performance_thread("plex_art",art_worker,0x10000140,0x10000,0x40000);
  if(art_tid>=0 && sceKernelStartThread(art_tid,0,NULL)<0){sceKernelDeleteThread(art_tid);art_tid=-1;}
#else
  for(int i=0;i<PAGE && page*PAGE+i<v->n;i++)art[i]=load_art(v,v->items+page*PAGE+i);
#endif
}
#ifdef __vita__
#define TOUCH_EVENT (1u<<31)
typedef struct { unsigned old; int held;touch_state_t touch; } input_t;
static void input_init(input_t *in) {SceCtrlData p={0};sceCtrlPeekBufferPositive(0,&p,1);in->old=p.buttons;in->held=0;touch_init(&in->touch);}
static unsigned input_read(input_t *in) {
  performance_poll();
  SceCtrlData p={0};sceCtrlPeekBufferPositive(0,&p,1);unsigned fresh=p.buttons&~in->old;
  unsigned dirs=p.buttons&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_UP|SCE_CTRL_DOWN);
  if(dirs && p.buttons==in->old){in->held++;if(in->held>=24 && !(in->held%5))fresh|=dirs;}else in->held=0;
  in->old=p.buttons;if(touch_poll(&in->touch))fresh|=TOUCH_EVENT;sceKernelDelayThread(16000);return fresh;
}
#endif
int gui_browse_view(gui_view_t *v) {
  if(v->cursor<0)v->cursor=0;if(v->cursor>=v->n)v->cursor=v->n?v->n-1:0;
  int nav=-1,loaded=-1,dirty=1,version=-1,result=GUI_QUIT;
#ifdef __vita__
  input_t in;input_init(&in);
  for(;;) {
    int per=v->libraries?6:PAGE,page=v->cursor/per;
    if(!v->libraries && loaded!=page){start_art(v,page);loaded=page;dirty=1;}
    int observed=__atomic_load_n(&art_version,__ATOMIC_ACQUIRE);
    if(observed!=version){version=observed;dirty=1;}
    if(dirty){draw_grid(v,nav);present();dirty=0;}
    unsigned p=input_read(&in);if(!p)continue;dirty=1;
    if(p&TOUCH_EVENT){int x=in.touch.x,y=in.touch.y;int selected=touch_sidebar(x,y);
      if(selected>=0){int actions[]={GUI_VIEWS,GUI_HOME,GUI_SEARCH,GUI_REFRESH,GUI_SCAN,GUI_SETTINGS};result=actions[selected];break;}
      if(x<176 && y>=451 && y<478){result=GUI_SETTINGS;break;}if(x<176 && y>=481){result=GUI_QUIT;break;}
      int cell=touch_grid(x,y,v->libraries),index=page*per+cell;if(cell>=0 && index<v->n){v->cursor=index;result=index;break;}
      if(y>=514 && x>=202){result=x<330?GUI_BACK:x<470?GUI_SEARCH:x<620?GUI_REFRESH:x<780?GUI_PREVIOUS:GUI_NEXT;if(result==GUI_PREVIOUS && !v->offset && !page)continue;if(result==GUI_NEXT && (page+1)*per>=v->n && v->offset+v->n>=v->total)continue;
        if(result==GUI_PREVIOUS && page){v->cursor=(page-1)*per;continue;}if(result==GUI_NEXT && (page+1)*per<v->n){v->cursor=(page+1)*per;continue;}break;}
      continue;
    }
    if(p&SCE_CTRL_START){result=GUI_QUIT;break;}
    if(p&SCE_CTRL_SELECT){result=GUI_SETTINGS;break;}
    if(p&SCE_CTRL_TRIANGLE){result=GUI_SEARCH;break;}
    if(p&SCE_CTRL_SQUARE){result=GUI_REFRESH;break;}
    if(p&SCE_CTRL_CIRCLE){if(nav>=0){nav=-1;continue;}result=GUI_BACK;break;}
    if(nav>=0) {
      if(p&SCE_CTRL_UP && nav>0)nav--;if(p&SCE_CTRL_DOWN && nav<5)nav++;
      if(p&SCE_CTRL_RIGHT){nav=-1;continue;}
      if(p&SCE_CTRL_CROSS){int actions[]={GUI_VIEWS,GUI_HOME,GUI_SEARCH,GUI_REFRESH,GUI_SCAN,GUI_SETTINGS};result=actions[nav];break;}
      continue;
    }
    if(p&SCE_CTRL_CROSS && v->n){result=v->cursor;break;}
    int cols=v->libraries?3:COLS;
    if(p&SCE_CTRL_LEFT){if(v->cursor%cols==0)nav=0;else v->cursor--;}
    if(p&SCE_CTRL_RIGHT && v->cursor+1<v->n)v->cursor++;
    if(p&SCE_CTRL_UP && v->cursor>=cols)v->cursor-=cols;
    if(p&SCE_CTRL_DOWN && v->cursor+cols<v->n)v->cursor+=cols;
    else if(p&SCE_CTRL_DOWN && v->n && v->offset+v->n<v->total){result=GUI_NEXT;break;}
    if(p&SCE_CTRL_RTRIGGER) {
      if((page+1)*per<v->n)v->cursor=(page+1)*per;
      else if(v->offset+v->n<v->total){result=GUI_NEXT;break;}
    }
    if(p&SCE_CTRL_LTRIGGER) {
      if(page>0)v->cursor=(page-1)*per;
      else if(v->offset>0){result=GUI_PREVIOUS;break;}
    }
  }
#else
  (void)nav;(void)dirty;(void)version;loaded=v->cursor/(v->libraries?6:PAGE);
  if(!v->libraries)start_art(v,loaded);draw_grid(v,-1);present();
#endif
  stop_art();return result;
}
static void draw_choices(const char *title,const char *subtitle,const char **rows,int count,int cursor) {
  shell(title,subtitle,-2);int first=(cursor/7)*7;
  for(int i=first;i<count && i<first+7;i++) {
    int y=123+(i-first)*48;
    rect(220,y,680,40,i==cursor?TILE:PANEL);if(i==cursor)rect(220,y,3,40,GOLD);
    text(rows[i],240,y+6,1,i==cursor?WHITE:GREY,640);
  }
  rect(220,510,210,30,TILE);rect(470,510,140,30,TILE);rect(640,510,130,30,TILE);rect(800,510,100,30,TILE);text("Back",232,514,0,GREY,190);text("Up",482,514,0,GREY,115);text("Down",652,514,0,GREY,105);text("Exit",812,514,0,GREY,75);
}
int gui_choice_cursor(const char *title,const char *subtitle,const char **rows,int count,int *selection) {
  int cursor=selection?*selection:0;if(cursor<0 || cursor>=count)cursor=0;
#ifdef __vita__
  input_t in;input_init(&in);int dirty=1;
  for(;;) {
    if(dirty){draw_choices(title,subtitle,rows,count,cursor);present();dirty=0;}
    unsigned p=input_read(&in);if(!p)continue;dirty=1;
    if(p&TOUCH_EVENT){int index=touch_choice(in.touch.x,in.touch.y);if(index>=0){index+=(cursor/7)*7;if(index<count){if(selection)*selection=index;return index;}}
      if(in.touch.y>=510){if(in.touch.x<450)return GUI_BACK;if(in.touch.x>=780)return GUI_QUIT;p|=in.touch.x<620?SCE_CTRL_UP:SCE_CTRL_DOWN;}else continue;}
    if(p&SCE_CTRL_CIRCLE)return GUI_BACK;if(p&SCE_CTRL_START)return GUI_QUIT;
    if(p&SCE_CTRL_UP && cursor>0)cursor--;if(p&SCE_CTRL_DOWN && cursor+1<count)cursor++;
    if(p&SCE_CTRL_CROSS && count>0){if(selection)*selection=cursor;return cursor;}
  }
#else
  draw_choices(title,subtitle,rows,count,cursor);present();return GUI_BACK;
#endif
}
int gui_choice(const char *title,const char *subtitle,const char **rows,int count){return gui_choice_cursor(title,subtitle,rows,count,NULL);}
int gui_wait(volatile int *done,void (*cancel)(void)) {
#ifdef __vita__
 input_t in;input_init(&in);int action=0;
 while(!__atomic_load_n(done,__ATOMIC_ACQUIRE)){
  unsigned p=input_read(&in);if(p&TOUCH_EVENT)p|=SCE_CTRL_CIRCLE;if(!action && p&(SCE_CTRL_CIRCLE|SCE_CTRL_START)){action=p&SCE_CTRL_START?GUI_QUIT:GUI_BACK;if(cancel)cancel();}
 }
 return action;
#else
 (void)done;(void)cancel;return 0;
#endif
}
#define CACHE_BUDGET (32u*1024*1024)
static int cache_file(const char *name){size_t n=strlen(name);return n==12 && !strcmp(name+8,".jpg") && strspn(name,"0123456789abcdef")==8;}
#ifdef __vita__
static void cache_trim(unsigned reserve) {
 for(;;){DIR *dir=opendir(ART_DIR);if(!dir)return;struct dirent *entry;unsigned long long total=0;
  char oldest[160]={0};time_t oldest_time=0;
  while((entry=readdir(dir))){if(!cache_file(entry->d_name))continue;char path[160];struct stat info;snprintf(path,sizeof(path),ART_DIR "%.12s",entry->d_name);
   if(!stat(path,&info)){total+=(unsigned long long)info.st_size;if(!oldest[0] || info.st_mtime<oldest_time){snprintf(oldest,sizeof(oldest),"%s",path);oldest_time=info.st_mtime;}}}
  closedir(dir);if(total+reserve<=CACHE_BUDGET || !oldest[0] || remove(oldest))return;
 }
}
#endif
int gui_cache_clear(void){stop_art();DIR *dir=opendir(ART_DIR);if(!dir)return 0;struct dirent *entry;int result=0;
 while((entry=readdir(dir)))if(cache_file(entry->d_name)){char path[160];snprintf(path,sizeof(path),ART_DIR "%.12s",entry->d_name);if(remove(path))result=-1;}closedir(dir);return result;}
int gui_keyboard(const char *title,char *value,unsigned size,int masked) {
  static const char *pages[4][6]={
 {"abcdefghijklm","nopqrstuvwxyz","ABCDEFGHIJKLM","NOPQRSTUVWXYZ","0123456789-_.","/:?&=+@!#%() "},
 {"àáâãäåæçèéêëì","íîïñòóôõöøœùú","ûüýÿßÀÁÂÃÄÅÆÇ","ÈÉÊËÌÍÎÏÑÒÓÔÕ","ÖØŒÙÚÛÜÝąćęłń","óśźżčšžğİı "},
 {"αβγδεζηθικλμν","ξοπρστυφχψως","ΑΒΓΔΕΖΗΘΙΚΛΜΝ","ΞΟΠΡΣΤΥΦΧΨΩ","άέήίόύώϊϋΐΰ","0123456789-. "},
 {"абвгдеёжзийкл","мнопрстуфхцчш","щъыьэюяієїґ ","АБВГДЕЁЖЗИЙКЛ","МНОПРСТУФХЦЧШ","ЩЪЫЬЭЮЯІЄЇҐ "}};
  int page=0;const char **keys=pages[page];
  char buffer[256];if(!size || size>sizeof(buffer))return GUI_BACK;
  snprintf(buffer,sizeof(buffer),"%s",value);int row=0,col=0;
#ifdef __vita__
  input_t in;input_init(&in);int dirty=1;
  for(;;) {
    if(dirty) {
      shell(title,masked?"Your token is stored on this Vita":"D-pad types; L/R switches character pages",-2);
      rect(204,114,720,64,PANEL);char shown[256];snprintf(shown,sizeof(shown),"%s",buffer);
      if(masked)memset(shown,'*',strlen(shown));
      const char *tail=shown;while(*tail && width(tail,1)>670)text_next(&tail);
      text(tail,224,130,1,WHITE,674);
      for(int r=0;r<6;r++)for(unsigned c=0;c<text_count(keys[r]);c++) {
        int x=212+c*54,y=199+r*45,sel=r==row && c==(unsigned)col;
        rect(x,y,48,37,sel?GOLD:TILE);char label[5]={0};const char *character=text_at(keys[r],c),*end=character;text_next(&end);memcpy(label,character,(size_t)(end-character));
        if(*character==' ')snprintf(label,sizeof(label),"sp");
        text(label,x+15,y+4,1,sel?BLACK:WHITE,45);
      }
      rect(212,492,146,36,TILE);rect(378,492,146,36,TILE);rect(544,492,146,36,TILE);rect(710,492,198,36,TILE);text("Delete",224,500,0,GREY,122);text("Save",390,500,0,GREY,122);text("Cancel",556,500,0,GREY,122);text(masked?"ASCII only":"Characters >",722,500,0,GREY,175);present();dirty=0;
    }
    unsigned p=input_read(&in);if(!p)continue;dirty=1;
    if(p&TOUCH_EVENT){int x=in.touch.x,y=in.touch.y;if(y>=492 && x>=212){p|=x<370?SCE_CTRL_SQUARE:x<535?SCE_CTRL_TRIANGLE:x<700?SCE_CTRL_CIRCLE:SCE_CTRL_RTRIGGER;}
      else if(x>=212 && y>=199 && y<469 && (x-212)%54<48 && (y-199)%45<37){int r=(y-199)/45,c=(x-212)/54;if(r<6 && (unsigned)c<text_count(keys[r])){row=r;col=c;p|=SCE_CTRL_CROSS;}else continue;}else continue;}
    if(p&SCE_CTRL_CIRCLE)return GUI_BACK;if(p&SCE_CTRL_START)return GUI_QUIT;
    if(!masked && p&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER)){page=(page+(p&SCE_CTRL_LTRIGGER?3:1))%4;keys=pages[page];row=col=0;}
    if(p&SCE_CTRL_UP)row=(row+5)%6;if(p&SCE_CTRL_DOWN)row=(row+1)%6;
    int len=(int)text_count(keys[row]);if(col>=len)col=len-1;
    if(p&SCE_CTRL_LEFT)col=(col+len-1)%len;if(p&SCE_CTRL_RIGHT)col=(col+1)%len;
    if(p&SCE_CTRL_SQUARE)text_delete(buffer);
    if(p&SCE_CTRL_CROSS)text_append(buffer,size,text_at(keys[row],(unsigned)col));
    if(p&SCE_CTRL_TRIANGLE){snprintf(value,size,"%s",buffer);return 0;}
  }
#else
  (void)keys;(void)masked;(void)row;(void)col;(void)title;return GUI_BACK;
#endif
}
int gui_details(const browse_item_t *it,const char *server,const char *token,const char *notice) {
  gui_view_t v={.server=server,.token=token};art_stop=0;
  artwork_t *img=NULL;
  (void)v;
  // Details only use an existing poster; never block controller input for artwork.
  char art_path[120];cache_path(server,it->thumb,art_path);FILE *cached=fopen(art_path,"rb");
  if(cached){fclose(cached);img=load_art(&v,it);}
  int choice=0,can_resume=it->view_offset>10000 && it->view_offset+10000<it->duration;
  int result=GUI_BACK;
#ifdef __vita__
  input_t in;input_init(&in);
#endif
  int dirty=1;
  for(;;) {
    if(dirty) {
      shell(it->parent_title[0]?it->parent_title:kind(it),"DETAILS",-2);
      rect(214,132,184,260,TILE);if(img)poster(img,214,132,184,260);
      text(it->title,422,120,2,WHITE,492);
      char meta[160];snprintf(meta,sizeof(meta),"%s   %s   %u min",it->year,it->content_rating,it->duration/60000);
      if(!strcmp(it->type,"episode"))snprintf(meta,sizeof(meta),"Season %d   Episode %d   %u min",it->parent_index,it->index,it->duration/60000);
      if(!strcmp(it->type,"track"))snprintf(meta,sizeof(meta),"%s   Track %d   %u:%02u",it->year,it->index,it->duration/60000,(it->duration/1000)%60);
      text(meta,422,173,0,GOLD,488);wrap(it->summary[0]?it->summary:"No description provided by the server.",422,212,482,6);
      const char *labels[]={can_resume?"Resume":"Play","Play from beginning","Back"};int count=can_resume?3:2;
      for(int i=0;i<count;i++) {
        const char *label=can_resume?labels[i]:i==0?"Play":"Back";
        rect(214+i*230,432,210,44,choice==i?GOLD:TILE);text(label,228+i*230,440,1,choice==i?BLACK:WHITE,190);
      }
      if(notice && *notice)text(notice,214,482,0,GOLD,702);text("Back",228,514,0,GREY,300);text("Options / tracks",600,514,0,GREY,315);present();dirty=0;
    }
#ifdef __vita__
    unsigned p=input_read(&in);if(!p)continue;dirty=1;
    if(p&TOUCH_EVENT){int x=in.touch.x,y=in.touch.y;if(y>=432 && y<476 && x>=214){int selected=(x-214)/230,count=can_resume?3:2;if(selected<count && (x-214)%230<210){choice=selected;p|=SCE_CTRL_CROSS;}else continue;}
      else if(y>=510){if(x<550)p|=SCE_CTRL_CIRCLE;else p|=SCE_CTRL_SQUARE;}else continue;}
    if(p&SCE_CTRL_CIRCLE)break;if(p&SCE_CTRL_START){result=GUI_QUIT;break;}
    if(p&SCE_CTRL_SQUARE){result=3;break;}
    int count=can_resume?3:2;
    if(p&SCE_CTRL_LEFT && choice>0)choice--;if(p&SCE_CTRL_RIGHT && choice+1<count)choice++;
    if(p&SCE_CTRL_CROSS){if(choice==count-1)break;result=can_resume && choice==0?1:0;break;}
#else
    break;
#endif
  }
  free(img);return result;
}
void gui_player_overlay(unsigned *buffer,const char *title,unsigned pos,unsigned duration,int paused,const char *message) {
  unsigned *saved=fb;fb=buffer;
  rect(0,0,W,64,PANEL);text(title,24,13,1,WHITE,904);
  rect(0,H-80,W,80,PANEL);rect(24,H-48,912,3,TILE);
  unsigned progress=duration?(unsigned)((uint64_t)912*pos/duration):0;if(progress>912)progress=912;
  rect(24,H-48,(int)progress,3,GOLD);
  char time[100];snprintf(time,sizeof(time),"%s  %u:%02u / %u:%02u",paused?"Paused":"Playing",pos/60000,(pos/1000)%60,duration/60000,(duration/1000)%60);
  text(message && *message?message:time,24,H-78,0,GOLD,890);
  text("Pause / Resume",24,H-33,0,WHITE,205);text("Stop",264,H-33,0,WHITE,140);text("-10 sec",444,H-33,0,WHITE,160);text("+10 sec",634,H-33,0,WHITE,160);text("Controls",824,H-33,0,WHITE,120);
  fb=saved;
}
int gui_snapshot(const char *path) {
  FILE *f=fopen(path,"wb");if(!f || !buffers[0]){if(f)fclose(f);return -1;}
  fprintf(f,"P6\n960 544\n255\n");unsigned *pixels=buffers[draw_buffer^1];
  for(int i=0;i<W*H;i++){unsigned char rgb[3]={pixels[i]&255,(pixels[i]>>8)&255,(pixels[i]>>16)&255};fwrite(rgb,1,3,f);}
  return fclose(f);
}

int gui_photo(const char *title,const char *path){FILE *file=fopen(path,"rb");if(!file)return -3;if(fseek(file,0,SEEK_END)){fclose(file);return -3;}long bytes=ftell(file);rewind(file);if(bytes<=0 || bytes>512*1024){fclose(file);return -3;}unsigned char *data=malloc((size_t)bytes),*image=NULL;int w=0,h=0,channels;
 if(data && fread(data,1,(size_t)bytes,file)==(size_t)bytes && stbi_info_from_memory(data,(int)bytes,&w,&h,&channels) && w>0 && h>0 && w<=2048 && h<=2048)image=stbi_load_from_memory(data,(int)bytes,&w,&h,&channels,3);fclose(file);free(data);if(!image)return -3;
 int controls=1,dirty=1,result=GUI_BACK;
#ifdef __vita__
 input_t input;input_init(&input);
#endif
 for(;;){if(dirty){rect(0,0,W,H,BLACK);int max_h=controls?H-112:H,dw=W,dh=(int)((long long)W*h/w);if(dh>max_h){dh=max_h;dw=(int)((long long)max_h*w/h);}if(dw<1)dw=1;if(dh<1)dh=1;int left=(W-dw)/2,top=(H-dh)/2;
  for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){const unsigned char *pixel=image+((y*h/dh)*w+x*w/dw)*3;fb[(top+y)*W+left+x]=pixel[0]|(pixel[1]<<8)|(pixel[2]<<16)|0xFF000000u;}
  if(controls){rect(0,0,W,64,PANEL);text(title,24,16,1,WHITE,912);rect(0,H-48,W,48,PANEL);rect(0,H-48,220,48,TILE);text("Back",24,H-35,0,GREY,190);text("Hide controls / Triangle",264,H-35,0,GREY,660);}present();dirty=0;}
#ifdef __vita__
  unsigned pressed=input_read(&input);if(pressed&SCE_CTRL_START){result=GUI_QUIT;break;}if(pressed&SCE_CTRL_CIRCLE)break;if(pressed&TOUCH_EVENT){if(controls && input.touch.y>=H-48 && input.touch.x<220)break;controls=!controls;dirty=1;}if(pressed&SCE_CTRL_TRIANGLE){controls=!controls;dirty=1;}
#else
  break;
#endif
 }stbi_image_free(image);return result;
}
