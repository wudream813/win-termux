#ifndef WIN_TERMUX_RENDER_H
#define WIN_TERMUX_RENDER_H

#include "common.h"
#include "types.h"
#include "screen.h"
#include "utf8.h"
#include "config.h"
#include "theme.h"
#include "keymap.h"

#define SETTINGS_SIDEBAR_W 22

/* 设置页三个新分类的固定行号（渲染与鼠标命中共用） */
#define SETTINGS_THEME_ROW0     5
#define SETTINGS_ROLE_ROW0      13
#define SETTINGS_ROLE_ROWS      8
#define SETTINGS_ROLE_COL_W     34
#define SETTINGS_KEYS_ROW0      6
#define SETTINGS_BEHAVIOR_ROW0  6
/* 相对 main_left 的按钮列偏移，渲染时用绝对定位写出，鼠标按同样的偏移命中 */
#define SETTINGS_KEYS_EDIT_COL  58
#define SETTINGS_KEYS_RESET_COL 63
#define SETTINGS_SB_MINUS_COL   22
#define SETTINGS_SB_PLUS_COL    33
#define RENAME_W 30
#define RENAME_H 3
#define CMD_BOX_W 38
#define CMD_BOX_H 4
#define CTX_W 24
#define CTX_H 4
#define CP_W 30
#define CP_H 4

/* Command palette pages.  palette_mode remains a boolean so the rest of the
 * input/render pipeline can keep treating the palette as a modal overlay. */
enum {
    PALETTE_PAGE_ROOT = 0,
    PALETTE_PAGE_OPERATIONS,
    PALETTE_PAGE_SETTINGS,
    PALETTE_PAGE_NEW_TERMINAL,
    PALETTE_PAGE_SWITCH_PANEL,
    PALETTE_PAGE_DEFAULT_STARTUP,
    PALETTE_PAGE_ADD_PANEL,
    PALETTE_PAGE_MENU_SETTINGS,
    PALETTE_PAGE_PANEL_EDITOR
};

enum {
    PALETTE_FOCUS_INPUT = 0,
    PALETTE_FOCUS_LIST = 1
};

typedef enum {
    PALETTE_ACTION_NONE = 0,
    PALETTE_ACTION_OPEN_OPERATIONS,
    PALETTE_ACTION_OPEN_SETTINGS,
    PALETTE_ACTION_OPEN_NEW_TERMINAL,
    PALETTE_ACTION_START_CUSTOM,
    PALETTE_ACTION_RENAME,
    PALETTE_ACTION_COLOR,
    PALETTE_ACTION_SEARCH,
    PALETTE_ACTION_SWITCH_PANEL,
    PALETTE_ACTION_COPY_MODE,
    PALETTE_ACTION_RELOAD,
    PALETTE_ACTION_GRAPHICAL_SETTINGS,
    PALETTE_ACTION_CLOSE_PANEL,
    PALETTE_ACTION_QUIT,
    PALETTE_ACTION_DEFAULT_STARTUP,
    PALETTE_ACTION_OPEN_INI,
    PALETTE_ACTION_ADD_PANEL,
    PALETTE_ACTION_MENU_SETTINGS,
    PALETTE_ACTION_OPEN_ABOUT,
    PALETTE_ACTION_EDIT_PANEL,
    PALETTE_ACTION_SELECT_TERMINAL,
    PALETTE_ACTION_SELECT_PANEL,
    PALETTE_ACTION_SELECT_DEFAULT,
    PALETTE_ACTION_NEXT_THEME
} PaletteAction;

typedef struct {
    const char *id;
    const char *title;
    const char *desc;
    const char *shortcut;
    PaletteAction action;
    int value;
    int number;
    int color;
} PaletteItemInfo;

void update_host_title(void);
void render_screen(void);
void draw_tab_bar(char *out, int bs, int *posp);
void render_help_content(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_chooser(char *out, int bs, int *posp, int host_rows, int host_cols);
void chooser_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h);
/* Popup left edge in ANSI's 1-based column space.  Mouse anchors remain 0-based. */
int popup_left_1based(int anchor0, int width, int host_cols);
void render_custom_cmd_box(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_rename_box(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_ctx_menu(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_color_picker(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_settings_presets(char *out, int bs, int *posp, int host_rows, int host_cols);
void presets_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h, int *max_nw, int *max_cw);
void render_settings_panel(char *out, int bs, int *posp, int host_rows, int host_cols);
void settings_sidebar_extra_rows(int *appearance_r, int *keys_r, int *behavior_r);
int settings_theme_row(int idx);
int settings_role_row(int role);
int settings_role_col(int main_left, int role);
int settings_keys_rows(void);
int settings_keys_visible(int host_rows);
int settings_keys_row_at(int host_rows, int entry);
int settings_keys_entry_at(int host_rows, int row);
void render_search_box(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_confirm_exit(char *out, int bs, int *posp, int host_rows, int host_cols);
void confirm_exit_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h);
void render_command_palette(char *out, int bs, int *posp, int host_rows, int host_cols);
void palette_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h);
int palette_visible_rows(int host_rows);
int palette_item_count(int page);
int palette_filter_cmds(int page, int *out_indices, int max_out, const char *query);
int palette_item_info(int page, int item_index, PaletteItemInfo *out);
void palette_editor_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h, int *input_w);
void render_cleanup(void);

#endif // WIN_TERMUX_RENDER_H
