#include "render.h"

#define TB_BG        "\x1b[48;2;22;27;34m"
#define TAB_IN_BG    "\x1b[48;2;33;38;45m"
#define TAB_IN_FG    "\x1b[38;2;139;148;158m"
#define TAB_ACT_BG   "\x1b[48;2;31;111;235m"
#define TAB_ACT_FG   "\x1b[38;2;255;255;255m"
#define BRAND_BG     "\x1b[48;2;137;87;229m"
#define BRAND_BG_HV  "\x1b[48;2;163;113;247m"
#define X_RED        "\x1b[38;2;248;81;73m"
#define X_RED_BG     "\x1b[48;2;248;81;73m"
#define PLUS_GREEN   "\x1b[38;2;63;185;80m"
#define PLUS_GREEN_BG "\x1b[48;2;63;185;80m"
#define DARK_FG      "\x1b[38;2;13;17;23m"

static const char *const TAB_COLOR_BG[9] = {
    "\x1b[48;2;31;111;235m",   // 0 default: blue
    "\x1b[48;2;31;111;235m",   // 1 blue (default highlight)
    "\x1b[48;2;63;185;80m",    // 2 green
    "\x1b[48;2;210;153;34m",   // 3 amber
    "\x1b[48;2;137;87;229m",   // 4 purple
    "\x1b[48;2;31;136;61m",    // 5 teal/green-dark
    "\x1b[48;2;121;192;255m",  // 6 light blue
    "\x1b[48;2;217;119;54m",   // 7 orange
    "\x1b[48;2;205;93;173m",   // 8 pink
};

static const char *const TAB_COLOR_BG_DIM[9] = {
    "\x1b[48;2;22;62;128m",
    "\x1b[48;2;22;62;128m",   // 1 blue dim
    "\x1b[48;2;36;99;49m",
    "\x1b[48;2;110;82;30m",
    "\x1b[48;2;74;48;122m",
    "\x1b[48;2;24;80;48m",
    "\x1b[48;2;52;96;128m",
    "\x1b[48;2;112;66;34m",
    "\x1b[48;2;104;50;90m",
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
    int popup_open = (g_mux.settings_mode || g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode || g_mux.custom_cmd_mode || g_search_mode);
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
            pos += snprintf(out + pos, bs - pos, "%s\x1b[38;2;139;148;158m%s", dimbg, head);
        if (hovering)
            pos += snprintf(out + pos, bs - pos, X_RED_BG "\x1b[38;2;255;255;255m\xc3\x97");
        else
            pos += snprintf(out + pos, bs - pos, X_RED "\xc3\x97");
        if (act)
            pos += snprintf(out + pos, bs - pos, "%s" TAB_ACT_FG "]", actbg);
        else
            pos += snprintf(out + pos, bs - pos, "%s\x1b[38;2;139;148;158m]", dimbg);
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
            pos += snprintf(out + pos, bs - pos, "\x1b[48;2;121;192;255m\x1b[38;2;13;17;23;1m[*]\x1b[0m");
        else
            pos += snprintf(out + pos, bs - pos, TAB_IN_BG "\x1b[38;2;121;192;255m[*]\x1b[0m");
        col += 3;
        g_mux.tab_count++;
    }
    pos += snprintf(out + pos, bs - pos, TB_BG);
    while (col < g_mux.host_cols && pos < bs - 4) { out[pos++] = ' '; col++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m");
    *posp = pos;
}

void chooser_geom(int host_rows, int host_cols, int *top, int *left, int *w, int *h) {
    (void)host_rows;
    int mcw = 0;
    for (int i = 0; i < g_chooser_item_count; i++) {
        int w15 = utf8_cols(g_chooser_items[i].name, (int)strlen(g_chooser_items[i].name));
        if (w15 > 15) w15 = 15;
        if (w15 > mcw) mcw = w15;
    }
    int cw = 1 + 2 + 3 + 1 + mcw + 2;
    if (cw < 20) cw = 20;
    if (cw > host_cols) cw = host_cols;
    int ch = g_chooser_item_count + 4;
    if (w) *w = cw;
    if (h) *h = ch;
    *top = 2;
    *left = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    if (*left + cw > host_cols) *left = (g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x) - cw;
    if (*left < 0) *left = 0;
}

