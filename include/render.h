#ifndef WIN_TERMUX_RENDER_H
#define WIN_TERMUX_RENDER_H

#include "common.h"
#include "types.h"
#include "screen.h"
#include "utf8.h"
#include "config.h"

#define SETTINGS_SIDEBAR_W 22
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
    PALETTE_ACTION_SELECT_DEFAULT
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
void render_search_box(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_command_palette(char *out, int bs, int *posp, int host_rows, int host_cols);
void palette_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h);
int palette_visible_rows(int host_rows);
int palette_item_count(int page);
int palette_filter_cmds(int page, int *out_indices, int max_out, const char *query);
int palette_item_info(int page, int item_index, PaletteItemInfo *out);
void palette_editor_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h, int *input_w);
void render_cleanup(void);

#endif // WIN_TERMUX_RENDER_H
