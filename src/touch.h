#pragma once
typedef struct {int down,start_x,start_y,x,y,moved,blocked,axis,drag;} touch_state_t;
int touch_feed(touch_state_t *state,int contacts,int x,int y);
int touch_scroll(touch_state_t *state,int threshold,int horizontal);
void touch_init(touch_state_t *state);
int touch_poll(touch_state_t *state);
int touch_sidebar(int x,int y);
int touch_grid(int x,int y,int libraries);
int touch_choice(int x,int y);