void render_chooser(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int top, left, cw, ch;
    chooser_geom(host_rows, host_cols, &top, &left, &cw, &ch);
    int pos = *posp;

    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 新建 pane ", top, left);
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
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m[%d]\x1b[0m \x1b[38;2;230;237;243m%s\x1b[0m",
                        r, left, i + 1, disp_name);
        int item_used = 1 + 2 + 3 + 1 + utf8_cols(disp_name, (int)strlen(disp_name));
        while (item_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; item_used++; }
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");
    }

    int about_r = top + 1 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;217;119;54;1m[A]\x1b[0m \x1b[38;2;217;119;54m关于 (About)\x1b[0m", about_r, left);
    int about_used = 1 + 2 + 3 + 1 + 12;
    while (about_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; about_used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");

    int esc_r = top + 2 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;139;148;158mEsc 取消\x1b[0m", esc_r, left);
    int esc_used = 1 + 2 + 8;
    while (esc_used < cw - 1 && pos < bs - 8) { out[pos++] = ' '; esc_used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");

    int bot_r = top + 3 + g_chooser_item_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m└", bot_r, left);
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
    int left = ax;
    if (left + CMD_BOX_W > host_cols) left = ax - CMD_BOX_W;
    if (left < 0) left = 0;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 自定义命令行 ──────────────────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m ", top + 1, left);
    render_scrollable_input(out, bs, &pos, g_mux.custom_cmd_buf, g_mux.custom_cmd_len, g_mux.custom_cmd_pos, CMD_BOX_W - 3, NULL, NULL);
    pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;139;148;158m[Enter=启动  Esc=取消]              \x1b[0m\x1b[48;2;33;38;45m│\x1b[0m", top + 2, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m└────────────────────────────────────┘\x1b[0m", top + 3, left);
    *posp = pos;
}

void render_rename_box(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = ax;
    if (left + RENAME_W > host_cols) left = ax - RENAME_W;
    if (left < 0) left = 0;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 重命名标签 ───────────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m ", top + 1, left);
    render_scrollable_input(out, bs, &pos, g_mux.rename_buf, g_mux.rename_len, g_mux.rename_pos, RENAME_W - 3, "", NULL);
    pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m└────────────────────────────┘\x1b[0m", top + 2, left);
    *posp = pos;
}

void render_ctx_menu(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = ax;
    if (left + CTX_W > host_cols) left = ax - CTX_W;
    if (left < 0) left = 0;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 标签操作 ───────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m[1]\x1b[0m \x1b[38;2;230;237;243m修改颜色        \x1b[0m\x1b[48;2;33;38;45m│\x1b[0m", top + 1, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m  \x1b[38;2;210;153;34m[2]\x1b[0m \x1b[38;2;230;237;243m重命名标签      \x1b[0m\x1b[48;2;33;38;45m│\x1b[0m", top + 2, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m└──────────────────────┘\x1b[0m", top + 3, left);
    *posp = pos;
}

void render_color_picker(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_rows;
    int pos = *posp;
    int top = 2;
    int ax = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
    int left = ax;
    if (left + CP_W > host_cols) left = ax - CP_W;
    if (left < 0) left = 0;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m┌─ 选择颜色 ─────────────────┐\x1b[0m", top, left);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m ", top + 1, left);
    for (int i = 1; i <= 4; i++) {
        int h = (g_mouse_y == top && g_mouse_x >= left + 2 + (i-1)*4 && g_mouse_x < left + 2 + i*4);
        pos += snprintf(out + pos, bs - pos, "%s  \x1b[38;2;%s;1m%d\x1b[0m%s ",
                        TAB_COLOR_BG[i], h ? "255;255;255" : "13;17;23", i, TAB_COLOR_BG[i]);
    }
    pos += snprintf(out + pos, bs - pos, " \x1b[48;2;33;38;45m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m ", top + 2, left);
    for (int i = 5; i <= 8; i++) {
        int h = (g_mouse_y == top + 1 && g_mouse_x >= left + 2 + (i-5)*4 && g_mouse_x < left + 2 + (i-4)*4);
        pos += snprintf(out + pos, bs - pos, "%s  \x1b[38;2;%s;1m%d\x1b[0m%s ",
                        TAB_COLOR_BG[i], h ? "255;255;255" : "13;17;23", i, TAB_COLOR_BG[i]);
    }
    pos += snprintf(out + pos, bs - pos, " \x1b[48;2;33;38;45m│\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m└────────────────────────────┘\x1b[0m", top + 3, left);
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
    *left = (host_cols - pw) / 2;
    if (*left < 0) *left = 0;
}

void render_settings_presets(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int top, left, pw, ph, mnw, mcw;
    presets_geom(host_rows, host_cols, &top, &left, &pw, &ph, &mnw, &mcw);
    int pos = *posp;

    const char *hdr_text = "┌─ 常用命令行预设 (按数字/回车选择) ";
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;255;255;255m\x1b[48;2;31;136;61;1m%s", top, left, hdr_text);
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
        const char *bg = (row_hover || is_sel) ? "\x1b[48;2;45;55;72m" : "\x1b[48;2;22;27;34m";
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m%s  \x1b[38;2;210;153;34m[%d]\x1b[0m%s \x1b[38;2;230;237;243;1m",
                        r, left, bg, i + 1, bg);
        cols = 1 + 2 + 4;
        append_padded_utf8(out, bs, &pos, &cols, g_presets[i].name, mnw);
        pos += snprintf(out + pos, bs - pos, "%s \x1b[38;2;139;148;158m", bg);
        cols += 1;
        append_padded_utf8(out, bs, &pos, &cols, g_presets[i].cmd, mcw);
        pos += snprintf(out + pos, bs - pos, "%s", bg);
        pad_to_right_border(out, bs, &pos, &cols, pw);
    }

    int esc_r = top + 1 + g_preset_count;
    int h_esc = (g_mouse_y == esc_r - 1 && g_mouse_x >= left - 1 + 2 && g_mouse_x <= left - 1 + 14);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m│\x1b[0m\x1b[48;2;22;27;34m  ", esc_r, left);
    cols = 1 + 2;
    if (h_esc)
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;217;119;54m\x1b[38;2;255;255;255;1m [Esc] 取消 \x1b[0m\x1b[48;2;22;27;34m");
    else
        pos += snprintf(out + pos, bs - pos, "\x1b[48;2;33;38;45m\x1b[38;2;139;148;158m [Esc] 取消 \x1b[0m\x1b[48;2;22;27;34m");
    cols += 12;
    pad_to_right_border(out, bs, &pos, &cols, pw);

    int bot_r = top + 2 + g_preset_count;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[48;2;33;38;45m└", bot_r, left);
    cols = 1;
    while (cols < pw - 1 && pos < bs - 8) {
        out[pos++] = '\xe2'; out[pos++] = '\x94'; out[pos++] = '\x80';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "┘\x1b[0m");
    *posp = pos;
}

void render_settings_panel(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int pos = *posp;
    int sb_w = SETTINGS_SIDEBAR_W;
    if (sb_w > host_cols / 2) sb_w = host_cols / 2;
    if (sb_w < 15) sb_w = 15;

    for (int y = 0; y < host_rows; y++) {
        int r = y + 2;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[0m\x1b[K", r);
    }

    pos += snprintf(out + pos, bs - pos, "\x1b[2;1H\x1b[48;2;121;192;255m\x1b[38;2;13;17;23;1m  *  termux - 设置面板 (Settings Panel)");
    int hdr_used = utf8_cols("  *  termux - 设置面板 (Settings Panel)", (int)strlen("  *  termux - 设置面板 (Settings Panel)"));
    while (hdr_used < host_cols && pos < bs - 8) { out[pos++] = ' '; hdr_used++; }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m");

    for (int y = 1; y < host_rows; y++) {
        int r = y + 2;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;48;54;61m│\x1b[0m", r, sb_w);
    }

    pos += snprintf(out + pos, bs - pos, "\x1b[3;1H\x1b[38;2;121;192;255;1m  导航选项\x1b[0m");
    pos += snprintf(out + pos, bs - pos, "\x1b[4;1H\x1b[38;2;48;54;61m─────────────────────\x1b[0m");

    int is_sel0 = (g_settings_nav == 0);
    int h_start = (g_mouse_y == 4 && g_mouse_x >= 0 && g_mouse_x < sb_w);
    const char *start_style = is_sel0 ? (h_start ? "\x1b[48;2;48;75;110m\x1b[38;2;255;255;255;1m" : "\x1b[48;2;38;60;88m\x1b[38;2;121;192;255;1m")
                                      : (h_start ? "\x1b[48;2;33;38;45m\x1b[38;2;255;255;255;1m" : "\x1b[38;2;230;237;243m");
    pos += snprintf(out + pos, bs - pos, "\x1b[5;1H%s  %s 启动 (Startup)  \x1b[0m", start_style, (is_sel0 ? "▶" : " "));

    pos += snprintf(out + pos, bs - pos, "\x1b[6;1H\x1b[38;2;48;54;61m┈┈ 菜单项配置 ┈┈┈┈┈┈─\x1b[0m");

    for (int i = 0; i < g_chooser_item_count; i++) {
        int r = 7 + i;
        if (r > host_rows - 3) break;
        int is_sel = (g_settings_nav == i + 1);
        int h_item = (g_mouse_y == r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
        const char *item_style = is_sel ? (h_item ? "\x1b[48;2;48;75;110m\x1b[38;2;255;255;255;1m" : "\x1b[48;2;38;60;88m\x1b[38;2;121;192;255;1m")
                                        : (h_item ? "\x1b[48;2;33;38;45m\x1b[38;2;255;255;255;1m" : "\x1b[38;2;230;237;243m");
        char dname[32] = {0};
        format_name_display(dname, sizeof(dname), g_chooser_items[i].name);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s  %s [%d] %-10s\x1b[0m", r, item_style, is_sel ? "▶" : " ", i + 1, dname);
    }

    int add_r = 7 + g_chooser_item_count;
    if (add_r <= host_rows - 2) {
        int h_add = (g_mouse_y == add_r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s  [+] 添加新条目    \x1b[0m", add_r, h_add ? "\x1b[48;2;63;185;80m\x1b[38;2;13;17;23;1m" : "\x1b[38;2;63;185;80;1m");
    }

    int pre_r = 8 + g_chooser_item_count;
    if (pre_r <= host_rows - 2) {
        int h_pre = (g_mouse_y == pre_r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s  [P] 快速预设库    \x1b[0m", pre_r, h_pre ? "\x1b[48;2;31;136;61m\x1b[38;2;255;255;255;1m" : "\x1b[38;2;31;136;61;1m");
    }

    int save_r = host_rows;
    int h_save_btn = (g_mouse_y == save_r - 1 && g_mouse_x >= 0 && g_mouse_x < sb_w);
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H%s [Ctrl+S] 保存配置  \x1b[0m", save_r, h_save_btn ? "\x1b[48;2;63;185;80m\x1b[38;2;13;17;23;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;63;185;80;1m");

    int main_left = sb_w + 3;
    int right_max_w = host_cols - main_left - 2;
    if (right_max_w < 10) right_max_w = 10;

    if (g_settings_nav == 0) {
        pos += snprintf(out + pos, bs - pos, "\x1b[3;%dH\x1b[38;2;121;192;255;1m■ 默认启动项设置 (Default Startup Item)\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[4;%dH\x1b[38;2;139;148;158m选择每次打开 termux 窗口时默认显示的界面 (按 ←/→/Space/T/H 切换)：\x1b[0m", main_left);

        int opt0_hover = (g_mouse_y == 4 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 25);
        int opt1_hover = (g_mouse_y == 4 && g_mouse_x >= main_left + 28 && g_mouse_x < main_left + 50);

        const char *opt0_style = (g_default_startup == 0) ? (opt0_hover ? "\x1b[48;2;140;205;255m\x1b[38;2;13;17;23;1m" : "\x1b[48;2;121;192;255m\x1b[38;2;13;17;23;1m")
                                                          : (opt0_hover ? "\x1b[48;2;45;55;72m\x1b[38;2;255;255;255;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;230;237;243m");
        const char *opt1_style = (g_default_startup == 1) ? (opt1_hover ? "\x1b[48;2;140;205;255m\x1b[38;2;13;17;23;1m" : "\x1b[48;2;121;192;255m\x1b[38;2;13;17;23;1m")
                                                          : (opt1_hover ? "\x1b[48;2;45;55;72m\x1b[38;2;255;255;255;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;230;237;243m");

        pos += snprintf(out + pos, bs - pos, "\x1b[5;%dH%s [●] 默认终端 (Terminal) \x1b[0m   %s [○] 内置帮助 (Help) \x1b[0m",
                        main_left, opt0_style, opt1_style);

        pos += snprintf(out + pos, bs - pos, "\x1b[7;%dH\x1b[38;2;121;192;255;1m■ [+] 新建菜单项顺序与管理 ([+] Menu Order)\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[8;%dH\x1b[38;2;139;148;158m按 ↑/↓ 选择行，U/D 调顺序，Enter/[改] 编辑，X/[删] 移除：\x1b[0m", main_left);

        pos += snprintf(out + pos, bs - pos, "\x1b[9;%dH\x1b[38;2;121;192;255;1m   序号  显示名称        启动命令行                       操作\x1b[0m", main_left);

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

            const char *row_bg = (row_focus && !h_up && !h_dn && !h_ed && !h_del) ? "\x1b[48;2;38;50;68m" :
                                 ((row_hover && !h_up && !h_dn && !h_ed && !h_del) ? "\x1b[48;2;27;33;44m" : "");

            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH%s %s\x1b[38;2;210;153;34m[%d]\x1b[0m%s  \x1b[38;2;230;237;243;1m%-12s\x1b[0m%s  \x1b[38;2;139;148;158m%-30s\x1b[0m%s",
                            r, main_left, row_bg, (row_focus ? "▶" : " "), i + 1, row_bg, dname, row_bg, dcmd, row_bg);

            pos += snprintf(out + pos, bs - pos, "  %s[↑]\x1b[0m", h_up ? "\x1b[48;2;63;185;80m\x1b[38;2;13;17;23;1m" : "\x1b[38;2;63;185;80m");
            pos += snprintf(out + pos, bs - pos, "%s[↓]\x1b[0m", h_dn ? "\x1b[48;2;217;119;54m\x1b[38;2;13;17;23;1m" : "\x1b[38;2;217;119;54m");
            pos += snprintf(out + pos, bs - pos, "%s[改]\x1b[0m", h_ed ? "\x1b[48;2;121;192;255m\x1b[38;2;13;17;23;1m" : "\x1b[38;2;121;192;255m");
            pos += snprintf(out + pos, bs - pos, "%s[删]\x1b[0m", h_del ? "\x1b[48;2;248;81;73m\x1b[38;2;255;255;255;1m" : "\x1b[38;2;248;81;73m");
        }

        int btn_r = 10 + g_chooser_item_count + 1;
        if (btn_r <= host_rows) {
            int h_add = (g_mouse_y == btn_r - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 13);
            int h_pre = (g_mouse_y == btn_r - 1 && g_mouse_x >= main_left + 15 && g_mouse_x < main_left + 29);

            pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH", btn_r, main_left);
            pos += snprintf(out + pos, bs - pos, "%s [+] 添加条目 \x1b[0m  ", h_add ? "\x1b[48;2;63;185;80m\x1b[38;2;13;17;23;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;63;185;80;1m");
            pos += snprintf(out + pos, bs - pos, "%s [P] 快速预设 \x1b[0m", h_pre ? "\x1b[48;2;31;136;61m\x1b[38;2;255;255;255;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;31;136;61;1m");
        }

        int hint_r = btn_r + 2 <= host_rows ? btn_r + 2 : host_rows;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[38;2;139;148;158m提示: ↑/↓ 选择, Enter 编辑, U/D 调序, X 删除, + 新建, P 预设, Ctrl+S 保存, Esc 退出\x1b[0m", hint_r, main_left);
    } else {
        int item_idx = g_settings_nav - 1;
        pos += snprintf(out + pos, bs - pos, "\x1b[3;%dH\x1b[38;2;121;192;255;1m■ 菜单项详细配置: [%d] %s\x1b[0m",
                        main_left, item_idx + 1, g_chooser_items[item_idx].name);

        int input_w = right_max_w - 4;
        if (input_w > 50) input_w = 50;
        if (input_w < 20) input_w = 20;

        int f0_sel = (g_settings_field == 0);
        int f0_hover = (g_mouse_y == 5 && g_mouse_x >= main_left - 1 && g_mouse_x <= main_left + input_w + 2);
        const char *f0_bg = f0_sel ? "\x1b[48;2;38;60;88m" : (f0_hover ? "\x1b[48;2;33;38;45m" : "\x1b[48;2;22;27;34m");
        pos += snprintf(out + pos, bs - pos, "\x1b[5;%dH\x1b[38;2;230;237;243;1m1. 显示名称 (Display Name):\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[6;%dH\x1b[48;2;33;38;45m│\x1b[0m%s ", main_left, f0_bg);
        render_scrollable_input(out, bs, &pos, g_edit_name, g_edit_name_len, g_edit_name_pos, input_w, f0_bg, NULL);
        pos += snprintf(out + pos, bs - pos, " \x1b[0m\x1b[48;2;33;38;45m│\x1b[0m");

        int f1_sel = (g_settings_field == 1);
        int f1_hover = (g_mouse_y == 8 && g_mouse_x >= main_left - 1 && g_mouse_x <= main_left + input_w + 2);
        const char *f1_bg = f1_sel ? "\x1b[48;2;38;60;88m" : (f1_hover ? "\x1b[48;2;33;38;45m" : "\x1b[48;2;22;27;34m");
        pos += snprintf(out + pos, bs - pos, "\x1b[8;%dH\x1b[38;2;230;237;243;1m2. 启动命令行 (Command Line):\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[9;%dH\x1b[48;2;33;38;45m│\x1b[0m%s ", main_left, f1_bg);
        render_scrollable_input(out, bs, &pos, g_edit_cmd, g_edit_cmd_len, g_edit_cmd_pos, input_w, f1_bg, NULL);
        pos += snprintf(out + pos, bs - pos, " \x1b[0m\x1b[48;2;33;38;45m│\x1b[0m");

        int f2_sel = (g_settings_field == 2);
        int f2_hover = (g_mouse_y == 11 && g_mouse_x >= main_left - 1 && g_mouse_x <= main_left + input_w + 2);
        const char *f2_bg = f2_sel ? "\x1b[48;2;38;60;88m" : (f2_hover ? "\x1b[48;2;33;38;45m" : "\x1b[48;2;22;27;34m");
        pos += snprintf(out + pos, bs - pos, "\x1b[11;%dH\x1b[38;2;230;237;243;1m3. 启动目录 (Working Directory) \x1b[38;2;139;148;158m[留空为当前目录，支持 %%USERPROFILE%%]:\x1b[0m", main_left);
        pos += snprintf(out + pos, bs - pos, "\x1b[12;%dH\x1b[48;2;33;38;45m│\x1b[0m%s ", main_left, f2_bg);
        render_scrollable_input(out, bs, &pos, g_edit_dir, g_edit_dir_len, g_edit_dir_pos, input_w, f2_bg, NULL);
        pos += snprintf(out + pos, bs - pos, " \x1b[0m\x1b[48;2;33;38;45m│\x1b[0m");

        int act_r = 14;
        int h_apply = (g_mouse_y == act_r - 1 && g_mouse_x >= main_left - 1 && g_mouse_x < main_left + 17);
        int h_imp = (g_mouse_y == act_r - 1 && g_mouse_x >= main_left + 19 && g_mouse_x < main_left + 35);
        int h_del = (g_mouse_y == act_r - 1 && g_mouse_x >= main_left + 37 && g_mouse_x < main_left + 49);

        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH", act_r, main_left);
        pos += snprintf(out + pos, bs - pos, "%s [保存并应用此项] \x1b[0m  ", h_apply ? "\x1b[48;2;121;192;255m\x1b[38;2;13;17;23;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;121;192;255;1m");
        pos += snprintf(out + pos, bs - pos, "%s [从预设库导入] \x1b[0m  ", h_imp ? "\x1b[48;2;31;136;61m\x1b[38;2;255;255;255;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;31;136;61;1m");
        pos += snprintf(out + pos, bs - pos, "%s [删除此项] \x1b[0m", h_del ? "\x1b[48;2;248;81;73m\x1b[38;2;255;255;255;1m" : "\x1b[48;2;33;38;45m\x1b[38;2;248;81;73;1m");

        pos += snprintf(out + pos, bs - pos, "\x1b[16;%dH\x1b[38;2;139;148;158m提示: Tab 切换输入框, Enter 保存应用, Ctrl+P 导入预设, Ctrl+D 删除, Esc 返回\x1b[0m", main_left);
    }

    if (g_settings_show_presets) {
        render_settings_presets(out, bs, &pos, host_rows, host_cols);
    }

    *posp = pos;
}

void render_search_box(char *out, int bs, int *posp, int host_rows, int host_cols) {
    int pos = *posp;
    int r = host_rows + 1;
    pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[48;2;33;38;45m\x1b[38;2;121;192;255;1m [搜索] \x1b[0m\x1b[48;2;22;27;34m\x1b[38;2;255;255;255m ", r);
    int prefix_cols = 9;
    int suffix_cols = 23;
    int box_w = host_cols - prefix_cols - suffix_cols - 4;
    if (box_w < 10) box_w = 10;
    render_scrollable_input(out, bs, &pos, g_search_buf, g_search_len, g_search_pos, box_w, "\x1b[48;2;22;27;34m", NULL);
    pos += snprintf(out + pos, bs - pos, " \x1b[0m\x1b[48;2;33;38;45m\x1b[38;2;139;148;158m [Enter 查找, Esc 退出] \x1b[0m");
    int used_cols = prefix_cols + box_w + suffix_cols;
    while (used_cols < host_cols - 1 && pos < bs - 8) {
        out[pos++] = ' ';
        used_cols++;
    }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[K");
    *posp = pos;
}

static const char *const g_help_lines[] = {
    "\x1b[38;2;255;255;255m\x1b[48;2;31;111;235m termux - 帮助",
    "\x1b[38;2;139;148;158m  版本 v" TERMUX_VERSION " | Windows Terminal Multiplexer (Win10 1809+)\x1b[0m",
    "",
    "\x1b[38;2;121;192;255;1m  键盘快捷键\x1b[0m",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mc\x1b[0m         新建默认 pane",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243m+\x1b[0m         新建 pane 菜单 (选择/自定义命令行)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243m[\x1b[0m         进入复制模式 (方向键/Space选择/Enter复制)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243m/\x1b[0m         搜索滚动历史 (n/N 跳转匹配, Esc 退出)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mn / p\x1b[0m     下一个 / 上一个 pane",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mx\x1b[0m         关闭当前 pane",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243ms\x1b[0m         打开图形化设置 (termux.ini)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mr\x1b[0m         热重载配置文件 (termux.ini)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243m? / h\x1b[0m     打开 / 关闭本帮助",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243md\x1b[0m         退出 termux",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243mt\x1b[0m         轮换标签颜色 (Shift+t 反向)",
    "  \x1b[38;2;210;153;34mCtrl+B\x1b[0m + \x1b[38;2;230;237;243m0-9\x1b[0m       跳转到 pane (支持主键盘与小键盘)",
    "",
    "\x1b[38;2;121;192;255;1m  鼠标操作\x1b[0m",
    "  \x1b[38;2;230;237;243m点击 tab\x1b[0m           切换 pane",
    "  \x1b[38;2;230;237;243m点击 [x]\x1b[0m           关闭该 pane",
    "  \x1b[38;2;230;237;243m右键 tab\x1b[0m           改颜色 / 改标题",
    "  \x1b[38;2;230;237;243m点击 [+]\x1b[0m           新建 pane (支持选择/自定义命令行)",
    "  \x1b[38;2;230;237;243m点击 [*]\x1b[0m           打开图形化设置页面",
    "  \x1b[38;2;230;237;243m点击 termux\x1b[0m        打开 / 关闭本帮助",
    "  \x1b[38;2;230;237;243m鼠标左键拖选\x1b[0m       框选终端文字，松开自动复制到剪贴板",
    "",
    "\x1b[38;2;121;192;255;1m  提示与警告\x1b[0m",
    "  - \x1b[38;2;248;81;73m警告: 终端必须使用等宽字体，否则会渲染故障\x1b[0m",
    "  - 每个 tab 带 \x1b[38;2;248;81;73m红 x\x1b[0m 关闭按钮（悬停红底）",
    "  - 编辑器 (nano/vim) 用 alt screen，退出后历史完整保留",
    "  - PgUp / PgDn / 滚轮可滚动本帮助与终端历史",
    "  - 按任意其它键返回",
};
static const int g_help_line_count = (int)(sizeof(g_help_lines) / sizeof(g_help_lines[0]));

void render_help_content(char *out, int bs, int *posp, int host_rows, int host_cols) {
    (void)host_cols;
    int pos = *posp;
    int vis = host_rows;
    int max_sc = g_help_line_count - vis;
    if (max_sc < 0) max_sc = 0;
    if (g_mux.help_scroll > max_sc) g_mux.help_scroll = max_sc;
    if (g_mux.help_scroll < 0) g_mux.help_scroll = 0;
    for (int r = 0; r < vis; r++) {
        int li = g_mux.help_scroll + r;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[K", r + 2);
        if (li < g_help_line_count)
            pos += snprintf(out + pos, bs - pos, "%s", g_help_lines[li]);
    }
    *posp = pos;
}

static char *g_render_buf = NULL;
static int g_render_buf_cap = 0;

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

            int popup_open = (g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode || g_mux.custom_cmd_mode || g_search_mode);
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

            int sel_active = 0, sel_min_abs_y = 0, sel_max_abs_y = 0, sel_min_x = 0, sel_max_x = 0;
            if (g_copy_mode && g_copy_sel_active) {
                int cur_abs_y = screen_to_abs_row(s, g_copy_cy, vo);
                sel_min_abs_y = g_copy_anchor_abs_y < cur_abs_y ? g_copy_anchor_abs_y : cur_abs_y;
                sel_max_abs_y = g_copy_anchor_abs_y > cur_abs_y ? g_copy_anchor_abs_y : cur_abs_y;
                if (g_copy_anchor_abs_y == cur_abs_y) {
                    sel_min_x = g_copy_anchor_x < g_copy_cx ? g_copy_anchor_x : g_copy_cx;
                    sel_max_x = g_copy_anchor_x > g_copy_cx ? g_copy_anchor_x : g_copy_cx;
                } else if (g_copy_anchor_abs_y < cur_abs_y) {
                    sel_min_x = g_copy_anchor_x; sel_max_x = g_copy_cx;
                } else {
                    sel_min_x = g_copy_cx; sel_max_x = g_copy_anchor_x;
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
                int ar = (vo > 0 && !s->in_alt_screen) ? screen_phys_row(s, y - vo) : -1;
                int cur_cell_abs_y = screen_to_abs_row(s, y, vo);
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
                        if (cur_cell_abs_y > sel_min_abs_y && cur_cell_abs_y < sel_max_abs_y) in_sel = 1;
                        else if (sel_min_abs_y == sel_max_abs_y && cur_cell_abs_y == sel_min_abs_y) in_sel = (x >= sel_min_x && x <= sel_max_x);
                        else if (cur_cell_abs_y == sel_min_abs_y) in_sel = (x >= sel_min_x);
                        else if (cur_cell_abs_y == sel_max_abs_y) in_sel = (x <= sel_max_x);
                        if (in_sel) {
                            brgb = rgb565(38, 75, 110); bgv = 1;
                            frgb = rgb565(255, 255, 255); fgv = 1;
                        }
                    }

                    if (match_lo < match_hi && (!sel_active || !(brgb == rgb565(38, 75, 110)))) {
                        for (int m = match_lo; m < match_hi; m++) {
                            if (x >= g_search_matches[m].start_x && x <= g_search_matches[m].end_x) {
                                if (m == g_search_match_cur) {
                                    brgb = rgb565(217, 119, 54); bgv = 1;
                                    frgb = rgb565(255, 255, 255); fgv = 1;
                                } else {
                                    brgb = rgb565(210, 153, 34); bgv = 1;
                                    frgb = rgb565(13, 17, 23); fgv = 1;
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
                            pos += snprintf(out + pos, bs - pos, "\x1b[0;48;2;225;235;250m \x1b[0m");
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

            if (g_copy_mode) {
                int badge_x = (g_mux.host_cols > 65) ? (g_mux.host_cols - 60) : 1;
                pos += snprintf(out + pos, bs - pos, "\x1b[2;%dH\x1b[48;2;210;153;34m\x1b[38;2;13;17;23;1m [复制模式] \x1b[0m\x1b[48;2;33;38;45m\x1b[38;2;230;237;243m %s | Enter/y 复制, Space/v 选区, Esc 退出 \x1b[0m",
                                badge_x, g_copy_sel_active ? "已开启选区" : "移动光标");
            } else if (g_search_active && g_search_match_count > 0) {
                int badge_x = (g_mux.host_cols > 65) ? (g_mux.host_cols - 60) : 1;
                pos += snprintf(out + pos, bs - pos, "\x1b[2;%dH\x1b[48;2;210;153;34m\x1b[38;2;13;17;23;1m [搜索: \"%s\" (%d/%d)] \x1b[0m\x1b[48;2;33;38;45m\x1b[38;2;230;237;243m n 下一个, N 上一个, Esc 退出 \x1b[0m",
                                badge_x, g_search_buf, g_search_match_cur + 1, g_search_match_count);
            }

            for (int y = rr; y < g_mux.host_rows && pos < bs - 64; y++)
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[0m\x1b[K", y + 2);
        }
    } else {
        for (int y = 0; y < g_mux.host_rows && pos < bs - 64; y++)
            pos += snprintf(out + pos, bs - pos, "\x1b[%d;1H\x1b[0m\x1b[K", y + 2);
    }

    if (g_search_mode) {
        render_search_box(out, bs, &pos, g_mux.host_rows, g_mux.host_cols);
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
        !g_mux.custom_cmd_mode && !g_mux.help_mode) {
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
                pos += snprintf(out + pos, bs - pos, "\x1b[2;%dH\x1b[48;2;33;38;45m\x1b[38;2;121;192;255;1m [完整标题] \x1b[38;2;255;255;255;1m%s \x1b[0m", left + 1, full_title);
            }
        }
    }

    if (g_search_mode) {
        int prefix_cols = 9;
        int suffix_cols = 23;
        int box_w = g_mux.host_cols - prefix_cols - suffix_cols - 4;
        if (box_w < 10) box_w = 10;
        int scr_off = get_input_screen_offset(g_search_buf, g_search_len, g_search_pos, box_w);
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", g_mux.host_rows + 1, 10 + scr_off);
    } else if (g_copy_mode) {
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", g_copy_cy + 2, g_copy_cx + 1);
    } else if (g_mux.rename_mode) {
        int r_top = 2, r_left = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
        if (r_left + RENAME_W > g_mux.host_cols) r_left = (g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x) - RENAME_W;
        if (r_left < 0) r_left = 0;
        int scr_off = get_input_screen_offset(g_mux.rename_buf, g_mux.rename_len, g_mux.rename_pos, RENAME_W - 3);
        int cx = r_left + 2 + scr_off;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", r_top + 1, cx);
    } else if (g_mux.custom_cmd_mode) {
        int c_top = 2, c_left = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
        if (c_left + CMD_BOX_W > g_mux.host_cols) c_left = (g_pop_anchor_x >= 0 ? g_pop_anchor_x : g_mouse_x) - CMD_BOX_W;
        if (c_left < 0) c_left = 0;
        int scr_off = get_input_screen_offset(g_mux.custom_cmd_buf, g_mux.custom_cmd_len, g_mux.custom_cmd_pos, CMD_BOX_W - 3);
        int cx = c_left + 2 + scr_off;
        pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", c_top + 1, cx);
    } else if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active && g_mux.panes[g_mux.active_pane].is_settings) {
        if (g_settings_nav >= 1 && !g_settings_show_presets) {
            int sb_w = SETTINGS_SIDEBAR_W;
            if (sb_w > g_mux.host_cols / 2) sb_w = g_mux.host_cols / 2;
            if (sb_w < 15) sb_w = 15;
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
        int rr = s->rows < g_mux.host_rows ? s->rows : g_mux.host_rows;
        int rc = s->cols < g_mux.host_cols ? s->cols : g_mux.host_cols;
        if (vo == 0 && s->cursor_visible) {
            int cx = s->cursor_x;
            int cy = s->cursor_y;
            if (s->wraparound_pending && cy + 1 < rr) {
                cy++;
                cx = 0;
            }
            if (cy + 1 <= rr && cx + 1 <= rc) {
                pos += snprintf(out + pos, bs - pos, "\x1b[%d;%dH\x1b[?25h", cy + 2, cx + 1);
            } else {
                pos += snprintf(out + pos, bs - pos, "\x1b[?25l");
            }
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

    host_write(out, pos);
}

void render_cleanup(void) {
    if (g_render_buf) {
        free(g_render_buf);
        g_render_buf = NULL;
        g_render_buf_cap = 0;
    }
}
