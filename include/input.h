#ifndef WIN_TERMUX_INPUT_H
#define WIN_TERMUX_INPUT_H

#include "common.h"
#include "types.h"
#include "screen.h"
#include "utf8.h"
#include "config.h"
#include "pane.h"
#include "render.h"

void handle_key(KEY_EVENT_RECORD *ke);
void handle_mouse(MOUSE_EVENT_RECORD *me);
void handle_prefix(WORD vk, DWORD ctrl, WCHAR uc);
void handle_settings_key(KEY_EVENT_RECORD *ke);
void handle_settings_mouse(MOUSE_EVENT_RECORD *me);
void handle_copy_mode_key(KEY_EVENT_RECORD *ke);
void handle_search_key(KEY_EVENT_RECORD *ke);
void copy_range_to_clipboard(Pane *p, int sx, int sy_abs, int ex, int ey_abs);
void execute_search(void);
void search_jump_next(void);
void search_jump_prev(void);
void do_scroll(int d);

#endif // WIN_TERMUX_INPUT_H
