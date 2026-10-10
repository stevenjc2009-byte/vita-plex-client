#include "touch.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 touch_state_t s={0};assert(!touch_feed(&s,1,500,400));assert(!touch_feed(&s,1,500,390));assert(!touch_scroll(&s,48,0));assert(touch_feed(&s,0,0,0));
 assert(!touch_feed(&s,1,500,400));assert(!touch_feed(&s,1,503,280));assert(touch_scroll(&s,48,0)==2);assert(!touch_scroll(&s,48,0));assert(!touch_feed(&s,1,503,304));assert(touch_scroll(&s,48,0)==0);assert(!touch_feed(&s,0,0,0));
 assert(!touch_feed(&s,1,500,300));assert(!touch_feed(&s,1,390,305));assert(touch_scroll(&s,90,1)==1 && !touch_scroll(&s,48,0));assert(!touch_feed(&s,0,0,0));
 touch_feed(&s,1,500,400);touch_feed(&s,2,500,200);assert(!touch_scroll(&s,48,0));touch_feed(&s,1,500,100);assert(!touch_scroll(&s,48,0));assert(!touch_feed(&s,0,0,0));
 s.down=s.blocked=s.moved=1;touch_feed(&s,1,500,100);assert(!touch_scroll(&s,48,0) && !touch_feed(&s,0,0,0));
assert(touch_sidebar(30,145)==0 && touch_sidebar(40,350)==4 && touch_sidebar(40,409)==5);assert(touch_sidebar(170,145)<0 && touch_sidebar(30,169)<0);assert(touch_grid(220,145,1)==0 && touch_grid(710,300,1)==5);assert(touch_grid(220,145,0)==0 && touch_grid(800,320,0)==9);assert(touch_grid(330,128,0)==0 && touch_grid(350,128,0)<0);assert(touch_grid(220,510,0)<0);assert(touch_choice(250,130)==0 && touch_choice(250,418)==6 && touch_choice(250,164)<0);puts("Touch hit targets match sidebar, library cards, posters and menu rows");return 0;}
