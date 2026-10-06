#include "touch.h"
#include <string.h>
#ifndef PLEX_TOUCH_EXTERNAL
#if defined(__vita__) && !defined(PLEX_MOCK_VITA)
#include <psp2/touch.h>
void touch_init(touch_state_t *s){memset(s,0,sizeof(*s));sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);SceTouchData d={0};if(sceTouchPeek(SCE_TOUCH_PORT_FRONT,&d,1)>0 && d.reportNum){s->down=1;s->moved=1;}}
int touch_poll(touch_state_t *s){SceTouchData d={0};if(sceTouchPeek(SCE_TOUCH_PORT_FRONT,&d,1)<0)return 0;if(d.reportNum){int x=d.report[0].x*960/1920,y=d.report[0].y*544/1088;
 if(!s->down){s->start_x=x;s->start_y=y;s->moved=0;}if(d.reportNum>1 || x-s->start_x>24 || s->start_x-x>24 || y-s->start_y>24 || s->start_y-y>24)s->moved=1;s->down=1;s->x=x;s->y=y;return 0;}
 if(s->down){s->down=0;return !s->moved;}return 0;}
#else
void touch_init(touch_state_t *s){memset(s,0,sizeof(*s));}int touch_poll(touch_state_t *s){(void)s;return 0;}
#endif
#endif
int touch_sidebar(int x,int y){if(x<12 || x>=164 || y<124)return -1;int n=(y-124)/52;return n<6 && (y-124)%52<42?n:-1;}
int touch_grid(int x,int y,int libraries){int left=libraries?202:214,top=libraries?126:128,dx=libraries?244:144,dy=libraries?166:184,cols=libraries?3:5,width=libraries?228:132,height=libraries?144:178;
 if(x<left || y<top){return -1;}
 int col=(x-left)/dx,row=(y-top)/dy;if(col>=cols || row>=2 || (x-left)%dx>=width || (y-top)%dy>=height)return -1;return row*cols+col;}
int touch_choice(int x,int y){if(x<220 || x>=900 || y<123)return -1;int n=(y-123)/48;return n<7 && (y-123)%48<40?n:-1;}
