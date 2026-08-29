#include "render.h"
#include "framediff.h"


#define TB_BG        "\x1b[048;2;022;027;034m"
#define TAB_IN_BG    "\x1b[048;2;033;038;045m"
#define TAB_IN_FG    "\x1b[038;2;139;148;158m"
#define TAB_ACT_BG   "\x1b[048;2;031;111;235m"
#define TAB_ACT_FG   "\x1b[038;2;255;255;255m"
#define BRAND_BG     "\x1b[048;2;137;087;229m"
#define BRAND_BG_HV  "\x1b[048;2;163;113;247m"
#define X_RED        "\x1b[038;2;248;081;073m"
#define X_RED_BG     "\x1b[048;2;248;081;073m"
#define PLUS_GREEN   "\x1b[038;2;063;185;080m"
#define PLUS_GREEN_BG "\x1b[048;2;063;185;080m"
#define DARK_FG      "\x1b[038;2;013;017;023m"

static const char *const TAB_COLOR_BG[9] = {
    "\x1b[048;2;031;111;235m",   // 0 default: blue
    "\x1b[048;2;031;111;235m",   // 1 blue (default highlight)
    "\x1b[048;2;063;185;080m",    // 2 green
    "\x1b[048;2;210;153;034m",   // 3 amber
    "\x1b[048;2;137;087;229m",   // 4 purple
    "\x1b[048;2;031;136;061m",    // 5 teal/green-dark
    "\x1b[048;2;121;192;255m",  // 6 light blue
    "\x1b[048;2;217;119;054m",   // 7 orange
    "\x1b[048;2;205;093;173m",   // 8 pink
};

static const char *const TAB_COLOR_BG_DIM[9] = {
    "\x1b[048;2;022;062;128m",
    "\x1b[048;2;022;062;128m",   // 1 blue dim
    "\x1b[048;2;036;099;049m",
    "\x1b[048;2;110;082;030m",
    "\x1b[048;2;074;048;122m",
    "\x1b[048;2;024;080;048m",
    "\x1b[048;2;052;096;128m",
    "\x1b[048;2;112;066;034m",
    "\x1b[048;2;104;050;090m",
};

static char g_current_host_title[128] = {0};

void update_host_title(void) {
    if (!g_orig_title[0]) {
        GetConsoleTitleW(g_orig_title, 255);
    }
    const char *target = "termux";
    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
        const char *t = g_mux.panes[g_mux.active_pane].title;
        if (t && *t) target = t;
    }
    if (strcmp(g_current_host_title, target) != 0) {
        strncpy(g_current_host_title, target, sizeof(g_current_host_title) - 1);
        g_current_host_title[sizeof(g_current_host_title) - 1] = 0;
        SetConsoleTitleA(g_current_host_title);
        char seq[256];
        int len = snprintf(seq, sizeof(seq), "\x1b]0;%s\x07", g_current_host_title);
        host_write(seq, len);
    }
}

void draw_tab_bar(char *out, int bs, int *posp) {
    int pos = *posp;
    g_mux.tab_count = 0; int col = 0;
    int popup_open = (g_mux.settings_mode || g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode ||
                      g_mux.custom_cmd_mode || g_search_mode || g_mux.palette_mode);
    {
        const char *brand = " termux ";
        int blen = (int)strlen(brand);
        int bcols = utf8_cols(brand, blen);
        if (col + bcols + 4 <= g_mux.host_cols) {
            int bhover = (!popup_open && g_mouse_y == 0 &&
                          g_mouse_x >= col && g_mouse_x < col + bcols);
            g_mux.tab_info[g_mux.tab_count].start_col = col;
            g_mux.tab_info[g_mux.tab_count].end_col = col + bcols;
            g_mux.tab_info[g_mux.tab_count].pane_idx = -2;
            if (bhover)
                pos += snprintf(out + pos, bs - pos, BRAND_BG_HV "\x1b[1m%s\x1b[22m", brand);
            else
                pos += snprintf(out + pos, bs - pos, BRAND_BG "%s", brand);
            col += bcols;
            g_mux.tab_count++;
        }
    }
    for (int i = 0; i < g_mux.pane_count; i++) {
        if (!g_mux.panes[i].active) continue;
        char nm[64]; format_tab_title(nm, sizeof(nm), g_mux.panes[i].title[0] ? g_mux.panes[i].title : "cmd");
        char head[80];
        int hl = snprintf(head, sizeof(head), "[%s", nm);
        int hc = utf8_cols(head, hl);
        int lc = hc + 1;
        if (col + lc + 4 + 4 > g_mux.host_cols) break;
        int hovering = (!popup_open && g_mouse_y == 0 &&
                        g_mouse_x >= col + hc && g_mouse_x < col + lc);
        g_mux.tab_info[g_mux.tab_count].start_col = col;
        g_mux.tab_info[g_mux.tab_count].pane_idx = i;
        int act = (i == g_mux.active_pane);
        int ci = g_mux.panes[i].color;
        if (ci < 0 || ci > 8) ci = 0;
        const char *actbg = TAB_COLOR_BG[ci];
        const char *dimbg = TAB_COLOR_BG_DIM[ci];
        if (act)
            pos += snprintf(out + pos, bs - pos, "%s" TAB_ACT_FG "\x1b[1m%s\x1b[22m", actbg, head);
        else
            pos += snprintf(out + pos, bs - pos, "%s\x1b[038;2;139;148;158m%s", dimbg, head);
        if (hovering)
            pos += snprintf(out + pos, bs - pos, X_RED_BG "\x1b[038;2;255;255;255m\xc3\x97");
        else
            pos += snprintf(out + pos, bs - pos, X_RED "\xc3\x97");
        if (act)
            pos += snprintf(out + pos, bs - pos, "%s" TAB_ACT_FG "]", actbg);
        else
            pos += snprintf(out + pos, bs - pos, "%s\x1b[038;2;139;148;158m]", dimbg);
        g_mux.tab_info[g_mux.tab_count].close_start = col + hc;
        g_mux.tab_info[g_mux.tab_count].close_end = col + hc + 1;
        col += lc + 1;
        g_mux.tab_info[g_mux.tab_count].end_col = col;
        g_mux.tab_count++;
    }
    if (col < g_mux.host_cols - 4) { pos += snprintf(out + pos, bs - pos, TB_BG " "); col++; }
    if (col + 3 <= g_mux.host_cols - 4) {
        g_mux.tab_info[g_mux.tab_count].start_col = col;
        g_mux.tab_info[g_mux.tab_count].end_col = col + 3;
        g_mux.tab_info[g_mux.tab_count].pane_idx = -1;
        int phover = (!popup_open && g_mouse_y == 0 &&
                      g_mouse_x >= col && g_mouse_x < col + 3);
        if (phover)
            pos += snprintf(out + pos, bs - pos, PLUS_GREEN_BG DARK_FG "[+]\x1b[0m");
        else
            pos += snprintf(out + pos, bs - pos, PLUS_GREEN "[+]");
        col += 3;
        g_mux.tab_count++;
    }
    pos += snprintf(out + pos, bs - pos, TB_BG);
    while (col < g_mux.host_cols - 3 && pos < bs - 8) { out[pos++] = ' '; col++; }

    if (col + 3 <= g_mux.host_cols) {
        g_mux.tab_info[g_mux.tab_count].start_col = col;
        g_mux.tab_info[g_mux.tab_count].end_col = col + 3;
        g_mux.tab_info[g_mux.tab_count].pane_idx = -3;
        int shover = (!popup_open && g_mouse_y == 0 && g_mouse_x >= col && g_mouse_x < col + 3);
        if (shover)
            pos += snprintf(out + pos, bs - pos, "\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m[*]\x1b[0m");
        else
            pos += snprintf(out + pos, bs - pos, TAB_IN_BG "\x1b[038;2;121;192;255m[*]\x1b[0m");
        col += 3;
        g_mux.tab_count++;
    }
    pos += snprintf(out + pos, bs - pos, TB_BG);
    while (col < g_mux.host_cols && pos < bs - 4) { out[pos++] = ' '; col++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m");
    *posp = pos;
}

int popup_left_1based(int anchor0, int width, int host_cols) {
    if (width < 1) width = 1;
    if (host_cols < 1) host_cols = 1;

    /* anchor0 is a Windows mouse column (0-based); all drawing and hit
     * testing after this point use ANSI columns (1-based). */
    int anchor = (anchor0 >= 0) ? anchor0 + 1 : 1;
    int left = anchor;
    if (left + width - 1 > host_cols) left = anchor - width + 1;
    if (left < 1) left = 1;
    return left;
}

void chooser_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h) {
    (void)host_rows;
    int mcw = 0;
    for (int i = 0; i < g_chooser_item_count; i++) {
        int w15 = utf8_cols(g_chooser_items[i].name, (int)strlen(g_chooser_items[i].name));
        if (w15 > 15) w15 = 15;
        if (w15 > mcw) mcw = w15;
    }
    int tagw = (g_chooser_item_count >= 10) ? 4 : 3;
    int cw = 1 + 2 + tagw + 1 + mcw + 2;
    if (cw < 20) cw = 20;
    if (cw > host_cols) cw = host_cols;
    int ch = g_chooser_item_count + 4;
    if (w) *w = cw;
    if (h) *h = ch;
    *top = 2;
    *left = popup_left_1based(g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x, cw, host_cols);
}

