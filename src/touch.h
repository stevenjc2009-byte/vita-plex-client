#pragma once
typedef struct {int down,start_x,start_y,x,y,moved;} touch_state_t;
void touch_init(touch_state_t *state);
int touch_poll(touch_state_t *state);
int touch_sidebar(int x,int y);
int touch_grid(int x,int y,int libraries);
int touch_choice(int x,int y);
