#include "touch.h"
#include <string.h>
int touch_feed(touch_state_t *s,int contacts,int x,int y){
 if(contacts){
  if(!s->down){s->start_x=s->x=x;s->start_y=s->y=y;s->moved=s->blocked=s->axis=s->drag=0;}
  if(contacts>1){s->blocked=1;s->moved=1;s->drag=0;}
  int dx=x-s->start_x,dy=y-s->start_y,ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
  if(!s->blocked){
   if(!s->axis && (ax>24 || ay>24)){s->axis=ax>ay?1:2;s->moved=1;s->drag=s->axis==1?dx:dy;}
   else if(s->axis)s->drag+=s->axis==1?x-s->x:y-s->y;
  }
  s->down=1;s->x=x;s->y=y;return 0;
 }
 int tap=s->down && !s->moved && !s->blocked;
 s->down=0;s->drag=0;return tap;
}
int touch_scroll(touch_state_t *s,int threshold,int horizontal){
 if(threshold<=0 || s->blocked || s->axis!=(horizontal?1:2))return 0;
 int steps=s->drag/threshold;s->drag-=steps*threshold;return -steps;
}
#ifndef PLEX_TOUCH_EXTERNAL
#if defined(__vita__) && !defined(PLEX_MOCK_VITA)
#include <psp2/touch.h>
void touch_init(touch_state_t *s){memset(s,0,sizeof(*s));sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);SceTouchData d={0};if(sceTouchPeek(SCE_TOUCH_PORT_FRONT,&d,1)>0 && d.reportNum){s->down=1;s->moved=s->blocked=1;}}
int touch_poll(touch_state_t *s){SceTouchData d={0};if(sceTouchPeek(SCE_TOUCH_PORT_FRONT,&d,1)<0){s->blocked=s->moved=1;s->drag=0;return 0;}return touch_feed(s,d.reportNum,d.reportNum?d.report[0].x*960/1920:0,d.reportNum?d.report[0].y*544/1088:0);}
#else
void touch_init(touch_state_t *s){memset(s,0,sizeof(*s));}int touch_poll(touch_state_t *s){(void)s;return 0;}
#endif
#endif
int touch_sidebar(int x,int y){if(x<12 || x>=164 || y<124)return -1;int n=(y-124)/52;return n<6 && (y-124)%52<42?n:-1;}
int touch_grid(int x,int y,int libraries){int left=libraries?202:214,top=libraries?126:128,dx=libraries?244:144,dy=libraries?166:184,cols=libraries?3:5,width=libraries?228:132,height=libraries?144:178;
 if(x<left || y<top){return -1;}
 int col=(x-left)/dx,row=(y-top)/dy;if(col>=cols || row>=2 || (x-left)%dx>=width || (y-top)%dy>=height)return -1;return row*cols+col;}
int touch_choice(int x,int y){if(x<220 || x>=900 || y<123)return -1;int n=(y-123)/48;return n<7 && (y-123)%48<40?n:-1;}
