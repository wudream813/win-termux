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

void update_host_title(void);
void render_screen(void);
void draw_tab_bar(char *out, int bs, int *posp);
void render_help_content(char *out, int bs, int *posp, int host_rows, int host_cols);
void render_chooser(char *out, int bs, int *posp, int host_rows, int host_cols);
void chooser_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h);
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
void render_cleanup(void);

#endif // WIN_TERMUX_RENDER_H
