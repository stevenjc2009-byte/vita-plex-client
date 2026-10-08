#pragma once
#include "browse.h"
enum { GUI_BACK=-1, GUI_QUIT=-2, GUI_HOME=-3, GUI_SETTINGS=-4,
  GUI_REFRESH=-5, GUI_SEARCH=-6, GUI_NEXT=-7, GUI_PREVIOUS=-8, GUI_SORT=-9,GUI_VIEWS=-10, GUI_SCAN=-11, GUI_SCAN_POLL=-12 };
typedef struct {
  const char *title, *subtitle, *notice, *server, *token;
  const browse_item_t *items;
  int n, libraries, offset, total, cursor,poll_scan;
} gui_view_t;
int gui_init(void);
void gui_shutdown(void);
void gui_message(const char *title, const char *message, const char *detail);
int gui_browse_view(gui_view_t *view);
int gui_choice(const char *title, const char *subtitle, const char **rows, int count);
int gui_keyboard(const char *title, char *value, unsigned size, int masked);
int gui_details(const browse_item_t *item, const char *server, const char *token, const char *notice);
void gui_player_overlay(unsigned int *buffer, const char *title, unsigned position,
  unsigned duration, int paused, const char *message);
// The desktop preview uses the same renderer as the Vita executable.
void gui_draw_grid(const gui_view_t *view);
int gui_snapshot(const char *path);

int gui_wait(volatile int *done,void (*cancel)(void));
int gui_choice_cursor(const char *title,const char *subtitle,const char **rows,int count,int *cursor);
int gui_cache_clear(void);

int gui_photo(const char *title,const char *path);

int gui_up_next(const char *title,int countdown);

void gui_player_end_frame(const unsigned *frame);

void gui_skip_overlay(unsigned *buffer,int credits);
