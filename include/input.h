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
void action_execute(int action, int arg, DWORD ctrl);
void handle_prefix(WORD vk, DWORD ctrl, WCHAR uc);
void handle_settings_key(KEY_EVENT_RECORD *ke);
void handle_settings_mouse(MOUSE_EVENT_RECORD *me);
/* Returns 1 when the key was NOT consumed and copy mode has been left, so the
 * caller should keep processing it (Shift/Alt 点选会话里的“其它键退出并发送”). */
int handle_copy_mode_key(KEY_EVENT_RECORD *ke);
void handle_search_key(KEY_EVENT_RECORD *ke);
void handle_palette_key(KEY_EVENT_RECORD *ke);
void handle_palette_mouse(MOUSE_EVENT_RECORD *me);
void open_command_palette(void);
void execute_palette_command(int item_index);
void copy_range_to_clipboard(Pane *p, int sx, int sy_abs, int ex, int ey_abs);
/* block = 1 复制矩形区域（每行取同一段列），block = 0 复制连续文本流。 */
void copy_selection_to_clipboard(Pane *p, int sx, int sy_abs, int ex, int ey_abs, int block);
void execute_search(void);
void search_jump_next(void);
void search_jump_prev(void);
void do_scroll(int d);

#endif // WIN_TERMUX_INPUT_H