void render_chooser(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int top, left, cw, ch;
    chooser_geom(host_rows, host_cols, &top, &left, &cw, &ch);
    int pos = *posp;

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;031;111;235m┌─ 新建 pane ", top, left);
    int used = 2 + 11;
    while (used < cw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        used++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    for (int i = 0; i < g_chooser_item_count; i++) {
        int r = top + 1 + i;
        char disp_name[64] = {0};
        format_name15_display(disp_name, sizeof(disp_name), g_chooser_items[i].name);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m  \x1b[038;2;210;153;034m[%d]\x1b[0m \x1b[038;2;230;237;243m%s\x1b[0m",
                        r, left, i + 1, disp_name);
        char chooser_tag[16];
        snprintf(chooser_tag, sizeof(chooser_tag), "[%d]", i + 1);
        int item_used = 1 + 2 + utf8_cols(chooser_tag, (int)strlen(chooser_tag)) + 1 +
                        utf8_cols(disp_name, (int)strlen(disp_name));
        while (item_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; item_used++; }
        pos += snprintf(out + pos, bs - pos, "\x1b[048;2;033;038;045m│\x1b[0m");
    }

    int about_r = top + 1 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m  \x1b[038;2;217;119;054;1m[A]\x1b[0m \x1b[038;2;217;119;054m关于 (About)\x1b[0m", about_r, left);
    int about_used = 1 + 2 + 3 + 1 + 12;
    while (about_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; about_used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[048;2;033;038;045m│\x1b[0m");

    int esc_r = top + 2 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m  \x1b[038;2;139;148;158mEsc 取消\x1b[0m", esc_r, left);
    int esc_used = 1 + 2 + 8;
    while (esc_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; esc_used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[048;2;033;038;045m│\x1b[0m");

    int bot_r = top + 3 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m└", bot_r, left);
    used = 1;
    while (used < cw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        used++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");
    *posp = pos;
}

void render_custom_cmd_box(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = popup_left_1based(ax, CMD_BOX_W, host_cols);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;031;111;235m┌─ 自定义命令行 ─────────────────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m" TB_BG " ", top + 1, left);
    render_scrollable_input(out, bs, &pos, g_mux.custom_cmd_buf, g_mux.custom_cmd_len, g_mux.custom_cmd_pos, CMD_BOX_W - 3, TB_BG, NULL);
    pos += snprintf(out + pos, bs - pos, "\x1b[048;2;033;038;045m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[038;2;139;148;158m", top + 2, left);
    const char *cmd_hint = "  [Enter=启动  Esc=取消]";
    int cmd_hint_cols = 1 + utf8_cols(cmd_hint, (int)strlen(cmd_hint));
    pos += snprintf(out + pos, bs - pos, "%s", cmd_hint);
    while (cmd_hint_cols < CMD_BOX_W - 1 && pos < bs - 8) {
        out[pos++] = ' ';
        cmd_hint_cols++;
    }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m└────────────────────────────────────┘\x1b[0m", top + 3, left);
    *posp = pos;
}

void render_rename_box(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = popup_left_1based(ax, RENAME_W, host_cols);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;031;111;235m┌─ 重命名标签 ───────────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m" TB_BG " ", top + 1, left);
    render_scrollable_input(out, bs, &pos, g_mux.rename_buf, g_mux.rename_len, g_mux.rename_pos, RENAME_W - 3, TB_BG, NULL);
    pos += snprintf(out + pos, bs - pos, "\x1b[048;2;033;038;045m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m└────────────────────────────┘\x1b[0m", top + 2, left);
    *posp = pos;
}

void render_ctx_menu(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = popup_left_1based(ax, CTX_W, host_cols);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;031;111;235m┌─ 标签操作 ───────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m  \x1b[038;2;210;153;034m[1]\x1b[0m \x1b[038;2;230;237;243m修改颜色        \x1b[0m\x1b[048;2;033;038;045m│\x1b[0m", top + 1, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m  \x1b[038;2;210;153;034m[2]\x1b[0m \x1b[038;2;230;237;243m重命名标签      \x1b[0m\x1b[048;2;033;038;045m│\x1b[0m", top + 2, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m└──────────────────────┘\x1b[0m", top + 3, left);
    *posp = pos;
}

/* ---------------------------------------------------------------------------
 * v1.8.9: 菜单项的「启动默认颜色」选择条
 * 第 0 格「默认」宽 6，其后 8 个色块每格宽 3，格子相连。渲染与命中同源。
 * ------------------------------------------------------------------------- */
int item_color_hit(int left, int col) {
    int off = col - left;
    if (off < 0 || off >= ITEM_COLOR_ROW_W) return -1;
    if (off < ITEM_COLOR_DEFAULT_W) return 0;
    return 1 + (off - ITEM_COLOR_DEFAULT_W) / ITEM_COLOR_SWATCH_W;
}

void render_item_color_row(char *out, int bs, int *posp, int row, int left, int color, int focused) {
    int pos = *posp;
    if (color < 0 || color > 8) color = 0;
    int hover = -1;
    if (g_mouse_y + 1 == row) hover = item_color_hit(left, g_mouse_x + 1);

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH", row, left);
    for (int i = 0; i <= 8; i++) {
        int sel = (i == color);
        int hot = (i == hover);
        const char *bg;
        const char *fg;
        if (i == 0) {
            bg = sel ? "\x1b[048;2;038;060;088m" : (hot ? "\x1b[048;2;033;038;045m" : "\x1b[048;2;022;027;034m");
            fg = sel ? "\x1b[038;2;255;255;255;1m" : "\x1b[038;2;139;148;158m";
            pos += snprintf(out + pos, bs - pos, "%s%s 默认 \x1b[0m", bg, fg);
            continue;
        }
        bg = (sel || hot) ? TAB_COLOR_BG[i] : TAB_COLOR_BG_DIM[i];
        fg = (sel || hot) ? "\x1b[038;2;013;017;023;1m" : "\x1b[038;2;139;148;158m";
        pos += snprintf(out + pos, bs - pos, "%s%s%c%d%c\x1b[0m", bg, fg,
                        sel ? '[' : ' ', i, sel ? ']' : ' ');
    }
    /* 焦点在这一行时给个箭头，键盘用户才知道 ←/→ 会作用到哪里。 */
    pos += snprintf(out + pos, bs - pos, "  %s%s\x1b[0m",
                    focused ? "\x1b[038;2;121;192;255;1m" : "\x1b[038;2;110;118;129m",
                    focused ? "左右键选颜色" : "            ");
    *posp = pos;
}

static void palette_hline(char *out, int bs, int *posp, int row, int left, int width,
                          const char *prefix, const char *suffix);

static void render_color_picker_cell(char *out, int bs, int *posp,
                                     const char *bg, const char *fg, char ch) {
    int pos = *posp;
    pos += snprintf(out + pos, bs - pos, "%s%s%c", bg, fg, ch);
    *posp = pos;
}

static void render_color_picker_row(char *out, int bs, int *posp, int row,
                                    int left, int base_color) {
    int pos = *posp;
    const char *panel_bg = "\x1b[048;2;033;038;045m";
    const char *normal_fg = "\x1b[038;2;013;017;023;1m";
    const char *hover_fg = "\x1b[038;2;255;255;255;1m";
    /* A swatch is a miniature tab: hovering it shows the "active tab" look
     * (full-strength colour, bold white label), everything else uses the
     * inactive-tab look (dimmed colour, muted grey label). */
    const char *idle_fg = "\x1b[038;2;139;148;158m";
    int mouse_row = row - 1; /* rendered row is ANSI 1-based */

    pos += snprintf(out + pos, bs - pos,
                    "\x1b[%d;%dH%s│", row, left, panel_bg);
    render_color_picker_cell(out, bs, &pos, panel_bg, normal_fg, ' ');

    for (int i = 0; i < 4; i++) {
        int color = base_color + i;
        int hovered = (g_mouse_y == mouse_row &&
                       g_mouse_x >= left + 1 + i * 4 &&
                       g_mouse_x < left + 5 + i * 4);
        const char *swatch_bg = hovered ? TAB_COLOR_BG[color] : TAB_COLOR_BG_DIM[color];
        for (int w = 0; w < 4; w++) {
            /* CP_SWATCH_W coloured cells then one panel-background gap, so the
             * swatches read as separate tabs instead of one long ribbon. */
            if (w >= CP_SWATCH_W) {
                render_color_picker_cell(out, bs, &pos, panel_bg, normal_fg, ' ');
                continue;
            }
            char ch = (w == 1) ? (char)('0' + color) : ' ';
            const char *fg = (w == 1) ? (hovered ? hover_fg : idle_fg) : normal_fg;
            render_color_picker_cell(out, bs, &pos, swatch_bg, fg, ch);
        }
    }

    /* Every remaining interior cell is explicitly panel background.  This
     * prevents the last swatch's background from leaking into the padding and
     * makes the visible row state unambiguous one cell at a time. */
    int interior_cols = 2 + 4 * 4;
    while (interior_cols < CP_W - 1) {
        render_color_picker_cell(out, bs, &pos, panel_bg, normal_fg, ' ');
        interior_cols++;
    }
    pos += snprintf(out + pos, bs - pos, "%s│\x1b[0m", panel_bg);
    *posp = pos;
}

void render_color_picker(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = popup_left_1based(ax, CP_W, host_cols);
    /* The frame is drawn to CP_W instead of a hard-coded ruler so the panel
     * always hugs the swatches; it used to be padded out with dead space. */
    const char *hdr = "┌─ 选择颜色 ";
    int cols = utf8_cols(hdr, (int)strlen(hdr));
    pos += snprintf(out + pos, bs - pos,
                    "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;031;111;235m%s",
                    top, left, hdr);
    while (cols < CP_W - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80'; cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");
    render_color_picker_row(out, bs, &pos, top + 1, left, 1);
    render_color_picker_row(out, bs, &pos, top + 2, left, 5);
    palette_hline(out, bs, &pos, top + 3, left, CP_W, "└", "┘");
    *posp = pos;
}

void presets_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h, int *max_nw, int *max_cw) {
    (void)host_rows;
    int mnw = 0, mcw = 0;
    for (int i = 0; i < g_preset_count; i++) {
        int nw = utf8_cols(g_presets[i].name, (int)strlen(g_presets[i].name));
        int cw = utf8_cols(g_presets[i].cmd, (int)strlen(g_presets[i].cmd));
        if (nw > mnw) mnw = nw;
        if (cw > mcw) mcw = cw;
    }
    if (mnw < 6) mnw = 6;
    if (mcw < 8) mcw = 8;
    const char *hdr_full = "┌─ 常用命令行预设 (按数字/回车选择) ┐";
    int min_hdr = utf8_cols(hdr_full, (int)strlen(hdr_full));
    int pw = 1 + 2 + 4 + mnw + 1 + mcw + 2;
    if (pw < min_hdr + 2) pw = min_hdr + 2;
    if (pw > host_cols) pw = host_cols;
    int ph = g_preset_count + 3;
    if (w) *w = pw;
    if (h) *h = ph;
    if (max_nw) *max_nw = mnw;
    if (max_cw) *max_cw = mcw;
    *top = 3;
    /* Unlike the tab bar, this popup is emitted with an ANSI cursor
     * position, so its left edge is 1-based. */
    *left = (host_cols - pw) / 2 + 1;
    if (*left < 1) *left = 1;
}

void render_settings_presets(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int top, left, pw, ph, mnw, mcw;
    presets_geom(host_rows, host_cols, &top, &left, &pw, &ph, &mnw, &mcw);
    int pos = *posp;

    const char *hdr_text = "┌─ 常用命令行预设 (按数字/回车选择) ";
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;031;136;061;1m%s", top, left, hdr_text);
    int cols = utf8_cols(hdr_text, (int)strlen(hdr_text));
    while (cols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    for (int i = 0; i < g_preset_count; i++) {
        int r = top + 1 + i;
        int row_hover = (g_mouse_y == r - 1 && g_mouse_x >= left - 1 && g_mouse_x < left - 1 + pw);
        int is_sel = (i == g_preset_sel);
        const char *bg = (row_hover || is_sel) ? "\x1b[048;2;045;055;072m" : "\x1b[048;2;022;027;034m";
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m%s  \x1b[038;2;210;153;034m[%d]\x1b[0m%s \x1b[038;2;230;237;243;1m",
                        r, left, bg, i + 1, bg);
        char preset_tag[16];
        snprintf(preset_tag, sizeof(preset_tag), "[%d]", i + 1);
        cols = 1 + 2 + utf8_cols(preset_tag, (int)strlen(preset_tag));
        append_padded_utf8(out, bs, &pos, &cols, g_presets[i].name, mnw);
        pos += snprintf(out + pos, bs - pos, "%s \x1b[038;2;139;148;158m", bg);
        cols += 1;
        append_padded_utf8(out, bs, &pos, &cols, g_presets[i].cmd, mcw);
        pos += snprintf(out + pos, bs - pos, "%s", bg);
        pad_to_right_border(out, bs, &pos, &cols, pw);
    }

    int esc_r = top + 1 + g_preset_count;
    int h_esc = (g_mouse_y == esc_r - 1 && g_mouse_x >= left - 1 + 2 && g_mouse_x <= left - 1 + 14);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[048;2;022;027;034m  ", esc_r, left);
    cols = 1 + 2;
    if (h_esc)
        pos += snprintf(out + pos, bs - pos, "\x1b[048;2;217;119;054m\x1b[038;2;255;255;255;1m [Esc] 取消 \x1b[0m\x1b[048;2;022;027;034m");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[048;2;033;038;045m\x1b[038;2;139;148;158m [Esc] 取消 \x1b[0m\x1b[048;2;022;027;034m");
    cols += 12;
    pad_to_right_border(out, bs, &pos, &cols, pw);

    int bot_r = top + 2 + g_preset_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m└", bot_r, left);
    cols = 1;
    while (cols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");
    *posp = pos;
}

/* ---------------------------------------------------------------------------
 * 设置页新增的三个分类：外观 / 键位 / 行为
 * 渲染与鼠标命中共用同一套几何函数，避免两边写死行号后走偏。
 * ------------------------------------------------------------------------- */

void settings_sidebar_extra_rows(int *appearance_r, int *keys_r, int *behavior_r) {
    int base = 8 + g_chooser_item_count;   /* 快速预设库所在行 */
    if (appearance_r) *appearance_r = base + 1;
    if (keys_r) *keys_r = base + 2;
    if (behavior_r) *behavior_r = base + 3;
}

int settings_theme_row(int idx) { return SETTINGS_THEME_ROW0 + idx; }

int settings_role_row(int role) { return SETTINGS_ROLE_ROW0 + (role % SETTINGS_ROLE_ROWS); }

int settings_role_col(int main_left, int role) {
    return role < SETTINGS_ROLE_ROWS ? main_left : main_left + SETTINGS_ROLE_COL_W;
}

int settings_keys_rows(void) { return 1 + keymap_action_count(); }

int settings_keys_visible(int host_rows) {
    int vis = host_rows - SETTINGS_KEYS_ROW0 - 1;
    if (vis < 3) vis = 3;
    if (vis > settings_keys_rows()) vis = settings_keys_rows();
    return vis;
}

/* 让选中行始终留在可视窗口里 */
static void settings_keys_clamp_scroll(int host_rows) {
    int vis = settings_keys_visible(host_rows);
    int total = settings_keys_rows();
    if (g_settings_keys_sel < 0) g_settings_keys_sel = 0;
    if (g_settings_keys_sel >= total) g_settings_keys_sel = total - 1;
    if (g_settings_keys_scroll > g_settings_keys_sel) g_settings_keys_scroll = g_settings_keys_sel;
    if (g_settings_keys_scroll < g_settings_keys_sel - vis + 1) g_settings_keys_scroll = g_settings_keys_sel - vis + 1;
    if (g_settings_keys_scroll > total - vis) g_settings_keys_scroll = total - vis;
    if (g_settings_keys_scroll < 0) g_settings_keys_scroll = 0;
}

int settings_keys_row_at(int host_rows, int entry) {
    settings_keys_clamp_scroll(host_rows);
    int rel = entry - g_settings_keys_scroll;
    if (rel < 0 || rel >= settings_keys_visible(host_rows)) return -1;
    return SETTINGS_KEYS_ROW0 + rel;
}

int settings_keys_entry_at(int host_rows, int row) {
    settings_keys_clamp_scroll(host_rows);
    int rel = row - SETTINGS_KEYS_ROW0;
    if (rel < 0 || rel >= settings_keys_visible(host_rows)) return -1;
    int entry = g_settings_keys_scroll + rel;
    return entry < settings_keys_rows() ? entry : -1;
}

/* 一个 2 格宽的实心色块，用真实 RGB 输出（不参与主题重映射） */
static void append_swatch(char *out, int bs, int *posp, int r, int g, int b) {
    *posp += snprintf(out + *posp, bs - *posp, "\x1b[48;2;%d;%d;%dm  \x1b[0m", r, g, b);
}

static const char *settings_row_style(int selected, int hovered) {
    if (selected) return hovered ? "\x1b[048;2;048;075;110m\x1b[038;2;255;255;255;1m"
                                 : "\x1b[048;2;038;060;088m\x1b[038;2;121;192;255;1m";
    return hovered ? "\x1b[048;2;033;038;045m\x1b[038;2;255;255;255;1m" : "\x1b[038;2;230;237;243m";
}

static void render_settings_appearance(char *out, int bs, int *posp, int host_rows, int host_cols, int main_left) {
    int pos = *posp;
    (void)host_cols;
    pos += snprintf(out + pos, bs - pos,
        "\x1b[3;%dH\x1b[038;2;121;192;255;1m■ 配色主题 (Theme)\x1b[0m", main_left);
    pos += snprintf(out + pos, bs - pos,
        "\x1b[4;%dH\x1b[038;2;139;148;158m↑/↓ 选择，Enter/Space 立即应用并写入 termux.ini：\x1b[0m", main_left);

    for (int i = 0; i < theme_count(); i++) {
        int row = settings_theme_row(i);
        if (row > host_rows) break;
        int active = (i == theme_index());
        int selected = (g_settings_theme_sel == i);
        int hovered = (g_mouse_y == row - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 40);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s %s %-14s \x1b[0m",
                        row, main_left, settings_row_style(selected, hovered),
                        active ? "[●]" : "[○]", theme_name_at(i));
        /* 主题预览：强调色 / 绿 / 橙 / 紫 四个色块 */
        const ThemeDef *def = &g_builtin_themes[i];
        int preview[4] = {TH_ACCENT, TH_GREEN, TH_ORANGE, TH_PURPLE};
        for (int k = 0; k < 4; k++) {
            ThemeRGB c = def->role[preview[k]];
            append_swatch(out, bs, &pos, c.r, c.g, c.b);
        }
    }

    int title_r = SETTINGS_ROLE_ROW0 - 2;
    pos += snprintf(out + pos, bs - pos,
        "\x1b[%d;%dH\x1b[038;2;121;192;255;1m■ 语义颜色 (Palette)\x1b[038;2;139;148;158m   Enter 编辑十六进制, R 复位当前项, Ctrl+R 清除全部\x1b[0m",
        title_r, main_left);
    pos += snprintf(out + pos, bs - pos,
        "\x1b[%d;%dH\x1b[038;2;139;148;158m界面里所有派生色都由这 16 个角色混合得出，改一个即可成套生效。\x1b[0m",
        title_r + 1, main_left);

    for (int role = 0; role < TH_ROLE_COUNT; role++) {
        int row = settings_role_row(role);
        int col = settings_role_col(main_left, role);
        if (row > host_rows) continue;
        int selected = (g_settings_theme_sel == theme_count() + role);
        int hovered = (g_mouse_y == row - 1 && g_mouse_x >= col - 1 && g_mouse_x < col + SETTINGS_ROLE_COL_W - 2);
        int r, g, b;
        theme_role_rgb(role, &r, &g, &b);

        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s %-15s\x1b[0m ",
                        row, col, settings_row_style(selected, hovered), theme_role_name(role));
        append_swatch(out, bs, &pos, r, g, b);

        if (g_hex_edit_active && g_hex_edit_role == role) {
            char shown[16];
            snprintf(shown, sizeof(shown), "#%s", g_hex_edit_buf);
            pos += snprintf(out + pos, bs - pos, " \x1b[048;2;038;060;088m\x1b[038;2;255;255;255;1m%-8s\x1b[0m", shown);
        } else {
            pos += snprintf(out + pos, bs - pos, " \x1b[038;2;139;148;158m#%02x%02x%02x\x1b[0m%s",
                            r, g, b, theme_role_is_overridden(role) ? "\x1b[038;2;210;153;034m*\x1b[0m" : " ");
        }
    }

    int hint_r = SETTINGS_ROLE_ROW0 + SETTINGS_ROLE_ROWS + 1;
    if (hint_r > host_rows) hint_r = host_rows;
    if (g_hex_edit_active) {
        pos += snprintf(out + pos, bs - pos,
            "\x1b[%d;%dH\x1b[038;2;210;153;034;1m正在编辑 %s：输入 6 位十六进制，Enter 确认，Esc 取消\x1b[0m",
            hint_r, main_left, theme_role_name(g_hex_edit_role));
    } else {
        pos += snprintf(out + pos, bs - pos,
            "\x1b[%d;%dH\x1b[038;2;139;148;158m提示: ↑/↓ 选择, ←/→ 换列, Enter 应用/编辑, R 复位, Ctrl+R 清除全部自定义, Ctrl+S 保存, Esc 返回\x1b[0m",
            hint_r, main_left);
    }
    *posp = pos;
}

static void render_settings_keys(char *out, int bs, int *posp, int host_rows, int host_cols, int main_left) {
    int pos = *posp;
    (void)host_cols;
    pos += snprintf(out + pos, bs - pos,
        "\x1b[3;%dH\x1b[038;2;121;192;255;1m■ 键位 (Key Bindings)\x1b[038;2;139;148;158m   ↑/↓ 选择, Enter 录制新键, R 复位, Ctrl+R 全部复位\x1b[0m",
        main_left);
    pos += snprintf(out + pos, bs - pos,
        "\x1b[4;%dH\x1b[038;2;139;148;158m所有改动写入 termux.ini 的 [general] prefix 与 [keys] 段；帮助页会同步显示。\x1b[0m",
        main_left);
    pos += snprintf(out + pos, bs - pos,
        "\x1b[5;%dH\x1b[038;2;121;192;255;1m   动作名             说明               当前键位      前缀   操作\x1b[0m", main_left);

    settings_keys_clamp_scroll(host_rows);
    int total = settings_keys_rows();
    for (int entry = 0; entry < total; entry++) {
        int row = settings_keys_row_at(host_rows, entry);
        if (row < 0) continue;
        int selected = (g_settings_keys_sel == entry);
        int hovered = (g_mouse_y == row - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 70);
        int capturing = (g_key_capture_active && selected);

        char name[24], label[24], combo[48];
        int custom = 0;
        if (entry == 0) {
            snprintf(name, sizeof(name), "prefix");
            snprintf(label, sizeof(label), "前缀键");
            /* 界面显示 Ctrl+B，而不是 ini 里的 C-b 写法。 */
            keymap_prefix_describe(combo, sizeof(combo));
            custom = !keymap_prefix_is_default();
        } else {
            int action = keymap_action_at(entry - 1);
            snprintf(name, sizeof(name), "%s", keymap_action_name(action));
            snprintf(label, sizeof(label), "%s", keymap_action_label(action));
            keymap_describe(action, combo, sizeof(combo));
            if (!combo[0]) snprintf(combo, sizeof(combo), "(未绑定)");
            custom = keymap_action_is_overridden(action);
        }
        if (capturing) snprintf(combo, sizeof(combo), "按下新键…");

        int cols = 0;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s %s ",
                        row, main_left, settings_row_style(selected, hovered), selected ? "▶" : " ");
        cols += 3;
        append_padded_utf8(out, bs, &pos, &cols, name, 19);
        append_padded_utf8(out, bs, &pos, &cols, label, 19);
        pos += snprintf(out + pos, bs - pos, "%s", capturing ? "\x1b[038;2;210;153;034;1m" : "");
        append_padded_utf8(out, bs, &pos, &cols, combo, 16);
        pos += snprintf(out + pos, bs - pos, "\x1b[0m");

        /* 是否需要先按前缀键，可以按 P 或点这里切换。 */
        if (entry > 0) {
            int action = keymap_action_at(entry - 1);
            int uses_prefix = keymap_action_uses_prefix(action);
            int h_prefix = (hovered && g_mouse_x >= main_left + SETTINGS_KEYS_PREFIX_COL - 1 &&
                            g_mouse_x < main_left + SETTINGS_KEYS_PREFIX_COL + 5);
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s%s\x1b[0m",
                            row, main_left + SETTINGS_KEYS_PREFIX_COL,
                            h_prefix ? "\x1b[048;2;137;087;229m\x1b[038;2;255;255;255;1m"
                                     : (uses_prefix ? "\x1b[038;2;139;148;158m" : "\x1b[038;2;063;185;080;1m"),
                            uses_prefix ? "[前缀]" : "[直接]");
        }

        int h_edit = (hovered && g_mouse_x >= main_left + SETTINGS_KEYS_EDIT_COL - 1 &&
                      g_mouse_x < main_left + SETTINGS_KEYS_RESET_COL - 1);
        int h_reset = (hovered && g_mouse_x >= main_left + SETTINGS_KEYS_RESET_COL - 1 &&
                       g_mouse_x < main_left + SETTINGS_KEYS_RESET_COL + 5);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s[改]\x1b[0m",
                        row, main_left + SETTINGS_KEYS_EDIT_COL,
                        h_edit ? "\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m" : "\x1b[038;2;121;192;255m");
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s[复位]\x1b[0m",
                        row, main_left + SETTINGS_KEYS_RESET_COL,
                        h_reset ? "\x1b[048;2;248;081;073m\x1b[038;2;255;255;255;1m"
                                : (custom ? "\x1b[038;2;248;081;073m" : "\x1b[038;2;048;054;061m"));
    }

    int hint_r = host_rows;
    pos += snprintf(out + pos, bs - pos,
        "\x1b[%d;%dH\x1b[038;2;139;148;158m%s\x1b[0m", hint_r, main_left,
        g_key_capture_active ? "请按下新的组合键（Esc 取消）；修饰键单独按无效。"
                             : "提示: Enter/[改] 录制, P/[前缀] 切换是否需要前缀, R/[复位] 默认, Esc 返回");
    *posp = pos;
}

static void render_settings_behavior(char *out, int bs, int *posp, int host_rows, int host_cols, int main_left) {
    int pos = *posp;
    (void)host_cols;
    pos += snprintf(out + pos, bs - pos,
        "\x1b[3;%dH\x1b[038;2;121;192;255;1m■ 行为 (Behavior)\x1b[0m", main_left);
    pos += snprintf(out + pos, bs - pos,
        "\x1b[4;%dH\x1b[038;2;139;148;158m↑/↓ 选择，Space/Enter 切换开关，←/→ 调整数值（scrollback 步进 1000）：\x1b[0m", main_left);

    struct { const char *key; const char *desc; int value; } toggles[SETTINGS_BEHAVIOR_TOGGLES] = {
        {"mouse",           "鼠标支持（标签点击 / 拖选 / 滚轮）", g_mouse_enabled},
        {"copy_on_select",  "拖选松开后自动复制到剪贴板",         g_copy_on_select},
        {"confirm_on_exit", "退出 termux 前二次确认",             g_confirm_on_exit},
        {"search_case_sensitive", "搜索锁定大小写（区分大小写）",  g_search_case_sensitive},
    };
    for (int i = 0; i < SETTINGS_BEHAVIOR_TOGGLES; i++) {
        int row = SETTINGS_BEHAVIOR_ROW0 + i;
        if (row > host_rows) break;
        int selected = (g_settings_behavior_sel == i);
        int hovered = (g_mouse_y == row - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 60);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s %s %-22s \x1b[0m%s%s\x1b[0m",
                        row, main_left, settings_row_style(selected, hovered),
                        toggles[i].value ? "[x]" : "[ ]", toggles[i].key,
                        settings_row_style(selected, hovered), toggles[i].desc);
    }

    int sb_row = SETTINGS_BEHAVIOR_ROW0 + SETTINGS_BEHAVIOR_TOGGLES;
    if (sb_row <= host_rows) {
        int selected = (g_settings_behavior_sel == SETTINGS_BEHAVIOR_TOGGLES);
        int hovered = (g_mouse_y == sb_row - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 60);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s     %-22s \x1b[0m",
                        sb_row, main_left, settings_row_style(selected, hovered), "scrollback");
        int h_minus = (hovered && g_mouse_x >= main_left + SETTINGS_SB_MINUS_COL - 1 &&
                       g_mouse_x < main_left + SETTINGS_SB_MINUS_COL + 2);
        int h_plus = (hovered && g_mouse_x >= main_left + SETTINGS_SB_PLUS_COL - 1 &&
                      g_mouse_x < main_left + SETTINGS_SB_PLUS_COL + 2);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s[-]\x1b[0m",
                        sb_row, main_left + SETTINGS_SB_MINUS_COL,
                        h_minus ? "\x1b[048;2;217;119;054m\x1b[038;2;013;017;023;1m" : "\x1b[038;2;217;119;054m");
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;230;237;243;1m%6d\x1b[0m 行",
                        sb_row, main_left + SETTINGS_SB_MINUS_COL + 4, g_scrollback_lines);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s[+]\x1b[0m",
                        sb_row, main_left + SETTINGS_SB_PLUS_COL,
                        h_plus ? "\x1b[048;2;063;185;080m\x1b[038;2;013;017;023;1m" : "\x1b[038;2;063;185;080m");
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;139;148;158m(对之后新建的 pane 生效)\x1b[0m",
                        sb_row, main_left + SETTINGS_SB_PLUS_COL + 4);
    }

    int hint_r = SETTINGS_BEHAVIOR_ROW0 + SETTINGS_BEHAVIOR_TOGGLES + 3;
    if (hint_r > host_rows) hint_r = host_rows;
    pos += snprintf(out + pos, bs - pos,
        "\x1b[%d;%dH\x1b[038;2;139;148;158m提示: Space/Enter 切换, ←/→ 调整 scrollback, Ctrl+S 保存, Esc 返回\x1b[0m",
        hint_r, main_left);
    *posp = pos;
}

void render_settings_panel(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int pos = *posp;
    int sb_w = SETTINGS_SIDEBAR_W;
    if (sb_w > host_cols / 2) sb_w = host_cols / 2;
    if (sb_w < 15) sb_w = 15;
    if (sb_w > host_cols) sb_w = host_cols;
    if (sb_w < 1) sb_w = 1;

    for (int y = 0; y < host_rows; y++) {
        int r = y + 2;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[0m\x1b[K", r);
    }

    pos += snprintf(out + pos, bs - pos, "\x1b[2;1H\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m  *  termux - 设置面板 (Settings Panel)");
    int hdr_used = utf8_cols("  *  termux - 设置面板 (Settings Panel)", (int)strlen("  *  termux - 设置面板 (Settings Panel)"));
    while (hdr_used < host_cols && pos < bs - 8) { out[pos++] = ' '; hdr_used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m");

    for (int y = 1; y < host_rows; y++) {
        int r = y + 2;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;048;054;061m│\x1b[0m", r, sb_w);
    }

    pos += snprintf(out + pos, bs - pos, "\x1b[3;1H\x1b[038;2;121;192;255;1m  导航选项\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[4;1H\x1b[038;2;048;054;061m─────────────────────\x1b[0m");

    int is_sel0 = (g_settings_nav == 0);
    int h_start = (g_mouse_y == 4 && g_mouse_x >= 0 && g_mouse_x < sb_w);
    const char *start_style = is_sel0 ? (h_start ? "\x1b[048;2;048;075;110m\x1b[038;2;255;255;255;1m" : "\x1b[048;2;038;060;088m\x1b[038;2;121;192;255;1m")
                                      : (h_start ? "\x1b[048;2;033;038;045m\x1b[038;2;255;255;255;1m" : "\x1b[038;2;230;237;243m");
    pos += snprintf(out + pos, bs - pos, "\x1b[5;1H%s  %s 启动 (Startup)  \x1b[0m", start_style, (is_sel0 ? "▶" : " "));

    pos += snprintf(out + pos, bs - pos, "\x1b[6;1H\x1b[038;2;048;054;061m┈┈ 菜单项配置 ┈┈┈┈┈┈─\x1b[0m");

    for (int i = 0; i < g_chooser_item_count; i++) {
        int r = 7 + i;
        if (r > host_rows - 3) break;
        int is_sel = (g_settings_nav == i + 1);
        int h_item = (g_mouse_y == r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
        const char *item_style = is_sel ? (h_item ? "\x1b[048;2;048;075;110m\x1b[038;2;255;255;255;1m" : "\x1b[048;2;038;060;088m\x1b[038;2;121;192;255;1m")
                                        : (h_item ? "\x1b[048;2;033;038;045m\x1b[038;2;255;255;255;1m" : "\x1b[038;2;230;237;243m");
        char dname[32] = {0};
        format_name_display(dname, sizeof(dname), g_chooser_items[i].name);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s  %s [%d] %-10s\x1b[0m", r, item_style, is_sel ? "▶" : " ", i + 1, dname);
    }

    int add_r = 7 + g_chooser_item_count;
    if (add_r <= host_rows - 2) {
        int h_add = (g_mouse_y == add_r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s  [+] 添加新条目    \x1b[0m", add_r, h_add ? "\x1b[048;2;063;185;080m\x1b[038;2;013;017;023;1m" : "\x1b[038;2;063;185;080;1m");
    }

    int pre_r = 8 + g_chooser_item_count;
    if (pre_r <= host_rows - 2) {
        int h_pre = (g_mouse_y == pre_r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s  [P] 快速预设库    \x1b[0m", pre_r, h_pre ? "\x1b[048;2;031;136;061m\x1b[038;2;255;255;255;1m" : "\x1b[038;2;031;136;061;1m");
    }

    int app_r, keys_r, beh_r;
    settings_sidebar_extra_rows(&app_r, &keys_r, &beh_r);
    const struct { int row; const char *label; int nav; } extra_nav[3] = {
        {app_r,  "  [A] 外观 / 主题   ", SETTINGS_NAV_APPEARANCE},
        {keys_r, "  [K] 键位设置      ", SETTINGS_NAV_KEYS},
        {beh_r,  "  [B] 行为开关      ", SETTINGS_NAV_BEHAVIOR},
    };
    for (int i = 0; i < 3; i++) {
        int row = extra_nav[i].row;
        if (row > host_rows - 1) break;
        int is_sel = (g_settings_nav == extra_nav[i].nav);
        int hovered = (g_mouse_y == row - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s%s\x1b[0m",
                        row, settings_row_style(is_sel, hovered), extra_nav[i].label);
    }

    int save_r = host_rows;
    int h_save_btn = (g_mouse_y == save_r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s [Ctrl+S] 保存配置  \x1b[0m", save_r, h_save_btn ? "\x1b[048;2;063;185;080m\x1b[038;2;013;017;023;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;063;185;080;1m");

    int main_left = sb_w + 3;
    int right_max_w = host_cols - main_left - 2;
    if (right_max_w < 10) right_max_w = 10;

    if (g_settings_nav == SETTINGS_NAV_APPEARANCE) {
        render_settings_appearance(out, bs, &pos, host_rows, host_cols, main_left);
    } else if (g_settings_nav == SETTINGS_NAV_KEYS) {
        render_settings_keys(out, bs, &pos, host_rows, host_cols, main_left);
    } else if (g_settings_nav == SETTINGS_NAV_BEHAVIOR) {
        render_settings_behavior(out, bs, &pos, host_rows, host_cols, main_left);
    } else if (g_settings_nav == 0) {
        pos += snprintf(out + pos, bs - pos, "\x1b[3;%dH\x1b[038;2;121;192;255;1m■ 默认启动项设置 (Default Startup Item)\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[4;%dH\x1b[038;2;139;148;158m选择每次打开 termux 窗口时默认显示的界面 (按 ←/→/Space/T/H 切换)：\x1b[0m", main_left);

        int opt0_hover = (g_mouse_y == 4 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 25);
        int opt1_hover = (g_mouse_y == 4 && g_mouse_x >= main_left + 28 && g_mouse_x < main_left + 50);

        const char *opt0_style = (g_default_startup == 0) ? (opt0_hover ? "\x1b[048;2;140;205;255m\x1b[038;2;013;017;023;1m" : "\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m")
                                                          : (opt0_hover ? "\x1b[048;2;045;055;072m\x1b[038;2;255;255;255;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;230;237;243m");
        const char *opt1_style = (g_default_startup == 1) ? (opt1_hover ? "\x1b[048;2;140;205;255m\x1b[038;2;013;017;023;1m" : "\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m")
                                                          : (opt1_hover ? "\x1b[048;2;045;055;072m\x1b[038;2;255;255;255;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;230;237;243m");

        pos += snprintf(out + pos, bs - pos, "\x1b[5;%dH%s [●] 默认终端 (Terminal) \x1b[0m   %s [○] 内置帮助 (Help) \x1b[0m",
                        main_left, opt0_style, opt1_style);

        pos += snprintf(out + pos, bs - pos, "\x1b[7;%dH\x1b[038;2;121;192;255;1m■ [+] 新建菜单项顺序与管理 ([+] Menu Order)\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[8;%dH\x1b[038;2;139;148;158m按 ↑/↓ 选择行，Ctrl+↑/↓ 调顺序，Enter/[改] 编辑，X/[删] 移除：\x1b[0m", main_left);

        pos += snprintf(out + pos, bs - pos, "\x1b[9;%dH\x1b[038;2;121;192;255;1m   序号  显示名称        启动命令行                       操作\x1b[0m", main_left);

        for (int i = 0; i < g_chooser_item_count; i++) {
            int r = 10 + i;
            if (r > host_rows - 2) break;
            int row_hover = (g_mouse_y == r - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < host_cols);
            int row_focus = (i == g_settings_table_sel);
            int h_up = (row_hover && g_mouse_x >= main_left + 52 && g_mouse_x <= main_left + 54);
            int h_dn = (row_hover && g_mouse_x >= main_left + 55 && g_mouse_x <= main_left + 57);
            int h_ed = (row_hover && g_mouse_x >= main_left + 58 && g_mouse_x <= main_left + 61);
            int h_del = (row_hover && g_mouse_x >= main_left + 62 && g_mouse_x <= main_left + 65);

            char dname[32] = {0}; format_name_display(dname, sizeof(dname), g_chooser_items[i].name);
            char dcmd[64] = {0}; format_cmd_display(dcmd, sizeof(dcmd), g_chooser_items[i].cmd);

            const char *row_bg = (row_focus && !h_up && !h_dn && !h_ed && !h_del) ? "\x1b[048;2;038;050;068m" :
                                 ((row_hover && !h_up && !h_dn && !h_ed && !h_del) ? "\x1b[048;2;027;033;044m" : "");

            char row_tag[16];
            snprintf(row_tag, sizeof(row_tag), "[%d]", i + 1);
            int row_cols = 0;
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s %s\x1b[038;2;210;153;034m%s\x1b[0m%s  ",
                            r, main_left, row_bg, (row_focus ? "▶" : " "), row_tag, row_bg);
            row_cols = 2 + utf8_cols(row_tag, (int)strlen(row_tag)) + 2;

            /* %-Ns pads bytes, not terminal columns.  Build both fixed
             * columns with the same UTF-8 display-width helper used by the
             * renderer's other tables so CJK names cannot move the buttons. */
            pos += snprintf(out + pos, bs - pos, "\x1b[038;2;230;237;243;1m");
            append_padded_utf8(out, bs, &pos, &row_cols, dname, 12);
            pos += snprintf(out + pos, bs - pos, "\x1b[0m%s  ", row_bg);
            row_cols += 2;
            pos += snprintf(out + pos, bs - pos, "\x1b[038;2;139;148;158m");
            append_padded_utf8(out, bs, &pos, &row_cols, dcmd, 30);
            pos += snprintf(out + pos, bs - pos, "\x1b[0m%s  ", row_bg);
            row_cols += 2;

            pos += snprintf(out + pos, bs - pos, "%s[↑]\x1b[0m", h_up ? "\x1b[048;2;063;185;080m\x1b[038;2;013;017;023;1m" : "\x1b[038;2;063;185;080m");
            pos += snprintf(out + pos, bs - pos, "%s[↓]\x1b[0m", h_dn ? "\x1b[048;2;217;119;054m\x1b[038;2;013;017;023;1m" : "\x1b[038;2;217;119;054m");
            pos += snprintf(out + pos, bs - pos, "%s[改]\x1b[0m", h_ed ? "\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m" : "\x1b[038;2;121;192;255m");
            pos += snprintf(out + pos, bs - pos, "%s[删]\x1b[0m", h_del ? "\x1b[048;2;248;081;073m\x1b[038;2;255;255;255;1m" : "\x1b[038;2;248;081;073m");
        }

        int btn_r = 10 + g_chooser_item_count + 1;
        if (btn_r <= host_rows) {
            int h_add = (g_mouse_y == btn_r - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 13);
            int h_pre = (g_mouse_y == btn_r - 1 && g_mouse_x >= main_left + 15 && g_mouse_x < main_left + 29);

            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH", btn_r, main_left);
            pos += snprintf(out + pos, bs - pos, "%s [+] 添加条目 \x1b[0m  ", h_add ? "\x1b[048;2;063;185;080m\x1b[038;2;013;017;023;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;063;185;080;1m");
            pos += snprintf(out + pos, bs - pos, "%s [P] 快速预设 \x1b[0m", h_pre ? "\x1b[048;2;031;136;061m\x1b[038;2;255;255;255;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;031;136;061;1m");
        }

        int hint_r = btn_r + 2 <= host_rows ? btn_r + 2 : host_rows;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;139;148;158m提示: ↑/↓ 选择, Ctrl+↑/↓ 调序, Enter 编辑, X 删除, + 新建, P 预设, Ctrl+S 保存, Esc 退出\x1b[0m", hint_r, main_left);
    } else {
        int item_idx = g_settings_nav - 1;
        pos += snprintf(out + pos, bs - pos, "\x1b[3;%dH\x1b[038;2;121;192;255;1m■ 菜单项详细配置: [%d] %s\x1b[0m",
                        main_left, item_idx + 1, g_chooser_items[item_idx].name);

        int input_w = right_max_w - 4;
        if (input_w > 50) input_w = 50;
        if (input_w < 20) input_w = 20;

        int f0_sel = (g_settings_field == 0);
        int f0_hover = (g_mouse_y == 5 && g_mouse_x >= main_left - 1 && g_mouse_x <= main_left + input_w + 2);
        const char *f0_bg = f0_sel ? "\x1b[048;2;038;060;088m" : (f0_hover ? "\x1b[048;2;033;038;045m" : "\x1b[048;2;022;027;034m");
        pos += snprintf(out + pos, bs - pos, "\x1b[5;%dH\x1b[038;2;230;237;243;1m1. 显示名称 (Display Name):\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[6;%dH\x1b[048;2;033;038;045m│\x1b[0m%s ", main_left, f0_bg);
        render_scrollable_input(out, bs, &pos, g_edit_name, g_edit_name_len, g_edit_name_pos, input_w, f0_bg, NULL);
        pos += snprintf(out + pos, bs - pos, "%s \x1b[0m\x1b[048;2;033;038;045m│\x1b[0m", f0_bg);

        int f1_sel = (g_settings_field == 1);
        int f1_hover = (g_mouse_y == 8 && g_mouse_x >= main_left - 1 && g_mouse_x <= main_left + input_w + 2);
        const char *f1_bg = f1_sel ? "\x1b[048;2;038;060;088m" : (f1_hover ? "\x1b[048;2;033;038;045m" : "\x1b[048;2;022;027;034m");
        pos += snprintf(out + pos, bs - pos, "\x1b[8;%dH\x1b[038;2;230;237;243;1m2. 启动命令行 (Command Line):\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[9;%dH\x1b[048;2;033;038;045m│\x1b[0m%s ", main_left, f1_bg);
        render_scrollable_input(out, bs, &pos, g_edit_cmd, g_edit_cmd_len, g_edit_cmd_pos, input_w, f1_bg, NULL);
        pos += snprintf(out + pos, bs - pos, "%s \x1b[0m\x1b[048;2;033;038;045m│\x1b[0m", f1_bg);

        int f2_sel = (g_settings_field == 2);
        int f2_hover = (g_mouse_y == 11 && g_mouse_x >= main_left - 1 && g_mouse_x <= main_left + input_w + 2);
        const char *f2_bg = f2_sel ? "\x1b[048;2;038;060;088m" : (f2_hover ? "\x1b[048;2;033;038;045m" : "\x1b[048;2;022;027;034m");
        pos += snprintf(out + pos, bs - pos, "\x1b[11;%dH\x1b[038;2;230;237;243;1m3. 启动目录 (Working Directory) \x1b[038;2;139;148;158m[留空为当前目录，支持 %%USERPROFILE%%]:\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[12;%dH\x1b[048;2;033;038;045m│\x1b[0m%s ", main_left, f2_bg);
        render_scrollable_input(out, bs, &pos, g_edit_dir, g_edit_dir_len, g_edit_dir_pos, input_w, f2_bg, NULL);
        pos += snprintf(out + pos, bs - pos, "%s \x1b[0m\x1b[048;2;033;038;045m│\x1b[0m", f2_bg);

        /* v1.8.9: 第 4 个字段 —— 这个菜单项启动出来的标签页默认用什么颜色。 */
        int f3_sel = (g_settings_field == 3);
        pos += snprintf(out + pos, bs - pos,
                        "\x1b[14;%dH\x1b[038;2;230;237;243;1m4. 启动默认颜色 (Tab Color) "
                        "\x1b[038;2;139;148;158m[用此项新建标签页时的颜色，默认=蓝]:\x1b[0m", main_left);
        render_item_color_row(out, bs, &pos, 15, main_left, g_edit_color, f3_sel);

        int act_r = 17;
        int h_apply = (g_mouse_y == act_r - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 17);
        int h_imp = (g_mouse_y == act_r - 1 && g_mouse_x >= main_left + 19 && g_mouse_x < main_left + 35);
        int h_del = (g_mouse_y == act_r - 1 && g_mouse_x >= main_left + 37 && g_mouse_x < main_left + 49);

        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH", act_r, main_left);
        pos += snprintf(out + pos, bs - pos, "%s [保存并应用此项] \x1b[0m  ", h_apply ? "\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;121;192;255;1m");
        pos += snprintf(out + pos, bs - pos, "%s [从预设库导入] \x1b[0m  ", h_imp ? "\x1b[048;2;031;136;061m\x1b[038;2;255;255;255;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;031;136;061;1m");
        pos += snprintf(out + pos, bs - pos, "%s [删除此项] \x1b[0m", h_del ? "\x1b[048;2;248;081;073m\x1b[038;2;255;255;255;1m" : "\x1b[048;2;033;038;045m\x1b[038;2;248;081;073;1m");

        pos += snprintf(out + pos, bs - pos, "\x1b[19;%dH\x1b[038;2;139;148;158m提示: Tab 切换字段, ←/→ 选颜色, Enter 保存应用, Ctrl+P 导入预设, Ctrl+D 删除, Esc 返回\x1b[0m", main_left);
    }

    if (g_settings_show_presets) {
        render_settings_presets(out, bs, &pos, host_rows, host_cols);
    }

    *posp = pos;
}

/* ---------------------------------------------------------------------------
 * 顶栏右侧状态徽章
 *
 * 复制模式与搜索以前把整条操作提示常驻在屏幕上（复制模式占 60 列，搜索占满
 * 底行），信息量大但绝大多数时间是噪音。现在只保留一个短徽章贴在右上角，
 * 提示文字改成鼠标悬停时才向左展开；搜索的“上一个 / 下一个 / 关闭”做成可点
 * 的按钮，热区与绘制位置来自同一个 status_badge_layout()。
 * ------------------------------------------------------------------------- */
#define BADGE_ROW        2
#define BADGE_BTN_COLS   6   /* "[U 上]" / "[D 下]" */
#define BADGE_CLOSE_COLS 3   /* "[×]" */

static void badge_collapsed_text(char *out, int n) {
    if (g_copy_mode) {
        const char *state = g_copy_sel_active ? (g_copy_block ? "块选区" : "行选区")
                                              : (g_copy_quick ? "点选区" : "移动光标");
        snprintf(out, n, " [复制模式 %s] ", state);
        return;
    }
    char query[28] = {0};
    snprintf(query, sizeof(query), "%s", g_search_buf);
    /* 长关键词截断，徽章宽度必须可预测。 */
    int qcols = utf8_cols(query, (int)strlen(query));
    if (qcols > 16) {
        int keep = 0, cols = 0;
        while (query[keep] && cols < 13) {
            int adv = 0;
            unsigned int cp = utf8_decode_cp(query + keep, (int)strlen(query + keep), &adv);
            if (adv <= 0) break;
            cols += is_wide_cp(cp) ? 2 : 1;
            keep += adv;
        }
        query[keep] = 0;
        snprintf(out, n, " [搜索 \"%s…\" %d/%d] ", query, g_search_match_cur + 1, g_search_match_count);
        return;
    }
    snprintf(out, n, " [搜索 \"%s\" %d/%d] ", query, g_search_match_cur + 1, g_search_match_count);
}

int status_badge_layout(int host_cols, StatusBadge *out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    int kind = 0;
    if (g_copy_mode) kind = 1;
    else if (g_search_active && g_search_match_count > 0 && !g_search_mode) kind = 2;
    if (!kind) return 0;

    char text[96];
    badge_collapsed_text(text, sizeof(text));
    int badge_cols = utf8_cols(text, (int)strlen(text));
    int width = badge_cols;
    if (kind == 2) width += BADGE_BTN_COLS * 2 + BADGE_CLOSE_COLS;

    int start = host_cols - width + 1;
    if (start < 1) start = 1;
    out->kind = kind;
    out->row = BADGE_ROW;
    out->start = start;
    out->end = start + width;
    if (kind == 2) {
        out->prev_s = start + badge_cols;
        out->prev_e = out->prev_s + BADGE_BTN_COLS;
        out->next_s = out->prev_e;
        out->next_e = out->next_s + BADGE_BTN_COLS;
        out->close_s = out->next_e;
        out->close_e = out->close_s + BADGE_CLOSE_COLS;
    }
    return kind;
}

int status_badge_hovered(const StatusBadge *b) {
    if (!b || !b->kind) return 0;
    return (g_mouse_y + 1 == b->row && g_mouse_x + 1 >= b->start && g_mouse_x + 1 < b->end);
}

void render_status_badge(char *out, int bs, int *posp, int host_cols) {
    StatusBadge b;
    if (!status_badge_layout(host_cols, &b)) return;
    int pos = *posp;
    int hovered = status_badge_hovered(&b);
    const char *panel = "\x1b[048;2;033;038;045m";

    /* 搜索徽章（kind==2）本身就常驻 [U 上]/[D 下]/[×] 按钮，悬停不再展开
     * 重复的「U 上一个·D 下一个」长提示；只给复制模式（kind==1）保留悬停说明。 */
    if (hovered && b.kind == 1) {
        const char *hint = " Enter/Ctrl+C 复制 · Shift/Alt+方向改选区 · Esc 退出 ";
        int hint_cols = utf8_cols(hint, (int)strlen(hint));
        int hint_left = b.start - hint_cols;
        if (hint_left >= 1)
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s\x1b[038;2;230;237;243m%s\x1b[0m",
                            b.row, hint_left, panel, hint);
    }

    char text[96];
    badge_collapsed_text(text, sizeof(text));
    const char *badge_style = (b.kind == 1)
        ? "\x1b[048;2;210;153;034m\x1b[038;2;013;017;023;1m"
        : "\x1b[048;2;031;111;235m\x1b[038;2;255;255;255;1m";
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s%s\x1b[0m", b.row, b.start, badge_style, text);

    if (b.kind == 2) {
        int mrow = g_mouse_y + 1, mcol = g_mouse_x + 1;
        struct { int s, e; const char *label; } btns[3] = {
            { b.prev_s, b.prev_e, "[U 上]" },
            { b.next_s, b.next_e, "[D 下]" },
            { b.close_s, b.close_e, "[×]" },
        };
        for (int i = 0; i < 3; i++) {
            int hot = (mrow == b.row && mcol >= btns[i].s && mcol < btns[i].e);
            const char *style = hot
                ? (i == 2 ? "\x1b[048;2;248;081;073m\x1b[038;2;255;255;255;1m"
                          : "\x1b[048;2;121;192;255m\x1b[038;2;013;017;023;1m")
                : (i == 2 ? "\x1b[048;2;033;038;045m\x1b[038;2;248;081;073m"
                          : "\x1b[048;2;033;038;045m\x1b[038;2;121;192;255m");
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s%s\x1b[0m",
                            b.row, btns[i].s, style, btns[i].label);
        }
    }
    *posp = pos;
}

/* v1.8.10: 搜索输入框不再占用整条底行 —— 那等于凭空吃掉一行终端内容。
 * 现在它和搜索状态徽章一样，是贴右上角的一个紧凑小框，宽度固定、右对齐，
 * 渲染与光标定位共用 search_box_layout()。 */
#define SEARCH_BOX_PREFIX_COLS 6    /* " 搜索 " */
#define SEARCH_BOX_INPUT_COLS  24
#define SEARCH_BOX_SUFFIX_COLS 12   /* " Enter/Esc " + 右侧留白 */
#define SEARCH_BOX_COLS (SEARCH_BOX_PREFIX_COLS + SEARCH_BOX_INPUT_COLS + SEARCH_BOX_SUFFIX_COLS)

void search_box_layout(int host_cols, int *row, int *left, int *input_col, int *input_w) {
    int w = SEARCH_BOX_COLS;
    if (w > host_cols) w = host_cols;
    int l = host_cols - w + 1;
    if (l < 1) l = 1;
    int iw = SEARCH_BOX_INPUT_COLS;
    if (w < SEARCH_BOX_COLS) {
        iw = w - SEARCH_BOX_PREFIX_COLS - SEARCH_BOX_SUFFIX_COLS;
        if (iw < 1) iw = 1;
    }
    if (row) *row = BADGE_ROW;
    if (left) *left = l;
    if (input_col) *input_col = l + SEARCH_BOX_PREFIX_COLS;
    if (input_w) *input_w = iw;
}

#define CONFIRM_W 34
#define CONFIRM_H 4
#define CONFIRM_YES_W 10   /* [ Y 确认 ] */
#define CONFIRM_NO_W  14   /* [ N/Esc 取消 ] */
#define CONFIRM_GAP    2

void confirm_exit_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h) {
    int width = CONFIRM_W, height = CONFIRM_H;
    if (width > host_cols) width = host_cols;
    int t = (host_rows - height) / 2 + 1;
    if (t < 2) t = 2;
    int l = (host_cols - width) / 2 + 1;
    if (l < 1) l = 1;
    if (top) *top = t;
    if (left) *left = l;
    if (w) *w = width;
    if (h) *h = height;
}

/* Both the renderer and the mouse handler derive the button boxes from this
 * one helper, so the highlighted area and the clickable area can never drift
 * apart.  Columns are 1-based ANSI columns; *_end is exclusive. */
void confirm_exit_button_geom(int host_rows, int host_cols, int *row,
                              int *yes_start, int *yes_end,
                              int *no_start, int *no_end) {
    int top, left, w, h;
    confirm_exit_geom(host_rows, host_cols, &top, &left, &w, &h);
    (void)h;
    int interior = w - 2;
    if (interior < 0) interior = 0;
    int used = CONFIRM_YES_W + CONFIRM_GAP + CONFIRM_NO_W;
    int pad = interior - used;
    if (pad < 0) pad = 0;
    int lead = pad / 2;
    int ys = left + 1 + lead;
    int ns = ys + CONFIRM_YES_W + CONFIRM_GAP;
    if (row) *row = top + 2;
    if (yes_start) *yes_start = ys;
    if (yes_end) *yes_end = ys + CONFIRM_YES_W;
    if (no_start) *no_start = ns;
    if (no_end) *no_end = ns + CONFIRM_NO_W;
}

static void confirm_pad(char *out, int bs, int *posp, int n) {
    int pos = *posp;
    while (n-- > 0 && pos < bs - 2) out[pos++] = ' ';
    *posp = pos;
}

void render_confirm_exit(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int pos = *posp, top, left, w, h;
    confirm_exit_geom(host_rows, host_cols, &top, &left, &w, &h);
    (void)h;
    int interior = w - 2;
    if (interior < 0) interior = 0;

    const char *panel = "\x1b[048;2;033;038;045m";
    const char *hdr = "┌─ 退出确认 ";
    int cols = utf8_cols(hdr, (int)strlen(hdr));
    pos += snprintf(out + pos, bs - pos,
        "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;248;081;073m%s", top, left, hdr);
    while (cols < w - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80'; cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    const char *msg = " 确定要退出 termux 吗？";
    int msg_cols = utf8_cols(msg, (int)strlen(msg));
    pos += snprintf(out + pos, bs - pos,
        "\x1b[%d;%dH%s│%s\x1b[038;2;230;237;243m%s", top + 1, left, panel, panel, msg);
    confirm_pad(out, bs, &pos, interior - msg_cols);
    pos += snprintf(out + pos, bs - pos, "\x1b[0m%s│\x1b[0m", panel);

    int row, ys, ye, ns, ne;
    confirm_exit_button_geom(host_rows, host_cols, &row, &ys, &ye, &ns, &ne);
    int mrow = g_mouse_y + 1, mcol = g_mouse_x + 1;
    int yes_hover = (mrow == row && mcol >= ys && mcol < ye);
    int no_hover = (mrow == row && mcol >= ns && mcol < ne);

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s│%s", row, left, panel, panel);
    confirm_pad(out, bs, &pos, ys - (left + 1));
    if (yes_hover)
        pos += snprintf(out + pos, bs - pos,
            "\x1b[048;2;248;081;073m\x1b[038;2;255;255;255;1m[ Y 确认 ]\x1b[0m%s", panel);
    else
        pos += snprintf(out + pos, bs - pos,
            "%s\x1b[038;2;248;081;073;1m[\x1b[22m\x1b[038;2;230;237;243m Y 确认 \x1b[038;2;248;081;073;1m]\x1b[22m", panel);
    confirm_pad(out, bs, &pos, CONFIRM_GAP);
    if (no_hover)
        pos += snprintf(out + pos, bs - pos,
            "\x1b[048;2;063;185;080m\x1b[038;2;013;017;023;1m[ N/Esc 取消 ]\x1b[0m%s", panel);
    else
        pos += snprintf(out + pos, bs - pos,
            "%s\x1b[038;2;063;185;080;1m[\x1b[22m\x1b[038;2;230;237;243m N/Esc 取消 \x1b[038;2;063;185;080;1m]\x1b[22m", panel);
    confirm_pad(out, bs, &pos, (left + w - 1) - ne);
    pos += snprintf(out + pos, bs - pos, "\x1b[0m%s│\x1b[0m", panel);

    palette_hline(out, bs, &pos, top + 3, left, w, "└", "┘");
    pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
    *posp = pos;
}

void render_search_box(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int pos = *posp;
    int row, left, input_col, box_w;
    (void)host_rows;
    search_box_layout(host_cols, &row, &left, &input_col, &box_w);

    pos += snprintf(out + pos, bs - pos,
                    "\x1b[%d;%dH\x1b[048;2;033;038;045m\x1b[038;2;121;192;255;1m 搜索 \x1b[0m", row, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[048;2;022;027;034m\x1b[038;2;255;255;255m");
    render_scrollable_input(out, bs, &pos, g_search_buf, g_search_len, g_search_pos, box_w,
                            "\x1b[048;2;022;027;034m", NULL);
    pos += snprintf(out + pos, bs - pos,
                    "\x1b[0m\x1b[048;2;033;038;045m\x1b[038;2;139;148;158m Enter/Esc  \x1b[0m");
    *posp = pos;
}

#define PALETTE_MAX_VISIBLE 9
#define PALETTE_W 72
#define PALETTE_EDITOR_W 78
#define PALETTE_EDITOR_H 12   /* 三个输入框 + 颜色行 + 分隔线 + 操作行 */

typedef struct {
    const char *id;
    const char *title;
    const char *desc;
    const char *shortcut;
    PaletteAction action;
    int value;
    int number;
    int color;
} PaletteStaticItem;

static const PaletteStaticItem g_palette_root_items[] = {
    { "operations", "操作命令面板", "新建、切换、搜索与终端操作", "Enter 进入", PALETTE_ACTION_OPEN_OPERATIONS, 0, 1, 4 },
    { "settings",   "设置命令面板", "启动项、INI 文件与菜单项设置", "Enter 进入", PALETTE_ACTION_OPEN_SETTINGS, 0, 2, 6 },
};

static const PaletteStaticItem g_palette_operation_items[] = {
    { "new-terminal",       "新建终端",           "选择已配置终端，支持搜索",       "Enter 进入", PALETTE_ACTION_OPEN_NEW_TERMINAL, 0, 1, 1 },
    { "custom-command",    "启动自定义命令行",   "输入并启动任意自定义命令行",       "",           PALETTE_ACTION_START_CUSTOM,       0, 2, 2 },
    { "rename",             "修改标题",           "修改当前标签页显示标题",           "",           PALETTE_ACTION_RENAME,             0, 3, 3 },
    { "color",              "修改颜色",           "修改当前标签页的主题颜色",         "",           PALETTE_ACTION_COLOR,              0, 4, 4 },
    { "search-history",     "搜索历史",           "搜索当前终端的滚动历史",           "",           PALETTE_ACTION_SEARCH,             0, 5, 5 },
    { "switch-panel",       "切换 panel",         "按编号或标题选择并切换 panel",     "Enter 进入", PALETTE_ACTION_SWITCH_PANEL,       0, 6, 6 },
    { "copy-mode",          "进入复制模式",       "移动光标、行选/块选终端文本并复制",     "",           PALETTE_ACTION_COPY_MODE,          0, 7, 7 },
    { "reload",             "热重载",             "重新加载 termux.ini 配置文件",      "",           PALETTE_ACTION_RELOAD,             0, 8, 5 },
    { "open-settings-page", "打开设置页面",       "进入图形化设置页面",               "Enter 打开", PALETTE_ACTION_GRAPHICAL_SETTINGS, 0, 9, 4 },
    { "settings-command-panel", "打开设置命令面板", "切换到设置命令面板",           "Enter 进入", PALETTE_ACTION_OPEN_SETTINGS,     0, 10, 6 },
    { "about",              "关于",               "查看版本、作者与系统信息",         "Enter 打开", PALETTE_ACTION_OPEN_ABOUT,         0, 11, 7 },
    { "close-panel",        "关闭当前 panel",     "关闭当前活动 panel",                "",           PALETTE_ACTION_CLOSE_PANEL,       0, 12, 7 },
    { "quit",               "退出 termux",       "退出整个 termux 程序并关闭所有会话", "",           PALETTE_ACTION_QUIT,              0, 13, 8 },
};

static const PaletteStaticItem g_palette_setting_items[] = {
    { "operations-command-panel", "打开操作命令面板", "切换到操作命令面板",       "Enter 进入", PALETTE_ACTION_OPEN_OPERATIONS,   0, 1, 1 },
    { "default-startup",    "修改默认启动项", "选择启动时显示终端或帮助页面",     "Enter 进入", PALETTE_ACTION_DEFAULT_STARTUP,     0, 2, 6 },
    { "open-ini",           "打开设置文件 (.ini)", "使用系统默认编辑器打开 termux.ini", "",           PALETTE_ACTION_OPEN_INI,          0, 3, 6 },
    { "add-panel",          "添加 panel 条目", "选择预设或自定义并继续编辑",       "Enter 进入", PALETTE_ACTION_ADD_PANEL,         0, 4, 2 },
    { "menu-settings",      "菜单项设置",     "在子面板中选择并编辑 panel 条目",       "Enter 进入", PALETTE_ACTION_MENU_SETTINGS,     0, 5, 4 },
    { "next-theme",         "切换配色主题",   "在内置主题之间轮换并写入 termux.ini", "",           PALETTE_ACTION_NEXT_THEME,        0, 6, 8 },
    { "appearance",         "外观 / 主题",    "设置页：选择主题、编辑 16 个语义色",  "Enter 打开", PALETTE_ACTION_OPEN_APPEARANCE,   0, 7, 6 },
    { "key-bindings",       "键位设置",       "设置页：前缀键与全部动作键位录制",    "Enter 打开", PALETTE_ACTION_OPEN_KEYS,         0, 8, 1 },
    { "behavior",           "行为开关",       "设置页：鼠标、自动复制、退出确认、滚动行数", "Enter 打开", PALETTE_ACTION_OPEN_BEHAVIOR, 0, 9, 2 },
};

static const PaletteStaticItem g_palette_startup_items[] = {
    { "terminal", "默认终端", "启动时创建并显示终端 panel", "", PALETTE_ACTION_SELECT_DEFAULT, 0, 1, 1 },
    { "help",     "内置帮助", "启动时显示内置帮助页面",     "", PALETTE_ACTION_SELECT_DEFAULT, 1, 2, 6 },
};

static int palette_static_count(int page) {
    switch (page) {
        case PALETTE_PAGE_ROOT: return (int)(sizeof(g_palette_root_items) / sizeof(g_palette_root_items[0]));
        case PALETTE_PAGE_OPERATIONS: return (int)(sizeof(g_palette_operation_items) / sizeof(g_palette_operation_items[0]));
        case PALETTE_PAGE_SETTINGS: return (int)(sizeof(g_palette_setting_items) / sizeof(g_palette_setting_items[0]));
        case PALETTE_PAGE_DEFAULT_STARTUP: return (int)(sizeof(g_palette_startup_items) / sizeof(g_palette_startup_items[0]));
        default: return 0;
    }
}

static int palette_strcasestr(const char *haystack, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!haystack || !*haystack) return 0;
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int k = 0; k < nlen; k++) {
            char ch1 = haystack[i + k];
            char ch2 = needle[k];
            if (ch1 >= 'A' && ch1 <= 'Z') ch1 = (char)(ch1 + ('a' - 'A'));
            if (ch2 >= 'A' && ch2 <= 'Z') ch2 = (char)(ch2 + ('a' - 'A'));
            if (ch1 != ch2) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static int palette_strcase_prefix(const char *haystack, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!haystack) return 0;
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    if (nlen > hlen) return 0;
    for (int i = 0; i < nlen; i++) {
        char ch1 = haystack[i];
        char ch2 = needle[i];
        if (ch1 >= 'A' && ch1 <= 'Z') ch1 = (char)(ch1 + ('a' - 'A'));
        if (ch2 >= 'A' && ch2 <= 'Z') ch2 = (char)(ch2 + ('a' - 'A'));
        if (ch1 != ch2) return 0;
    }
    return 1;
}

static int palette_strcase_equal(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    return (int)strlen(haystack) == (int)strlen(needle) &&
           palette_strcase_prefix(haystack, needle);
}

static int palette_match_score(const PaletteItemInfo *item, const char *query, int title_only) {
    if (!item || !query) return -1;
    if (!*query) return 0;

    const char *fields[4] = { item->title, item->desc, item->shortcut, item->id };
    int best = -1;
    int field_count = title_only ? 1 : 4;
    for (int field = 0; field < field_count; field++) {
        const char *text = fields[field];
        if (!text || !*text || !palette_strcasestr(text, query)) continue;
        int score = field * 30 + 20;
        if (palette_strcase_prefix(text, query)) score = field * 30 + 10;
        if (palette_strcase_equal(text, query)) score = field * 30;
        if (best < 0 || score < best) best = score;
    }
    return best;
}

static int palette_copy_static(const PaletteStaticItem *src, PaletteItemInfo *out) {
    if (!src || !out) return 0;
    out->id = src->id;
    out->title = src->title;
    out->desc = src->desc;
    out->shortcut = src->shortcut;
    out->action = src->action;
    out->value = src->value;
    out->number = src->number;
    out->color = src->color;
    return 1;
}

int palette_item_count(int page) {
    switch (page) {
        case PALETTE_PAGE_ROOT:
        case PALETTE_PAGE_OPERATIONS:
        case PALETTE_PAGE_SETTINGS:
        case PALETTE_PAGE_DEFAULT_STARTUP:
            return palette_static_count(page);
        case PALETTE_PAGE_NEW_TERMINAL:
            return g_chooser_item_count;
        case PALETTE_PAGE_SWITCH_PANEL: {
            int count = 0;
            for (int i = 0; i < g_mux.pane_count; i++)
                if (g_mux.panes[i].active) count++;
            return count;
        }
        case PALETTE_PAGE_ADD_PANEL:
            return g_preset_count;
        case PALETTE_PAGE_MENU_SETTINGS:
            /* Menu item settings edits existing entries only.  Adding a new
             * entry remains available from the settings command panel, not
             * from this management subpanel. */
            return g_chooser_item_count;
        default:
            return 0;
    }
}

int palette_item_info(int page, int item_index, PaletteItemInfo *out) {
    if (!out || item_index < 0) return 0;
    memset(out, 0, sizeof(*out));

    if (page == PALETTE_PAGE_ROOT && item_index < palette_static_count(page))
        return palette_copy_static(&g_palette_root_items[item_index], out);
    if (page == PALETTE_PAGE_OPERATIONS && item_index < palette_static_count(page))
        return palette_copy_static(&g_palette_operation_items[item_index], out);
    if (page == PALETTE_PAGE_SETTINGS && item_index < palette_static_count(page))
        return palette_copy_static(&g_palette_setting_items[item_index], out);
    if (page == PALETTE_PAGE_DEFAULT_STARTUP && item_index < palette_static_count(page)) {
        if (!palette_copy_static(&g_palette_startup_items[item_index], out)) return 0;
        out->shortcut = (g_default_startup == item_index) ? "当前默认" : "";
        return 1;
    }

    if (page == PALETTE_PAGE_NEW_TERMINAL && item_index < g_chooser_item_count) {
        out->id = "configured-terminal";
        out->title = g_chooser_items[item_index].name;
        out->desc = g_chooser_items[item_index].cmd;
        out->shortcut = g_chooser_items[item_index].cmd;
        out->action = PALETTE_ACTION_SELECT_TERMINAL;
        out->value = item_index;
        out->number = item_index + 1;
        out->color = (item_index % 8) + 1;
        return 1;
    }

    if (page == PALETTE_PAGE_SWITCH_PANEL) {
        int visible_index = 0;
        for (int i = 0; i < g_mux.pane_count; i++) {
            if (!g_mux.panes[i].active) continue;
            if (visible_index++ != item_index) continue;
            out->id = "panel";
            out->title = g_mux.panes[i].title[0] ? g_mux.panes[i].title : "cmd";
            out->desc = g_mux.panes[i].full_title;
            out->shortcut = "";
            out->action = PALETTE_ACTION_SELECT_PANEL;
            out->value = i;
            out->number = item_index + 1;
            out->color = (g_mux.panes[i].color >= 0 && g_mux.panes[i].color <= 8) ?
                         g_mux.panes[i].color : 0;
            return 1;
        }
        return 0;
    }

    if (page == PALETTE_PAGE_ADD_PANEL && item_index < g_preset_count) {
        out->id = "panel-preset";
        out->title = g_presets[item_index].name;
        out->desc = g_presets[item_index].cmd;
        out->shortcut = g_presets[item_index].cmd;
        out->action = PALETTE_ACTION_SELECT_PANEL;
        out->value = item_index;
        out->number = item_index + 1;
        out->color = item_index == g_preset_count - 1 ? 2 : 5;
        return 1;
    }

    if (page == PALETTE_PAGE_MENU_SETTINGS) {
        if (item_index >= 0 && item_index < g_chooser_item_count) {
            out->id = "menu-item";
            out->title = g_chooser_items[item_index].name[0] ? g_chooser_items[item_index].name : "未命名 panel";
            out->desc = g_chooser_items[item_index].cmd;
            out->shortcut = "Enter 编辑";
            out->action = PALETTE_ACTION_EDIT_PANEL;
            out->value = item_index;
            out->number = item_index + 1;
            out->color = (item_index % 8) + 1;
            return 1;
        }
    }
    return 0;
}

int palette_filter_cmds(int page, int *out_indices, int max_out, const char *query) {
    if (!out_indices || max_out <= 0) return 0;
    if (max_out > 64) max_out = 64;
    int count = 0;
    int total = palette_item_count(page);
    int title_only = 0;

    /* A terminal chooser should search the configured display name first.
     * If a name matches, command-line substrings from other entries must not
     * drown it out (for example, 'w' should prefer WSL over powershell.exe). */
    if (page == PALETTE_PAGE_NEW_TERMINAL && query && *query) {
        for (int i = 0; i < total; i++) {
            PaletteItemInfo item;
            if (palette_item_info(page, i, &item) && palette_strcasestr(item.title, query)) {
                title_only = 1;
                break;
            }
        }
    }

    int scores[64];
    for (int i = 0; i < total; i++) {
        PaletteItemInfo item;
        if (!palette_item_info(page, i, &item)) continue;
        int score = palette_match_score(&item, query ? query : "", title_only);
        if (score < 0) continue;
        if (count >= max_out && score >= scores[count - 1]) continue;

        int new_count = count < max_out ? count + 1 : max_out;
        int at = new_count - 1;
        while (at > 0 && scores[at - 1] > score) {
            out_indices[at] = out_indices[at - 1];
            scores[at] = scores[at - 1];
            at--;
        }
        out_indices[at] = i;
        scores[at] = score;
        count = new_count;
    }
    return count;
}

int palette_visible_rows(int host_rows) {
    int visible = PALETTE_MAX_VISIBLE;
    int max_visible = host_rows - 5; /* top=2, footer+bottom end at top+visible+4 */
    if (max_visible < 1) max_visible = 1;
    if (visible > max_visible) visible = max_visible;
    return visible;
}

void palette_editor_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h, int *input_w) {
    int pw = PALETTE_EDITOR_W;
    if (pw > host_cols) pw = host_cols;
    if (pw < 1) pw = 1;
    int ph = PALETTE_EDITOR_H;
    /* The three label/input pairs, divider and combined action/help row fit
     * in ten rows.  Do not stretch the child to the parent's list height:
     * that old filler area was visible as needless blank space below the
     * editor.  render_palette_editor() clears the stale parent rows below the
     * compact child surface instead. */
    if (host_rows > 0 && ph > host_rows) ph = host_rows;
    if (ph < 1) ph = 1;
    if (top) *top = 2;
    if (left) *left = (host_cols - pw) / 2 + 1;
    if (w) *w = pw;
    if (h) *h = ph;
    if (input_w) {
        int iw = pw - 4;
        if (iw < 8) iw = 8;
        *input_w = iw;
    }
}

void palette_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h) {
    if (g_mux.palette_page == PALETTE_PAGE_PANEL_EDITOR) {
        palette_editor_geom(host_rows, host_cols, top, left, w, h, NULL);
        return;
    }
    int pw = PALETTE_W;
    if (pw > host_cols) pw = host_cols;
    if (pw < 1) pw = 1;
    int visible = palette_visible_rows(host_rows);
    if (top) *top = 2;
    if (left) *left = (host_cols - pw) / 2 + 1;
    if (w) *w = pw;
    if (h) *h = visible + 5;
}

static const char *palette_page_title(int page) {
    switch (page) {
        case PALETTE_PAGE_ROOT: return "命令面板";
        case PALETTE_PAGE_OPERATIONS: return "命令面板 / 操作命令面板";
        case PALETTE_PAGE_SETTINGS: return "命令面板 / 设置命令面板";
        case PALETTE_PAGE_NEW_TERMINAL: return "操作 / 新建终端";
        case PALETTE_PAGE_SWITCH_PANEL: return "操作 / 切换 panel";
        case PALETTE_PAGE_DEFAULT_STARTUP: return "设置 / 默认启动项";
        case PALETTE_PAGE_ADD_PANEL: return "设置 / 添加 panel 条目";
        case PALETTE_PAGE_MENU_SETTINGS: return "设置 / 菜单项设置";
        case PALETTE_PAGE_PANEL_EDITOR: return "设置 / 编辑 panel 条目";
        default: return "命令面板";
    }
}

static void palette_hline(char *out, int bs, int *posp, int row, int left, int width, const char *prefix, const char *suffix) {
    int pos = *posp;
    int cols = 0;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m%s", row, left, prefix);
    cols += utf8_cols(prefix, (int)strlen(prefix));
    while (cols < width - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "%s\x1b[0m", suffix);
    *posp = pos;
}

static void render_palette_item_row(char *out, int bs, int *posp, int row, int left, int width,
                                    int page, int item_index, int display_number, int selected, int hovered,
                                    const PaletteItemInfo *item) {
    int pos = *posp;
    const char *bg = (selected || hovered) ? "\x1b[048;2;038;060;088m" : "\x1b[048;2;022;027;034m";
    const char *fg = selected ? "\x1b[038;2;255;255;255;1m" : "\x1b[038;2;230;237;243m";
    char tag[16];
    int tagw;
    if (display_number > 0) {
        snprintf(tag, sizeof(tag), "[%d]", display_number);
        tagw = utf8_cols(tag, (int)strlen(tag));
    } else {
        snprintf(tag, sizeof(tag), "   ");
        tagw = 3;
    }

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m%s", row, left, bg);
    int cols = 1; /* left border */
    pos += snprintf(out + pos, bs - pos, "%s ", selected ? "▶" : " ");
    cols += 2;

    /* v1.8.8: 序号一律用普通文字色，不再按标签颜色上色块、也不再用琥珀色，
     * 避免一列 [1][2][3] 花花绿绿抢视线。 */
    pos += snprintf(out + pos, bs - pos, "%s%s%s", bg,
                    selected ? "\x1b[038;2;230;237;243m" : "\x1b[038;2;139;148;158m", tag);
    cols += tagw;
    out[pos++] = ' '; cols++;

    int shortcut_w = item && item->shortcut ? utf8_cols(item->shortcut, (int)strlen(item->shortcut)) : 0;
    if (shortcut_w > 18) shortcut_w = 18;
    /* Reserve exactly the separator plus the shortcut; the final padding
     * loop supplies any remaining space before the right border. */
    int title_w = width - 1 - cols - (shortcut_w > 0 ? shortcut_w + 1 : 1);
    if (title_w < 4) title_w = 4;
    pos += snprintf(out + pos, bs - pos, "%s", fg);
    if (item) append_padded_utf8(out, bs, &pos, &cols, item->title ? item->title : "", title_w);
    else append_padded_utf8(out, bs, &pos, &cols, "", title_w);
    pos += snprintf(out + pos, bs - pos, "%s", bg);

    if (shortcut_w > 0 && item && item->shortcut) {
        out[pos++] = ' '; cols++;
        pos += snprintf(out + pos, bs - pos, "\x1b[038;2;139;148;158m");
        append_padded_utf8(out, bs, &pos, &cols, item->shortcut, shortcut_w);
        pos += snprintf(out + pos, bs - pos, "%s", bg);
    } else {
        out[pos++] = ' '; cols++;
    }
    while (cols < width - 1 && pos < bs - 8) { out[pos++] = ' '; cols++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");
    (void)page;
    (void)item_index;
    *posp = pos;
}

static void render_palette_editor(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int top, left, pw, ph, input_w;
    palette_editor_geom(host_rows, host_cols, &top, &left, &pw, &ph, &input_w);
    int pos = *posp;
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "┌─ %s ", palette_page_title(PALETTE_PAGE_PANEL_EDITOR));
    int hcols = utf8_cols(hdr, (int)strlen(hdr));
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;137;087;229;1m%s", top, left, hdr);
    while (hcols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80'; hcols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    const char *labels[3] = {
        "显示名称 (Display Name)",
        "启动命令行 (Command Line)",
        "启动目录 (Working Directory)"
    };
    char *bufs[3] = { g_edit_name, g_edit_cmd, g_edit_dir };
    int lens[3] = { g_edit_name_len, g_edit_cmd_len, g_edit_dir_len };
    int poss[3] = { g_edit_name_pos, g_edit_cmd_pos, g_edit_dir_pos };
    for (int i = 0; i < 3; i++) {
        int label_row = top + 1 + i * 2;
        int input_row = label_row + 1;
        int active = (g_mux.palette_field == i);
        const char *label_style = active ? "\x1b[038;2;230;237;243;1m" : "\x1b[038;2;230;237;243m";
        int label_cols = 1;
        pos += snprintf(out + pos, bs - pos,
                        "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[048;2;033;038;045m ",
                        label_row, left);
        pos += snprintf(out + pos, bs - pos, "%s", label_style);
        label_cols++;
        int label_w = pw - 1 - label_cols;
        if (label_w < 1) label_w = 1;
        append_padded_utf8(out, bs, &pos, &label_cols, labels[i], label_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");
        const char *field_bg = active ? "\x1b[048;2;038;060;088m" : "\x1b[048;2;022;027;034m";
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m%s ", input_row, left, field_bg);
        render_scrollable_input(out, bs, &pos, bufs[i], lens[i], poss[i], input_w, field_bg, NULL);
        pos += snprintf(out + pos, bs - pos, "%s \x1b[0m\x1b[048;2;033;038;045m│\x1b[0m", field_bg);
    }

    /* v1.8.9: 第 4 个字段 —— 启动默认颜色。 */
    {
        int label_row = top + 7;
        int active = (g_mux.palette_field == 3);
        const char *label = "启动默认颜色 (Tab Color)";
        int label_cols = 1;
        pos += snprintf(out + pos, bs - pos,
                        "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[048;2;033;038;045m ",
                        label_row, left);
        pos += snprintf(out + pos, bs - pos, "%s",
                        active ? "\x1b[038;2;230;237;243;1m" : "\x1b[038;2;230;237;243m");
        label_cols++;
        int label_w = pw - 1 - label_cols;
        if (label_w < 1) label_w = 1;
        append_padded_utf8(out, bs, &pos, &label_cols, label, label_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");

        int color_row = top + 8;
        pos += snprintf(out + pos, bs - pos,
                        "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[048;2;022;027;034m",
                        color_row, left);
        int fill = pw - 2;
        for (int k = 0; k < fill && pos < bs - 8; k++) out[pos++] = ' ';
        pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");
        render_item_color_row(out, bs, &pos, color_row, left + 1, g_edit_color, g_mux.palette_field == 3);
    }

    palette_hline(out, bs, &pos, top + 9, left, pw, "├", "┤");
    const char *save_hint = " [Enter] 保存并返回上一级";
    const char *editor_hint = "  Tab 切换字段 · ←/→ 选颜色 · Esc 返回 · Ctrl+S 保存 ";
    int action_row = top + 10;
    pos += snprintf(out + pos, bs - pos,
                    "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[038;2;121;192;255;1m%s"
                    "\x1b[038;2;139;148;158m%s",
                    action_row, left, save_hint, editor_hint);
    int used = 1 + utf8_cols(save_hint, (int)strlen(save_hint)) +
               utf8_cols(editor_hint, (int)strlen(editor_hint));
    while (used < pw - 1 && pos < bs - 8) { out[pos++] = ' '; used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");

    /* A page switch does not redraw the old parent list first.  Erase only
     * the rows that the old, taller parent box occupied below this compact
     * editor; do not use EL because the terminal pane continues to the right. */
    int parent_pw = PALETTE_W;
    if (parent_pw > host_cols) parent_pw = host_cols;
    if (parent_pw < 1) parent_pw = 1;
    int parent_left = (host_cols - parent_pw) / 2 + 1;
    int parent_h = palette_visible_rows(host_rows) + 5;
    for (int r = top + ph; r < top + parent_h; r++) {
        pos += snprintf(out + pos, bs - pos,
                        "\x1b[%d;%dH\x1b[048;2;022;027;034m",
                        r, parent_left);
        int clear_cols = 0;
        while (clear_cols < parent_pw && pos < bs - 8) {
            out[pos++] = ' ';
            clear_cols++;
        }
        pos += snprintf(out + pos, bs - pos, "\x1b[0m");
    }

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m└", top + ph - 1, left);
    int cols = 1;
    while (cols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80'; cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");
    *posp = pos;
}

void render_command_palette(char *out, int bs, int *posp, int host_rows, int host_cols) {
    if (g_mux.palette_page == PALETTE_PAGE_PANEL_EDITOR) {
        render_palette_editor(out, bs, posp, host_rows, host_cols);
        return;
    }

    int top, left, pw, ph;
    palette_geom(host_rows, host_cols, &top, &left, &pw, &ph);
    int pos = *posp;
    int filtered[64];
    int filtered_count = palette_filter_cmds(g_mux.palette_page, filtered, 64, g_mux.palette_query);
    int visible = palette_visible_rows(host_rows);
    if (g_mux.palette_sel >= filtered_count) g_mux.palette_sel = filtered_count > 0 ? filtered_count - 1 : 0;
    if (g_mux.palette_sel < 0) g_mux.palette_sel = 0;
    if (g_mux.palette_sel < g_mux.palette_scroll) g_mux.palette_scroll = g_mux.palette_sel;
    if (g_mux.palette_sel >= g_mux.palette_scroll + visible)
        g_mux.palette_scroll = g_mux.palette_sel - visible + 1;
    int max_scroll = filtered_count > visible ? filtered_count - visible : 0;
    if (g_mux.palette_scroll > max_scroll) g_mux.palette_scroll = max_scroll;
    if (g_mux.palette_scroll < 0) g_mux.palette_scroll = 0;

    char hdr[160];
    snprintf(hdr, sizeof(hdr), "┌─ %s ", palette_page_title(g_mux.palette_page));
    int cols = utf8_cols(hdr, (int)strlen(hdr));
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[038;2;255;255;255m\x1b[048;2;137;087;229;1m%s", top, left, hdr);
    while (cols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80'; cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┐\x1b[0m");

    int input_w = pw - 6;
    if (input_w < 8) input_w = 8;
    const char *palette_input_bg = g_mux.palette_focus == PALETTE_FOCUS_INPUT
        ? "\x1b[048;2;038;060;088m" : "\x1b[048;2;022;027;034m";
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m%s > ", top + 1, left, palette_input_bg);
    render_scrollable_input(out, bs, &pos, g_mux.palette_query, g_mux.palette_query_len, g_mux.palette_query_pos, input_w, palette_input_bg, NULL);
    pos += snprintf(out + pos, bs - pos, "%s \x1b[0m\x1b[048;2;033;038;045m│\x1b[0m", palette_input_bg);

    palette_hline(out, bs, &pos, top + 2, left, pw, "├", "┤");

    for (int vi = 0; vi < visible; vi++) {
        int row = top + 3 + vi;
        int fi = g_mux.palette_scroll + vi;
        if (fi < filtered_count) {
            PaletteItemInfo item;
            palette_item_info(g_mux.palette_page, filtered[fi], &item);
            int mouse_row = g_mouse_y + 1;
            int mouse_col = g_mouse_x + 1;
            int hovered = (mouse_row == row && mouse_col >= left && mouse_col < left + pw);
            render_palette_item_row(out, bs, &pos, row, left, pw, g_mux.palette_page,
                                    filtered[fi], vi + 1, fi == g_mux.palette_sel, hovered, &item);
        } else {
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[048;2;022;027;034m", row, left);
            cols = 1;
            if (filtered_count == 0 && vi == 0) {
                const char *none = "  无匹配项目";
                pos += snprintf(out + pos, bs - pos, "\x1b[038;2;139;148;158m%s", none);
                cols += utf8_cols(none, (int)strlen(none));
            }
            while (cols < pw - 1 && pos < bs - 8) { out[pos++] = ' '; cols++; }
            pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");
        }
    }

    int footer = top + 3 + visible;
    const char *focus_label = g_mux.palette_focus == PALETTE_FOCUS_LIST ? "选择" : "输入";
    /* An untouched query field hands arrows/digits straight to the list, so
     * advertise the digits as a quick pick there too. */
    const char *digit_label = (g_mux.palette_focus == PALETTE_FOCUS_LIST ||
                               g_mux.palette_query_len == 0) ? "选当前" : "搜索";
    char footer_hint[192];
    if (g_mux.palette_page == PALETTE_PAGE_MENU_SETTINGS) {
        snprintf(footer_hint, sizeof(footer_hint),
                 g_mux.palette_query_len
                     ? " [焦点:%s] Tab切换 · 数字:%s · Enter编辑 · Ctrl+X删 · 搜索禁调序"
                     : " [焦点:%s] Tab切换 · 数字:%s · Enter编辑 · Ctrl+↑/↓调序 · X删",
                 focus_label, digit_label);
    } else {
        snprintf(footer_hint, sizeof(footer_hint),
                 " [焦点:%s] Tab切换 · 数字:%s · Enter执行/进入 · Esc返回",
                 focus_label, digit_label);
    }
    pos += snprintf(out + pos, bs - pos,
                    "\x1b[%d;%dH\x1b[048;2;033;038;045m│\x1b[0m\x1b[048;2;033;038;045m\x1b[038;2;139;148;158m",
                    footer, left);
    int footer_cols = 0;
    int footer_inner = pw > 2 ? pw - 2 : 0;
    if (footer_inner > 0)
        append_padded_utf8(out, bs, &pos, &footer_cols, footer_hint, footer_inner);
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");
    palette_hline(out, bs, &pos, top + ph - 1, left, pw, "└", "┘");
    *posp = pos;
}

static const char *const g_help_head[] = {
    "\x1b[038;2;255;255;255m\x1b[048;2;031;111;235m termux - 帮助",
    "\x1b[038;2;139;148;158m  版本 v" TERMUX_VERSION " | Windows Terminal Multiplexer (Win10 1809+)\x1b[0m",
    "",
    "\x1b[038;2;121;192;255;1m  键盘快捷键\x1b[0m",
};
static const int g_help_head_count = (int)(sizeof(g_help_head) / sizeof(g_help_head[0]));

/* 快捷键区：键位从 keymap 实时取，改了 prefix / [keys] 后帮助页同步变化 */
typedef struct {
    int action;
    const char *extra;   /* 追加说明，可为 NULL */
    const char *desc;
} HelpShortcut;

static const HelpShortcut g_help_shortcuts[] = {
    {ACT_COMMAND_PALETTE, NULL,         "命令面板（操作 / 设置两类）"},
    {ACT_NEW_PANE,        NULL,         "新建默认 pane"},
    {ACT_NEW_PANE_MENU,   NULL,         "新建 pane 菜单 (选择/自定义命令行)"},
    {ACT_COPY_MODE,       NULL,         "进入复制模式 (Shift/Alt+方向选择, Enter/Ctrl+C 复制)"},
    {ACT_SEARCH,          NULL,         "搜索滚动历史 (n/N 跳转匹配, Esc 退出)"},
    {ACT_NEXT_PANE,       NULL,         "下一个 pane"},
    {ACT_PREV_PANE,       NULL,         "上一个 pane"},
    {ACT_CLOSE_PANE,      NULL,         "关闭当前 panel"},
    {ACT_SETTINGS,        NULL,         "打开图形化设置 (termux.ini)"},
    {ACT_RELOAD_CONFIG,   NULL,         "热重载配置文件 (termux.ini)"},
    {ACT_NEXT_THEME,      NULL,         "切换配色主题"},
    {ACT_HELP,            NULL,         "打开 / 关闭本帮助"},
    {ACT_QUIT,            NULL,         "退出 termux"},
    {ACT_TAB_COLOR_NEXT,  "Shift 反向", "轮换标签颜色"},
    {ACT_SELECT_PANE,     "0-9",        "跳转到 pane (支持主键盘与小键盘)"},
};
static const int g_help_shortcut_count = (int)(sizeof(g_help_shortcuts) / sizeof(g_help_shortcuts[0]));

static const char *const g_help_tail[] = {
    "",
    "\x1b[038;2;121;192;255;1m  鼠标操作\x1b[0m",
    "  \x1b[038;2;230;237;243m点击 tab\x1b[0m           切换 pane",
    "  \x1b[038;2;230;237;243m点击 [x]\x1b[0m           关闭该 pane",
    "  \x1b[038;2;230;237;243m右键 tab\x1b[0m           改颜色 / 改标题",
    "  \x1b[038;2;230;237;243m点击 [+]\x1b[0m           新建 pane (支持选择/自定义命令行)",
    "  \x1b[038;2;230;237;243m点击 [*]\x1b[0m           打开图形化设置页面",
    "  \x1b[038;2;230;237;243m点击 termux\x1b[0m        打开 / 关闭本帮助",
    "  \x1b[038;2;230;237;243m鼠标左键拖选\x1b[0m       框选终端文字，松开自动复制到剪贴板",
    "",
    "\x1b[038;2;121;192;255;1m  提示与警告\x1b[0m",
    "  - \x1b[038;2;248;081;073m警告: 终端必须使用等宽字体，否则会渲染故障\x1b[0m",
    "  - 每个 tab 带 \x1b[038;2;248;081;073m红 x\x1b[0m 关闭按钮（悬停红底）",
    "  - 编辑器 (nano/vim) 用 alt screen，退出后历史完整保留",
    "  - PgUp / PgDn / 滚轮可滚动本帮助与终端历史",
    "  - 按任意其它键返回",
};
static const int g_help_tail_count = (int)(sizeof(g_help_tail) / sizeof(g_help_tail[0]));

/* 把 "Ctrl+B c" 拆成前缀段与按键段分别着色 */
static const char *help_shortcut_line(int idx, char *buf, int buf_size) {
    const HelpShortcut *hs = &g_help_shortcuts[idx];
    char combo[48] = {0};
    keymap_describe(hs->action, combo, sizeof(combo));
    if (!combo[0]) {
        char pfx[32];
        keymap_prefix_describe(pfx, sizeof(pfx));
        snprintf(combo, sizeof(combo), "%s -", pfx);
    }

    char prefix[32] = {0}, key[32] = {0};
    const char *sp = strrchr(combo, ' ');
    /* v1.8.7: 被设为「直接键」的动作 combo 里没有前缀段，左列改标注「直接」。 */
    if (!keymap_action_uses_prefix(hs->action)) {
        snprintf(prefix, sizeof(prefix), "%s", "直接");
        snprintf(key, sizeof(key), "%s", combo);
    } else if (sp) {
        int plen = (int)(sp - combo);
        if (plen > (int)sizeof(prefix) - 1) plen = (int)sizeof(prefix) - 1;
        memcpy(prefix, combo, plen);
        snprintf(key, sizeof(key), "%s", sp + 1);
    } else {
        snprintf(prefix, sizeof(prefix), "%s", combo);
    }
    if (hs->action == ACT_SELECT_PANE && hs->extra) snprintf(key, sizeof(key), "%s", hs->extra);

    int kw = (int)strlen(key);
    int pad = kw < 9 ? 9 - kw : 1;
    char keycol[64];
    snprintf(keycol, sizeof(keycol), "%s%*s", key, pad, "");

    if (hs->extra && hs->action != ACT_SELECT_PANE) {
        snprintf(buf, buf_size,
                 "  \x1b[038;2;210;153;034m%s\x1b[0m + \x1b[038;2;230;237;243m%s\x1b[0m%s (%s)",
                 prefix, keycol, hs->desc, hs->extra);
    } else {
        snprintf(buf, buf_size,
                 "  \x1b[038;2;210;153;034m%s\x1b[0m + \x1b[038;2;230;237;243m%s\x1b[0m%s",
                 prefix, keycol, hs->desc);
    }
    return buf;
}

static int help_total_lines(void) {
    return g_help_head_count + g_help_shortcut_count + g_help_tail_count;
}

static const char *help_line_at(int idx, char *buf, int buf_size) {
    if (idx < 0) return "";
    if (idx < g_help_head_count) return g_help_head[idx];
    idx -= g_help_head_count;
    if (idx < g_help_shortcut_count) return help_shortcut_line(idx, buf, buf_size);
    idx -= g_help_shortcut_count;
    if (idx < g_help_tail_count) return g_help_tail[idx];
    return "";
}

void render_help_content(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_cols;
    int pos = *posp;
    int vis = host_rows;
    int line_count = help_total_lines();
    int max_sc = line_count - vis;
    if (max_sc < 0) max_sc = 0;
    if (g_mux.help_scroll > max_sc) g_mux.help_scroll = max_sc;
    if (g_mux.help_scroll < 0) g_mux.help_scroll = 0;
    for (int r = 0; r < vis; r++) {
        int li = g_mux.help_scroll + r;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[K", r + 2);
        if (li < line_count) {
            char linebuf[256];
            pos += snprintf(out + pos, bs - pos, "%s", help_line_at(li, linebuf, sizeof(linebuf)));
        }
    }
    *posp = pos;
}

static char *g_render_buf = NULL;
static int g_render_buf_cap = 0;

/* v1.8.12 脏区渲染：上一帧影子。整帧仍照常生成（绝对光标定位），
 * 输出前按行与影子比对，没变的行不发，省掉绝大部分字节。 */
static FrameDiff g_frame_diff;
static char *g_diff_buf = NULL;
static size_t g_diff_buf_cap = 0;

static char *render_buffer_acquire(int needed) {
    if (needed <= 0) return NULL;
    if (g_render_buf_cap >= needed) return g_render_buf;
    int cap = g_render_buf_cap > 0 ? g_render_buf_cap : 16384;
    while (cap < needed) {
        if (cap > 0x3FFFFFFF) { cap = needed; break; }
        cap *= 2;
    }
    char *next = (char *)realloc(g_render_buf, (size_t)cap);
    if (!next) return NULL;
    g_render_buf = next;
    g_render_buf_cap = cap;
    return g_render_buf;
}

static const struct {
    unsigned char thumb_r, thumb_g, thumb_b;
    unsigned char track_bg_r, track_bg_g, track_bg_b;
    unsigned char track_fg_r, track_fg_g, track_fg_b;
} g_sb_grad[11] = {
    { 220, 230, 245,  33, 38, 45,  139, 148, 158 },
    { 190, 205, 225,  31, 36, 43,  125, 134, 144 },
    { 160, 178, 200,  28, 33, 40,  110, 118, 128 },
    { 130, 150, 175,  26, 30, 36,   95, 102, 112 },
    { 105, 125, 150,  23, 27, 33,   80,  88,  96 },
    {  85, 102, 125,  20, 24, 29,   66,  72,  80 },
    {  68,  82, 102,  17, 21, 26,   52,  58,  65 },
    {  55,  66,  82,  15, 18, 22,   40,  45,  52 },
    {  45,  54,  66,  13, 16, 19,   30,  35,  42 },
    {  38,  44,  52,  11, 14, 17,   26,  30,  38 },
    {  30,  35,  42,  10, 13, 16,   20,  24,  30 }
};

static int terminal_cursor_position(const ScreenBuffer *s, int scroll_offset,
                                     int host_rows, int host_cols, int *out_row, int *out_col) {
    if (!s || scroll_offset != 0 || !s->cursor_visible || host_rows < 1 || host_cols < 1)
        return 0;

    int rr = s->rows < host_rows ? s->rows : host_rows;
    int rc = s->cols < host_cols ? s->cols : host_cols;
    if (rr <= 0 || rc <= 0) return 0;

    int cx = s->cursor_x;
    int cy = s->cursor_y;
    /* VT auto-wrap is delayed until the next character.  During that
     * pending state the cursor still belongs to the last cell that was
     * written; moving it to the next row made the terminal output cursor
     * disappear from the rightmost cell. */
    if (cx >= rc) cx = rc - 1;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cy >= rr) cy = rr - 1;

    if (out_row) *out_row = cy + 2;
    if (out_col) *out_col = cx + 1;
    return 1;
}

/* 渲染时取屏幕第 row 行的整行 WCHAR（供选区端点整字吸附）。活屏与回滚历史
 * 都要覆盖：原来只在 vo>0 时才有 phys 行，活屏 ar=-1 会漏吸附，导致在当前
 * 屏幕上鼠标选中汉字时高亮把宽字符切成半个（看似光标停在字中间）。 */
/* 取屏幕第 row 行的单元格缓冲（供选区端点整字吸附）。返回 CHAR_INFO*（真实
 * 步长）；旧实现返回 &cells[0].Char.UnicodeChar（WCHAR*），按 2 字节步长索引
 * 会读到相邻单元格的 Attributes、列号错位。活屏 / 回滚历史 / alt 屏都覆盖。 */
static const CHAR_INFO *render_sel_line(ScreenBuffer *s, int row, int vo) {
    if (s->in_alt_screen && s->alt_buffer && row >= 0 && row < s->rows)
        return &s->alt_buffer[(size_t)row * s->cols];
    int abs_y = screen_to_abs_row(s, row, vo);
    int pr = (abs_y >= 0 && abs_y < s->total_lines)
             ? (s->scroll_top - s->hist_lines + abs_y + s->total_lines * 2) % s->total_lines : -1;
    if (pr < 0 || pr >= s->total_lines || !s->lines || !s->lines[pr].cells) return NULL;
    return s->lines[pr].cells;
}

/* 选区在某一行上的端点吸附到完整字符：块选每行两端都吸；流式选区只在
 * 首行吸左、末行吸右，中间整行不碰。宽字符（中文/全角/emoji）由此不会被
 * 切成半个高亮/半个复制。sel_active 为 0 时直接返回。 */
static void snap_sel_row(ScreenBuffer *s, int row, int vo, int sel_active, int block,
                               int cur_abs_y, int min_abs_y, int max_abs_y,
                               int *sel_min_x, int *sel_max_x) {
    if (!sel_active) return;
    const CHAR_INFO *sl = render_sel_line(s, row, vo);
    if (!sl) return;
    int at_first = (cur_abs_y == min_abs_y), at_last = (cur_abs_y == max_abs_y);
    if (block) {
        *sel_min_x = snap_left_to_char(sl, s->cols, *sel_min_x);
        *sel_max_x = snap_right_to_char(sl, s->cols, *sel_max_x);
    } else {
        if (at_first) *sel_min_x = snap_left_to_char(sl, s->cols, *sel_min_x);
        if (at_last)  *sel_max_x = snap_right_to_char(sl, s->cols, *sel_max_x);
    }
}

void render_screen(void) {
    EnterCriticalSection(&g_mux.cs);
    if (g_mux.host_cols < 1 || g_mux.host_rows < 1 || g_mux.total_host_rows < 1) { LeaveCriticalSection(&g_mux.cs); return; }
    update_host_title();

    int bs = (g_mux.host_rows + 4) * (g_mux.host_cols * 48 + 1024) + 16384;
    char *out = render_buffer_acquire(bs);
    if (!out) { LeaveCriticalSection(&g_mux.cs); return; }
    int pos = 0;

    if (g_mux.help_mode) {
        render_help_content(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
        Pane *pane = &g_mux.panes[g_mux.active_pane];
        if (pane->is_settings) {
            render_settings_panel(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
        } else {
            ScreenBuffer *s = &pane->screen;
            WORD la_attr = 0xFFFF, la_fr = 0, la_br = 0; int la_fv = -1, la_bv = -1;
            if (pane->scroll_offset > s->hist_lines) pane->scroll_offset = s->hist_lines;
            if (pane->scroll_offset < 0) pane->scroll_offset = 0;
            int vo = pane->scroll_offset, rr = s->rows < g_mux.host_rows ? s->rows : g_mux.host_rows, rc = s->cols < g_mux.host_cols ? s->cols : g_mux.host_cols;
            int show_sb = (!s->in_alt_screen && g_mux.host_cols >= 10);
            int sb_top = 0, sb_bot = 0;
            if (show_sb) {
                int hist = s->hist_lines;
                if (hist <= 0) {
                    sb_top = 0;
                    sb_bot = rr;
                } else {
                    int total = hist + rr;
                    int th = (rr * rr) / total;
                    if (th < 1) th = 1;
                    if (th >= rr) th = rr - 1;
                    int vtop = hist - vo;
                    int max_tpos = rr - th;
                    if (max_tpos <= 0) max_tpos = 1;
                    int tpos = (vtop * max_tpos + hist / 2) / hist;
                    if (tpos < 0) tpos = 0;
                    if (tpos + th > rr) tpos = rr - th;
                    sb_top = tpos;
                    sb_bot = tpos + th;
                }
            }
            int text_rc = rc;

            int popup_open = (g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode ||
                              g_mux.custom_cmd_mode || g_search_mode || g_mux.palette_mode);
            int dist = (popup_open) ? 99 : ((g_mouse_y >= 1 && g_mouse_x >= 0) ? ((g_mux.host_cols - 1) - g_mouse_x) : 99);
            if (dist < 0) dist = 0;
            int is_hover = (!popup_open && dist == 0 && (g_mouse_y >= 1 || g_sb_dragging));
            int mouse_on_thumb = 0;
            if (is_hover) {
                int my_row = g_mouse_y - 1;
                if (my_row >= sb_top && my_row < sb_bot) {
                    mouse_on_thumb = 1;
                }
                if (g_sb_dragging) {
                    mouse_on_thumb = 1;
                }
            }

            int sel_active = 0, sel_block = 0, sel_min_abs_y = 0, sel_max_abs_y = 0, sel_min_x = 0, sel_max_x = 0;
            if (g_copy_mode && g_copy_sel_active) {
                int cur_abs_y = screen_to_abs_row(s, g_copy_cy, vo);
                sel_min_abs_y = g_copy_anchor_abs_y < cur_abs_y ? g_copy_anchor_abs_y : cur_abs_y;
                sel_max_abs_y = g_copy_anchor_abs_y > cur_abs_y ? g_copy_anchor_abs_y : cur_abs_y;
                if (g_copy_block) {
                    /* Rectangular selection: the same column range on every row.
                     * 用选区端点 g_copy_end_x（键盘=主格光标；鼠标=原始点击列），
                     * snap_sel_row 再把每行两端整字吸附，光标显示列 g_copy_cx 不参与。 */
                    sel_block = 1;
                    sel_min_x = g_copy_anchor_x < g_copy_end_x ? g_copy_anchor_x : g_copy_end_x;
                    sel_max_x = g_copy_anchor_x > g_copy_end_x ? g_copy_anchor_x : g_copy_end_x;
                } else if (g_copy_anchor_abs_y == cur_abs_y) {
                    sel_min_x = g_copy_anchor_x < g_copy_end_x ? g_copy_anchor_x : g_copy_end_x;
                    sel_max_x = g_copy_anchor_x > g_copy_end_x ? g_copy_anchor_x : g_copy_end_x;
                } else if (g_copy_anchor_abs_y < cur_abs_y) {
                    sel_min_x = g_copy_anchor_x; sel_max_x = g_copy_end_x;
                } else {
                    sel_min_x = g_copy_end_x; sel_max_x = g_copy_anchor_x;
                }
                sel_active = 1;
            } else if (g_mouse_selecting) {
                sel_min_abs_y = g_mouse_sel_s_abs_y < g_mouse_sel_e_abs_y ? g_mouse_sel_s_abs_y : g_mouse_sel_e_abs_y;
                sel_max_abs_y = g_mouse_sel_s_abs_y > g_mouse_sel_e_abs_y ? g_mouse_sel_s_abs_y : g_mouse_sel_e_abs_y;
                if (g_mouse_sel_s_abs_y == g_mouse_sel_e_abs_y) {
                    sel_min_x = g_mouse_sel_sx < g_mouse_sel_ex ? g_mouse_sel_sx : g_mouse_sel_ex;
                    sel_max_x = g_mouse_sel_sx > g_mouse_sel_ex ? g_mouse_sel_sx : g_mouse_sel_ex;
                } else if (g_mouse_sel_s_abs_y < g_mouse_sel_e_abs_y) {
                    sel_min_x = g_mouse_sel_sx; sel_max_x = g_mouse_sel_ex;
                } else {
                    sel_min_x = g_mouse_sel_ex; sel_max_x = g_mouse_sel_sx;
                }
                sel_active = 1;
            }

            for (int y = 0; y < rr; y++) {
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H", y + 2);
                /* v1.8.13 脏区修复：每一行都必须自成一个 SGR 作用域。增量帧只会
                 * 发出与上一帧不同的行，此时终端里实际的 SGR 状态是「上一帧最后
                 * 发出的那一行」留下的，而不是整帧顺序里本行上一行的状态。本行
                 * 第一个 cell 以前靠「颜色和上一行末尾相同就不发 SGR」的跨行携带，
                 * 单独重发时背景/前景就会丢（colortool 色块行之后，后续变化行
                 * 不重发背景 SGR，整块背景就没了）。行首先复位，并强制本行第一个
                 * 非空 cell 重发完整颜色，保证任意一行单独发出都和整帧一致。 */
                pos += snprintf(out + pos, bs - pos, "\x1b[0m");
                la_attr = 0xFFFF; la_fr = 0; la_br = 0; la_fv = -1; la_bv = -1;
                int ar = (vo > 0 && !s->in_alt_screen) ? screen_phys_row(s, y - vo) : -1;
                int cur_cell_abs_y = screen_to_abs_row(s, y, vo);

                snap_sel_row(s, y, vo, sel_active, sel_block, cur_cell_abs_y, sel_min_abs_y, sel_max_abs_y, &sel_min_x, &sel_max_x);
                int match_lo = 0, match_hi = 0;
                if (g_search_active && g_search_match_count > 0) {
                    int lo = 0, hi = g_search_match_count;
                    while (lo < hi) {
                        int mid = (lo + hi) / 2;
                        if (g_search_matches[mid].abs_y < cur_cell_abs_y) lo = mid + 1;
                        else hi = mid;
                    }
                    match_lo = lo;
                    hi = g_search_match_count;
                    while (lo < hi) {
                        int mid = (lo + hi) / 2;
                        if (g_search_matches[mid].abs_y <= cur_cell_abs_y) lo = mid + 1;
                        else hi = mid;
                    }
                    match_hi = lo;
                }
                for (int x = 0; x < text_rc; x++) {
                    CHAR_INFO *cell = (ar >= 0) ? ((s->lines && s->lines[ar].cells) ? &s->lines[ar].cells[x] : NULL) : screen_cell(s, y, x);
                    WCHAR wc = L' '; WORD attr = 0x07;
                    if (cell) { wc = cell->Char.UnicodeChar; attr = cell->Attributes; }
                    if (wc == 0) continue;
                    WORD frgb, brgb; int fgv, bgv;
                    cell_truecolor(s, y, x, ar, &frgb, &brgb, &fgv, &bgv);

                    if (sel_active) {
                        int in_sel = 0;
                        if (sel_block) {
                            in_sel = (cur_cell_abs_y >= sel_min_abs_y && cur_cell_abs_y <= sel_max_abs_y &&
                                      x >= sel_min_x && x <= sel_max_x);
                        }
                        else if (cur_cell_abs_y > sel_min_abs_y && cur_cell_abs_y < sel_max_abs_y) in_sel = 1;
                        else if (sel_min_abs_y == sel_max_abs_y && cur_cell_abs_y == sel_min_abs_y) in_sel = (x >= sel_min_x && x <= sel_max_x);
                        else if (cur_cell_abs_y == sel_min_abs_y) in_sel = (x >= sel_min_x);
                        else if (cur_cell_abs_y == sel_max_abs_y) in_sel = (x <= sel_max_x);
                        if (in_sel) {
                            brgb = theme_role_rgb565(TH_SELECTION); bgv = 1;
                            frgb = theme_role_rgb565(TH_WHITE); fgv = 1;
                        }
                    }

                    if (match_lo < match_hi && (!sel_active || !(brgb == theme_role_rgb565(TH_SELECTION)))) {
                        for (int m = match_lo; m < match_hi; m++) {
                            if (x >= g_search_matches[m].start_x && x <= g_search_matches[m].end_x) {
                                if (m == g_search_match_cur) {
                                    brgb = theme_role_rgb565(TH_ORANGE); bgv = 1;
                                    frgb = theme_role_rgb565(TH_WHITE); fgv = 1;
                                } else {
                                    brgb = theme_role_rgb565(TH_YELLOW); bgv = 1;
                                    frgb = theme_role_rgb565(TH_BG0); fgv = 1;
                                }
                                break;
                            }
                        }
                    }

                    if (attr != la_attr || frgb != la_fr || brgb != la_br || fgv != la_fv || bgv != la_bv) {
                        const char *ul = (attr & COMMON_LVB_UNDERSCORE) ? ";4" : "";
                        if (fgv || bgv) {
                            int fr, fg2, fb; rgb565_split(frgb, &fr, &fg2, &fb);
                            int br2, bg2, bb; rgb565_split(brgb, &br2, &bg2, &bb);
                            if (fgv && bgv)
                                pos += snprintf(out + pos, bs - pos, "\x1b[0%s;38;2;%d;%d;%d;48;2;%d;%d;%dm", ul, fr, fg2, fb, br2, bg2, bb);
                            else if (fgv)
                                pos += snprintf(out + pos, bs - pos, "\x1b[0%s;38;2;%d;%d;%dm", ul, fr, fg2, fb);
                            else
                                pos += snprintf(out + pos, bs - pos, "\x1b[0%s;48;2;%d;%d;%dm", ul, br2, bg2, bb);
                        } else {
                            static const int m[8] = {0,4,2,6,1,5,3,7};
                            int fg = attr & 0x0F, bg = (attr >> 4) & 0x0F;
                            if (fg & 8) pos += snprintf(out + pos, bs - pos, "\x1b[0%s;1;%d;%dm", ul, (fg & 8) ? 90 + m[fg & 7] : 30 + m[fg & 7], (bg & 8) ? 100 + m[bg & 7] : 40 + m[bg & 7]);
                            else pos += snprintf(out + pos, bs - pos, "\x1b[0%s;%d;%dm", ul, 30 + m[fg & 7], 40 + m[bg & 7]);
                        }
                        la_attr = attr; la_fr = frgb; la_br = brgb; la_fv = fgv; la_bv = bgv;
                    }
                    if (wc >= 0xD800 && wc <= 0xDBFF && x + 1 < text_rc) {
                        CHAR_INFO *next_cell = (ar >= 0) ? ((s->lines && s->lines[ar].cells) ? &s->lines[ar].cells[x + 1] : NULL) : screen_cell(s, y, x + 1);
                        if (next_cell && next_cell->Char.UnicodeChar >= 0xDC00 && next_cell->Char.UnicodeChar <= 0xDFFF) {
                            WCHAR low = next_cell->Char.UnicodeChar;
                            unsigned int cp = 0x10000 + (((unsigned int)(wc & 0x3FF)) << 10) + (low & 0x3FF);
                            out[pos++] = (char)(0xF0 | (cp >> 18));
                            out[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                            out[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            out[pos++] = (char)(0x80 | (cp & 0x3F));
                            x++;
                            if (pos > bs - 256) break;
                            continue;
                        }
                    }
                    if (wc >= 0xD800 && wc <= 0xDFFF) {
                        out[pos++] = ' ';
                        if (pos > bs - 256) break;
                        continue;
                    }
                    if (wc < 0x80) out[pos++] = (char)wc;
                    else if (wc < 0x800) { out[pos++] = 0xC0 | (wc >> 6); out[pos++] = 0x80 | (wc & 0x3F); }
                    else { out[pos++] = 0xE0 | (wc >> 12); out[pos++] = 0x80 | ((wc >> 6) & 0x3F); out[pos++] = 0x80 | (wc & 0x3F); }
                    if (pos > bs - 256) break;
                }
                if (show_sb && dist <= 10) {
                    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH", y + 2, g_mux.host_cols);
                    int in_thumb = (y >= sb_top && y < sb_bot);
                    if (in_thumb) {
                        if (mouse_on_thumb) {
                            pos += snprintf(out + pos, bs - pos, "\x1b[0;048;2;225;235;250m \x1b[0m");
                        } else {
                            pos += snprintf(out + pos, bs - pos, "\x1b[0;48;2;%d;%d;%dm \x1b[0m",
                                            g_sb_grad[dist].thumb_r, g_sb_grad[dist].thumb_g, g_sb_grad[dist].thumb_b);
                        }
                    } else {
                        pos += snprintf(out + pos, bs - pos, "\x1b[0;48;2;%d;%d;%dm\x1b[38;2;%d;%d;%dm│\x1b[0m",
                                        g_sb_grad[dist].track_bg_r, g_sb_grad[dist].track_bg_g, g_sb_grad[dist].track_bg_b,
                                        g_sb_grad[dist].track_fg_r, g_sb_grad[dist].track_fg_g, g_sb_grad[dist].track_fg_b);
                    }
                    la_attr = 0xFFFF;
                } else if (!show_sb) {
                    if (text_rc < g_mux.host_cols) pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[K");
                }
                if (pos > bs - 256) break;
            }

            render_status_badge(out, bs, &pos, g_mux.host_cols);

            for (int y = rr; y < g_mux.host_rows && pos < bs - 64; y++)
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[0m\x1b[K", y + 2);
        }
    } else {
        for (int y = 0; y < g_mux.host_rows && pos < bs - 64; y++)
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[0m\x1b[K", y + 2);
    }

    if (g_mux.confirm_exit_mode) {
        render_confirm_exit(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_search_mode) {
        render_search_box(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.palette_mode) {
        render_command_palette(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.chooser_mode) {
        render_chooser(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.ctx_mode == 1) {
        render_ctx_menu(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.ctx_mode == 2) {
        render_color_picker(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.rename_mode) {
        render_rename_box(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    } else if (g_mux.custom_cmd_mode) {
        render_custom_cmd_box(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
    }

    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[1;1H");
    draw_tab_bar(out, bs, &pos);

    if (g_hover_preview_pane >= 0 && g_hover_preview_active &&
        !g_mux.chooser_mode && !g_mux.ctx_mode && !g_mux.rename_mode &&
        !g_mux.custom_cmd_mode && !g_mux.palette_mode && !g_mux.help_mode) {
        if (g_hover_preview_pane >= 0 && g_hover_preview_pane < g_mux.pane_count &&
            g_mux.panes[g_hover_preview_pane].active) {
            Pane *hp = &g_mux.panes[g_hover_preview_pane];
            const char *full_title = hp->full_title[0] ? hp->full_title :
                                     (hp->title[0] ? hp->title : "cmd");
            int tcols = utf8_cols(full_title, (int)strlen(full_title));
            if (tcols > 15) {
                int tab_col = 0;
                for (int i = 0; i < g_mux.tab_count; i++) {
                    if (g_mux.tab_info[i].pane_idx == g_hover_preview_pane) {
                        tab_col = g_mux.tab_info[i].start_col;
                        break;
                    }
                }
                int tw = tcols + 14;
                if (tw > g_mux.host_cols) tw = g_mux.host_cols;
                int left = tab_col;
                if (left + tw > g_mux.host_cols) left = g_mux.host_cols - tw;
                if (left < 0) left = 0;
                pos += snprintf(out + pos, bs - pos, "\x1b[2;%dH\x1b[048;2;033;038;045m\x1b[038;2;121;192;255;1m [完整标题] \x1b[038;2;255;255;255;1m%s \x1b[0m", left + 1, full_title);
            }
        }
    }

    /* v1.8.14 脏区修复：以下全是「光标」相关序列（定位 CUP + 显隐 ?25h/?25l），
     * 它们是整帧的全局尾部，不属于任何一行的内容。若让 framediff 按行切片，
     * 帧尾这个 CUP 会把光标序列折进「光标所在行」的 chunk——当那一行内容没变
     * （别处正在刷输出、或滚动隐藏/回底重现）时整段不发，光标定位被吞掉，终端
     * 光标就停在上一个发出的 CUP（常落在滚动条列），表现为「光标位置不对」。
     * 光标序列必须逐帧无条件发出，故记录起点，让 framediff 只对前面的 body
     * （标签栏 + 各内容行）做差分，光标段永远原样追加在增量帧末尾。 */
    int cursor_pos = pos;

    if (g_search_mode) {
        int row, left, input_col, box_w;
        search_box_layout(g_mux.host_cols, &row, &left, &input_col, &box_w);
        int scr_off = get_input_screen_offset(g_search_buf, g_search_len, g_search_pos, box_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", row, input_col + scr_off);
    } else if (g_mux.palette_mode) {
        if (g_mux.palette_page == PALETTE_PAGE_PANEL_EDITOR) {
            int top, left, input_w;
            palette_editor_geom(g_mux.host_rows, g_mux.host_cols, &top, &left, NULL, NULL, &input_w);
            char *buf = NULL;
            int len = 0, text_pos = 0;
            if (g_mux.palette_field == 0) {
                buf = g_edit_name; len = g_edit_name_len; text_pos = g_edit_name_pos;
            } else if (g_mux.palette_field == 1) {
                buf = g_edit_cmd; len = g_edit_cmd_len; text_pos = g_edit_cmd_pos;
            } else if (g_mux.palette_field == 2) {
                buf = g_edit_dir; len = g_edit_dir_len; text_pos = g_edit_dir_pos;
            }
            if (!buf) {
                /* v1.8.9: 颜色选择行没有输入框，藏光标。 */
                pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
            } else {
                int scr_off = get_input_screen_offset(buf, len, text_pos, input_w);
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h",
                                top + 2 + g_mux.palette_field * 2, left + 2 + scr_off);
            }
        } else if (g_mux.palette_focus == PALETTE_FOCUS_INPUT) {
            int top, left, pw, ph;
            palette_geom(g_mux.host_rows, g_mux.host_cols, &top, &left, &pw, &ph);
            int input_w = pw - 6;
            if (input_w < 8) input_w = 8;
            int scr_off = get_input_screen_offset(g_mux.palette_query, g_mux.palette_query_len, g_mux.palette_query_pos, input_w);
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", top + 1, left + 4 + scr_off);
        } else {
            pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
        }
    } else if (g_copy_mode) {
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", g_copy_cy + 2, g_copy_cx + 1);
    } else if (g_mux.rename_mode) {
        int r_top = 2;
        int r_anchor0 = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
        int r_left = popup_left_1based(r_anchor0, RENAME_W, g_mux.host_cols);
        int scr_off = get_input_screen_offset(g_mux.rename_buf, g_mux.rename_len, g_mux.rename_pos, RENAME_W - 3);
        int cx = r_left + 2 + scr_off;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", r_top + 1, cx);
    } else if (g_mux.custom_cmd_mode) {
        int c_top = 2;
        int c_anchor0 = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
        int c_left = popup_left_1based(c_anchor0, CMD_BOX_W, g_mux.host_cols);
        int scr_off = get_input_screen_offset(g_mux.custom_cmd_buf, g_mux.custom_cmd_len, g_mux.custom_cmd_pos, CMD_BOX_W - 3);
        int cx = c_left + 2 + scr_off;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", c_top + 1, cx);
    } else if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active && g_mux.panes[g_mux.active_pane].is_settings) {
        /* 只有菜单项详情页（文本输入）与颜色十六进制编辑才显示光标；
         * 外观 / 键位 / 行为页没有输入框，必须把光标藏起来，
         * 否则会留下一个位置错乱的闪烁光标。 */
        if (g_hex_edit_active && !g_settings_show_presets) {
            int sb_w = SETTINGS_SIDEBAR_W;
            if (sb_w > g_mux.host_cols / 2) sb_w = g_mux.host_cols / 2;
            if (sb_w < 15) sb_w = 15;
            if (sb_w > g_mux.host_cols) sb_w = g_mux.host_cols;
            if (sb_w < 1) sb_w = 1;
            int main_left = sb_w + 3;
            int role_col = settings_role_col(main_left, g_hex_edit_role);
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h",
                            settings_role_row(g_hex_edit_role),
                            role_col + 21 + g_hex_edit_len);
        } else if (g_settings_nav >= 1 && g_settings_nav <= g_chooser_item_count && !g_settings_show_presets) {
            int sb_w = SETTINGS_SIDEBAR_W;
            if (sb_w > g_mux.host_cols / 2) sb_w = g_mux.host_cols / 2;
            if (sb_w < 15) sb_w = 15;
            if (sb_w > g_mux.host_cols) sb_w = g_mux.host_cols;
            if (sb_w < 1) sb_w = 1;
            int main_left = sb_w + 3;
            int right_max_w = g_mux.host_cols - main_left - 2;
            if (right_max_w < 10) right_max_w = 10;
            int input_w = right_max_w - 4;
            if (input_w > 50) input_w = 50;
            if (input_w < 20) input_w = 20;

            if (g_settings_field == 0) {
                int scr_off = get_input_screen_offset(g_edit_name, g_edit_name_len, g_edit_name_pos, input_w);
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", 6, main_left + 2 + scr_off);
            } else if (g_settings_field == 1) {
                int scr_off = get_input_screen_offset(g_edit_cmd, g_edit_cmd_len, g_edit_cmd_pos, input_w);
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", 9, main_left + 2 + scr_off);
            } else if (g_settings_field == 2) {
                int scr_off = get_input_screen_offset(g_edit_dir, g_edit_dir_len, g_edit_dir_pos, input_w);
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", 12, main_left + 2 + scr_off);
            } else {
                /* v1.8.9: 颜色选择行不是输入框，别留下闪烁光标。 */
                pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
            }
        } else {
            pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
        }
    } else if (g_mux.chooser_mode || g_mux.ctx_mode || g_mux.help_mode) {
        pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
    } else if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
        Pane *pane = &g_mux.panes[g_mux.active_pane];
        ScreenBuffer *s = &pane->screen;
        int vo = pane->scroll_offset;
        int cursor_row = 0, cursor_col = 0;
        if (terminal_cursor_position(s, vo, g_mux.host_rows, g_mux.host_cols,
                                     &cursor_row, &cursor_col)) {
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", cursor_row, cursor_col);
        } else {
            pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
        }
    } else {
        pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
    }

    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active)
        dump_render_output(out, pos, g_mux.panes[g_mux.active_pane].screen.cols, g_mux.panes[g_mux.active_pane].screen.rows, g_mux.host_cols, g_mux.host_rows);
    g_mux.needs_redraw = 0;
    LeaveCriticalSection(&g_mux.cs);

    theme_remap(out, pos);

    /* 脏区输出：整帧按 CUP 切成逐行字节，只发与上一帧不同的行。
     * host 尺寸变化会让 begin_frame 检测到行数不一致并强制整帧重发。
     * 注意 framediff 状态只在本线程（渲染循环）访问，放在锁外即可。
     * v1.8.14：只对 body（cursor_pos 之前：标签栏 + 各内容行 + 弹层）做行差分；
     * cursor_pos 之后是光标段（定位 + 显隐），逐帧无条件追加到增量末尾——否则它会
     * 被末尾 CUP 折进光标所在行的 chunk，那一行内容没变时整段被跳过，光标丢失。 */
    size_t cursor_len = (size_t)(pos - cursor_pos);
    framediff_begin_frame(&g_frame_diff, g_mux.total_host_rows);
    framediff_scan(&g_frame_diff, out, (size_t)cursor_pos);
    size_t dlen = framediff_emit(&g_frame_diff, NULL, 0) + cursor_len;
    if (dlen < (size_t)pos) {
        /* 增量路径：复用常驻缓冲，先写差分 body，再原样追加光标段。 */
        if (dlen + 1 > g_diff_buf_cap) {
            size_t cap = g_diff_buf_cap > 0 ? g_diff_buf_cap : 8192;
            while (cap < dlen + 1) cap *= 2;
            char *nb = (char *)realloc(g_diff_buf, cap);
            if (nb) { g_diff_buf = nb; g_diff_buf_cap = cap; }
        }
        if (g_diff_buf && dlen + 1 <= g_diff_buf_cap) {
            size_t blen = framediff_emit(&g_frame_diff, g_diff_buf, g_diff_buf_cap);
            if (blen + cursor_len <= g_diff_buf_cap)
                memcpy(g_diff_buf + blen, out + cursor_pos, cursor_len);
            dump_delta_output(g_diff_buf, (int)dlen, (int)pos);
            host_write(g_diff_buf, (int)dlen);
        } else {
            dump_delta_output(out, pos, (int)pos);
            host_write(out, pos);
        }
    } else {
        /* 增量没省到（或分配失败）：发整帧。 */
        dump_delta_output(out, pos, (int)pos);
        host_write(out, pos);
    }
}

void render_cleanup(void) {
    if (g_render_buf) {
        free(g_render_buf);
        g_render_buf = NULL;
        g_render_buf_cap = 0;
    }
    framediff_free(&g_frame_diff);
}
