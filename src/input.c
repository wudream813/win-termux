#include "input.h"
#include "cliphtml.h"
#include <ctype.h>

void do_scroll(int d) {
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return;
    Pane *p = &g_mux.panes[g_mux.active_pane];
    if (!p->active || p->screen.in_alt_screen) return;
    int mx = p->screen.hist_lines;
    if (mx <= 0) { p->scroll_offset = 0; return; }
    p->scroll_offset += d;
    if (p->scroll_offset > mx) p->scroll_offset = mx;
    if (p->scroll_offset < 0) p->scroll_offset = 0;
    g_mux.needs_redraw = 1;
}

/* 复制模式与搜索是“当前 pane 的模态”，而不是全局状态。进入时记住是谁开的，
 * 切换标签页时立刻收回，否则新标签页会继承旧标签页的选区/匹配，两个页面看起来
 * 能被同时操控。 */
void ui_modes_claim(void) { g_ui_mode_pane = g_mux.active_pane; }

void ui_modes_cancel(void) {
    if (g_ui_mode_pane >= 0 && g_ui_mode_pane < g_mux.pane_count)
        g_mux.panes[g_ui_mode_pane].scroll_offset = 0;
    g_copy_mode = 0;
    g_copy_sel_active = 0;
    g_copy_quick = 0;
    g_copy_block = 0;
    g_search_mode = 0;
    g_search_active = 0;
    g_search_match_count = 0;
    g_search_match_cur = -1;
    g_mouse_selecting = 0;
    g_ui_mode_pane = -1;
}

void ui_modes_sync_pane(void) {
    if (g_ui_mode_pane < 0) return;
    if (g_ui_mode_pane == g_mux.active_pane) return;
    ui_modes_cancel();
    g_mux.needs_redraw = 1;
}

void execute_search(void) {
    g_search_match_count = 0;
    g_search_match_cur = -1;
    if (g_search_len <= 0) {
        g_search_active = 0;
        return;
    }
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return;
    Pane *p = &g_mux.panes[g_mux.active_pane];
    if (!p->active) return;
    ScreenBuffer *s = &p->screen;

    WCHAR wquery[64] = {0};
    int wq_len = MultiByteToWideChar(CP_UTF8, 0, g_search_buf, g_search_len, wquery, 63);
    if (wq_len <= 0) {
        g_search_active = 0;
        return;
    }

    EnterCriticalSection(&g_mux.cs);
    int total_lines = s->in_alt_screen ? s->rows : (s->hist_lines + s->rows);
    WCHAR *row_chars = (WCHAR *)malloc(s->cols * sizeof(WCHAR));
    if (!row_chars) {
        LeaveCriticalSection(&g_mux.cs);
        return;
    }

    for (int abs_y = 0; abs_y < total_lines; abs_y++) {
        int rlen = s->cols;
        for (int x = 0; x < s->cols; x++) {
            CHAR_INFO *cell = NULL;
            if (s->in_alt_screen) {
                if (abs_y >= 0 && abs_y < s->rows && s->alt_buffer)
                    cell = &s->alt_buffer[abs_y * s->cols + x];
            } else {
                int ar = abs_y;
                int pr = (s->scroll_top - s->hist_lines + ar + s->total_lines * 2) % s->total_lines;
                if (pr >= 0 && pr < s->total_lines && s->lines && s->lines[pr].cells)
                    cell = &s->lines[pr].cells[x];
            }
            row_chars[x] = cell ? cell->Char.UnicodeChar : L' ';
        }

        for (int x = 0; x <= rlen - wq_len; x++) {
            int match = 1;
            for (int k = 0; k < wq_len; k++) {
                /* 锁定大小写时逐字符精确比较，否则统一折叠成小写。 */
                WCHAR c1 = g_search_case_sensitive ? row_chars[x + k] : towlower(row_chars[x + k]);
                WCHAR c2 = g_search_case_sensitive ? wquery[k] : towlower(wquery[k]);
                if (c1 != c2) {
                    match = 0;
                    break;
                }
            }
            if (match && g_search_match_count < MAX_SEARCH_MATCHES) {
                g_search_matches[g_search_match_count].abs_y = abs_y;
                g_search_matches[g_search_match_count].start_x = x;
                g_search_matches[g_search_match_count].end_x = x + wq_len - 1;
                g_search_match_count++;
            }
        }
    }
    free(row_chars);

    if (g_search_match_count > 0) {
        g_search_active = 1;
        g_search_match_cur = g_search_match_count - 1;
        int target_abs_y = g_search_matches[g_search_match_cur].abs_y;
        if (!s->in_alt_screen) {
            int vo = s->hist_lines - (target_abs_y - s->rows / 2);
            if (vo < 0) vo = 0;
            if (vo > s->hist_lines) vo = s->hist_lines;
            p->scroll_offset = vo;
        }
    } else {
        g_search_active = 0;
    }
    LeaveCriticalSection(&g_mux.cs);
}

void search_jump_next(void) {
    if (g_search_match_count <= 0 || !g_search_active) return;
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return;
    Pane *p = &g_mux.panes[g_mux.active_pane];
    ScreenBuffer *s = &p->screen;

    EnterCriticalSection(&g_mux.cs);
    if (g_search_match_count > 0 && g_search_active) {
        g_search_match_cur = (g_search_match_cur - 1 + g_search_match_count) % g_search_match_count;
        int target_abs_y = g_search_matches[g_search_match_cur].abs_y;
        if (!s->in_alt_screen) {
            int vo = s->hist_lines - (target_abs_y - s->rows / 2);
            if (vo < 0) vo = 0;
            if (vo > s->hist_lines) vo = s->hist_lines;
            p->scroll_offset = vo;
        }
    }
    LeaveCriticalSection(&g_mux.cs);
    g_mux.needs_redraw = 1;
}

void search_jump_prev(void) {
    if (g_search_match_count <= 0 || !g_search_active) return;
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return;
    Pane *p = &g_mux.panes[g_mux.active_pane];
    ScreenBuffer *s = &p->screen;

    EnterCriticalSection(&g_mux.cs);
    if (g_search_match_count > 0 && g_search_active) {
        g_search_match_cur = (g_search_match_cur + 1) % g_search_match_count;
        int target_abs_y = g_search_matches[g_search_match_cur].abs_y;
        if (!s->in_alt_screen) {
            int vo = s->hist_lines - (target_abs_y - s->rows / 2);
            if (vo < 0) vo = 0;
            if (vo > s->hist_lines) vo = s->hist_lines;
            p->scroll_offset = vo;
        }
    }
    LeaveCriticalSection(&g_mux.cs);
    g_mux.needs_redraw = 1;
}

static void palette_reset_query(void) {
    g_mux.palette_sel = 0;
    g_mux.palette_scroll = 0;
    g_mux.palette_query_len = 0;
    g_mux.palette_query_pos = 0;
    g_mux.palette_query[0] = 0;
    g_mux.palette_focus = PALETTE_FOCUS_INPUT;
}

static void palette_close(void) {
    g_mux.palette_mode = 0;
    g_mux.palette_page = PALETTE_PAGE_ROOT;
    g_mux.palette_stack_len = 0;
    g_mux.palette_sel = 0;
    g_mux.palette_scroll = 0;
    g_mux.palette_query_len = 0;
    g_mux.palette_query_pos = 0;
    g_mux.palette_query[0] = 0;
    g_mux.palette_focus = PALETTE_FOCUS_INPUT;
    g_mux.palette_field = 0;
    g_mux.palette_edit_idx = -1;
    g_mux.palette_edit_new = 0;
}

static void palette_push_page(int page) {
    if (g_mux.palette_stack_len < PALETTE_STACK_MAX) {
        PaletteViewState *saved = &g_mux.palette_stack[g_mux.palette_stack_len++];
        saved->page = g_mux.palette_page;
        saved->selection = g_mux.palette_sel;
        saved->scroll = g_mux.palette_scroll;
        saved->query_len = g_mux.palette_query_len;
        saved->query_pos = g_mux.palette_query_pos;
        saved->focus = g_mux.palette_focus;
        memcpy(saved->query, g_mux.palette_query, sizeof(saved->query));
    }
    g_mux.palette_page = page;
    palette_reset_query();
    g_mux.needs_redraw = 1;
}

static void palette_switch_domain(int page) {
    /* Operations and settings are peer pages.  Switching between them must
     * not grow the modal stack on every toggle; Esc therefore still returns
     * to the same parent (or closes the palette when it was opened directly).
     * A query/selection belongs to the old page and is intentionally reset. */
    if (page != PALETTE_PAGE_OPERATIONS && page != PALETTE_PAGE_SETTINGS) return;
    g_mux.palette_page = page;
    palette_reset_query();
    g_mux.needs_redraw = 1;
}

static void palette_pop_page(void) {
    if (g_mux.palette_stack_len > 0) {
        PaletteViewState *saved = &g_mux.palette_stack[--g_mux.palette_stack_len];
        g_mux.palette_page = saved->page;
        g_mux.palette_sel = saved->selection;
        g_mux.palette_scroll = saved->scroll;
        g_mux.palette_query_len = saved->query_len;
        g_mux.palette_query_pos = saved->query_pos;
        g_mux.palette_focus = saved->focus;
        memcpy(g_mux.palette_query, saved->query, sizeof(g_mux.palette_query));
        g_mux.palette_query[sizeof(g_mux.palette_query) - 1] = 0;
    } else {
        palette_close();
    }
    g_mux.needs_redraw = 1;
}

static void close_active_pane_and_select(void) {
    int c = g_mux.active_pane;
    if (c < 0 || c >= g_mux.pane_count || !g_mux.panes[c].active) return;
    int n = find_next_active_pane(c);
    close_pane(c);
    if (n >= 0 && n < g_mux.pane_count && g_mux.panes[n].active) {
        switch_pane(n);
        return;
    }
    for (int i = 0; i < g_mux.pane_count; i++) {
        if (g_mux.panes[i].active) {
            switch_pane(i);
            return;
        }
    }
    g_mux.active_pane = -1;
    g_mux.running = 0;
}

static void palette_open_search(void) {
    palette_close();
    g_search_mode = 1;
    ui_modes_claim();
    g_search_len = 0;
    g_search_pos = 0;
    g_search_buf[0] = 0;
    g_mux.needs_redraw = 1;
}

static void palette_open_custom_command(void) {
    palette_close();
    g_mux.ctx_mode = 0;
    g_mux.rename_mode = 0;
    g_mux.chooser_mode = 0;
    g_mux.help_mode = 0;
    g_mux.custom_cmd_mode = 1;
    g_mux.custom_cmd_len = 0;
    g_mux.custom_cmd_pos = 0;
    g_mux.custom_cmd_buf[0] = 0;
    g_pop_anchor_x = g_mux.host_cols / 2 - CMD_BOX_W / 2;
    if (g_pop_anchor_x < 0) g_pop_anchor_x = 0;
}

static void palette_open_rename(void) {
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count ||
        !g_mux.panes[g_mux.active_pane].active ||
        g_mux.panes[g_mux.active_pane].is_about || g_mux.panes[g_mux.active_pane].is_settings) {
        palette_close();
        return;
    }
    Pane *p = &g_mux.panes[g_mux.active_pane];
    palette_close();
    g_mux.ctx_pane = g_mux.active_pane;
    g_mux.rename_mode = 1;
    snprintf(g_mux.rename_buf, sizeof(g_mux.rename_buf), "%s", p->title[0] ? p->title : "cmd");
    g_mux.rename_len = (int)strlen(g_mux.rename_buf);
    g_mux.rename_pos = g_mux.rename_len;
    g_pop_anchor_x = g_mux.host_cols / 2 - RENAME_W / 2;
    if (g_pop_anchor_x < 0) g_pop_anchor_x = 0;
}

static void palette_open_color(void) {
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count ||
        !g_mux.panes[g_mux.active_pane].active ||
        g_mux.panes[g_mux.active_pane].is_about || g_mux.panes[g_mux.active_pane].is_settings) {
        palette_close();
        return;
    }
    palette_close();
    g_mux.ctx_pane = g_mux.active_pane;
    g_mux.ctx_mode = 2;
    g_pop_anchor_x = g_mux.host_cols / 2 - CP_W / 2;
    if (g_pop_anchor_x < 0) g_pop_anchor_x = 0;
}

static void palette_open_copy_mode(void) {
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count ||
        !g_mux.panes[g_mux.active_pane].active) {
        palette_close();
        return;
    }
    Pane *p = &g_mux.panes[g_mux.active_pane];
    palette_close();
    g_copy_mode = 1;
    ui_modes_claim();
    g_copy_sel_active = 0;
    g_copy_quick = 0;
    g_copy_block = 0;
    g_copy_cx = p->screen.cursor_x;
    g_copy_cy = p->screen.cursor_y;
    g_mux.needs_redraw = 1;
}

static void palette_open_panel_editor(int item_idx) {
    if (item_idx < 0 || item_idx >= g_chooser_item_count) return;
    load_item_to_editor(item_idx);
    g_mux.palette_edit_idx = item_idx;
    g_mux.palette_edit_new = 0;
    g_mux.palette_field = 0;
    palette_push_page(PALETTE_PAGE_PANEL_EDITOR);
}

static int palette_add_item_from_source(const ChooserItem *source, int preset_index) {
    if (g_chooser_item_count >= MAX_CHOOSER_ITEMS) return -1;
    int idx = g_chooser_item_count++;
    if (source) {
        snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[idx].name), "%s", source->name);
        snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[idx].cmd), "%s", source->cmd);
        snprintf(g_chooser_items[idx].workdir, sizeof(g_chooser_items[idx].workdir), "%s", source->workdir);
        g_chooser_items[idx].color = (source->color >= 1 && source->color <= 8) ? source->color : 0;
    } else if (preset_index >= 0 && preset_index < g_preset_count) {
        snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[idx].name), "%s", g_presets[preset_index].name);
        snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[idx].cmd), "%s", g_presets[preset_index].cmd);
        g_chooser_items[idx].workdir[0] = 0;
        g_chooser_items[idx].color = 0;
        if (strcmp(g_chooser_items[idx].cmd, ":custom") == 0)
            snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[idx].cmd), "cmd.exe");
    } else {
        snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[idx].name), "新 panel");
        snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[idx].cmd), "cmd.exe");
        g_chooser_items[idx].workdir[0] = 0;
        g_chooser_items[idx].color = 0;
    }
    return idx;
}

static void palette_select_terminal(int item_index) {
    if (item_index < 0 || item_index >= g_chooser_item_count) return;
    palette_close();
    int ni = create_pane_from_item(item_index);
    if (ni >= 0) switch_pane(ni);
    g_mux.needs_redraw = 1;
}

static void palette_select_panel_target(int value) {
    if (g_mux.palette_page == PALETTE_PAGE_SWITCH_PANEL) {
        palette_close();
        if (value >= 0 && value < g_mux.pane_count && g_mux.panes[value].active)
            switch_pane(value);
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_mux.palette_page == PALETTE_PAGE_ADD_PANEL) {
        int idx = palette_add_item_from_source(NULL, value);
        if (idx < 0) {
            g_mux.needs_redraw = 1;
            return;
        }
        load_item_to_editor(idx);
        g_mux.palette_edit_idx = idx;
        g_mux.palette_edit_new = 1;
        g_mux.palette_field = 0;
        palette_push_page(PALETTE_PAGE_PANEL_EDITOR);
        g_mux.needs_redraw = 1;
    }
}

void execute_palette_command(int item_index) {
    PaletteItemInfo item;
    if (!palette_item_info(g_mux.palette_page, item_index, &item)) return;

    switch (item.action) {
        case PALETTE_ACTION_OPEN_OPERATIONS:
            if (g_mux.palette_page == PALETTE_PAGE_SETTINGS)
                palette_switch_domain(PALETTE_PAGE_OPERATIONS);
            else
                palette_push_page(PALETTE_PAGE_OPERATIONS);
            break;
        case PALETTE_ACTION_OPEN_SETTINGS:
            if (g_mux.palette_page == PALETTE_PAGE_OPERATIONS)
                palette_switch_domain(PALETTE_PAGE_SETTINGS);
            else
                palette_push_page(PALETTE_PAGE_SETTINGS);
            break;
        case PALETTE_ACTION_OPEN_NEW_TERMINAL:
            palette_push_page(PALETTE_PAGE_NEW_TERMINAL);
            break;
        case PALETTE_ACTION_START_CUSTOM:
            palette_open_custom_command();
            break;
        case PALETTE_ACTION_RENAME:
            palette_open_rename();
            break;
        case PALETTE_ACTION_COLOR:
            palette_open_color();
            break;
        case PALETTE_ACTION_SEARCH:
            palette_open_search();
            break;
        case PALETTE_ACTION_SWITCH_PANEL:
            palette_push_page(PALETTE_PAGE_SWITCH_PANEL);
            break;
        case PALETTE_ACTION_COPY_MODE:
            palette_open_copy_mode();
            break;
        case PALETTE_ACTION_RELOAD:
            palette_close();
            load_config();
            g_mux.needs_redraw = 1;
            break;
        case PALETTE_ACTION_NEXT_THEME:
            palette_close();
            action_execute(ACT_NEXT_THEME, 0, 0);
            break;
        case PALETTE_ACTION_OPEN_APPEARANCE:
        case PALETTE_ACTION_OPEN_KEYS:
        case PALETTE_ACTION_OPEN_BEHAVIOR: {
            palette_close();
            g_key_capture_active = 0;
            g_hex_edit_active = 0;
            open_settings_pane();
            g_settings_nav = (item.action == PALETTE_ACTION_OPEN_APPEARANCE) ? SETTINGS_NAV_APPEARANCE
                           : (item.action == PALETTE_ACTION_OPEN_KEYS) ? SETTINGS_NAV_KEYS
                           : SETTINGS_NAV_BEHAVIOR;
            g_mux.needs_redraw = 1;
            break;
        }
        case PALETTE_ACTION_GRAPHICAL_SETTINGS:
            palette_close();
            open_settings_pane();
            g_mux.needs_redraw = 1;
            break;
        case PALETTE_ACTION_OPEN_ABOUT: {
            palette_close();
            int ni = create_about_pane();
            if (ni >= 0) switch_pane(ni);
            g_mux.needs_redraw = 1;
            break;
        }
        case PALETTE_ACTION_MENU_SETTINGS:
            palette_push_page(PALETTE_PAGE_MENU_SETTINGS);
            break;
        case PALETTE_ACTION_EDIT_PANEL:
            palette_open_panel_editor(item.value);
            break;
        case PALETTE_ACTION_CLOSE_PANEL:
            palette_close();
            close_active_pane_and_select();
            g_mux.needs_redraw = 1;
            break;
        case PALETTE_ACTION_QUIT:
            palette_close();
            /* 走统一的退出动作，confirm_on_exit 才会对命令面板同样生效 */
            action_execute(ACT_QUIT, 0, 0);
            break;
        case PALETTE_ACTION_DEFAULT_STARTUP:
            palette_push_page(PALETTE_PAGE_DEFAULT_STARTUP);
            break;
        case PALETTE_ACTION_OPEN_INI:
            palette_close();
            open_config_file();
            g_mux.needs_redraw = 1;
            break;
        case PALETTE_ACTION_ADD_PANEL:
            palette_push_page(PALETTE_PAGE_ADD_PANEL);
            break;
        case PALETTE_ACTION_SELECT_TERMINAL:
            palette_select_terminal(item.value);
            break;
        case PALETTE_ACTION_SELECT_PANEL:
            palette_select_panel_target(item.value);
            break;
        case PALETTE_ACTION_SELECT_DEFAULT:
            g_default_startup = item.value;
            save_config();
            palette_pop_page();
            break;
        default:
            break;
    }
    g_mux.needs_redraw = 1;
}

void open_command_palette(void) {
    int settings_active = (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count &&
                           g_mux.panes[g_mux.active_pane].active &&
                           g_mux.panes[g_mux.active_pane].is_settings);
    g_mux.palette_mode = 1;
    /* Keep the palette in the same domain as the page it was opened from:
     * Ctrl+B : opens the settings command panel on the graphical settings page
     * and the operations command panel everywhere else. */
    g_mux.palette_page = settings_active ? PALETTE_PAGE_SETTINGS : PALETTE_PAGE_OPERATIONS;
    g_mux.palette_stack_len = 0;
    g_settings_show_presets = 0;
    g_mux.palette_field = 0;
    g_mux.palette_edit_idx = -1;
    g_mux.palette_edit_new = 0;
    palette_reset_query();
    g_mux.needs_redraw = 1;
}

static int palette_key_char_to_utf8(WCHAR uc, char *u8) {
    if (uc >= 0xD800 && uc <= 0xDBFF) {
        g_high_surrogate = uc;
        return 0;
    }
    if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
        unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
        g_high_surrogate = 0;
        u8[0] = (char)(0xF0 | (cp >> 18));
        u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u8[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    if (uc < 0x20 && uc != 0x200D) {
        g_high_surrogate = 0;
        return 0;
    }
    g_high_surrogate = 0;
    if (uc < 0x80) { u8[0] = (char)uc; return 1; }
    if (uc < 0x800) {
        u8[0] = (char)(0xC0 | (uc >> 6));
        u8[1] = (char)(0x80 | (uc & 0x3F));
        return 2;
    }
    u8[0] = (char)(0xE0 | (uc >> 12));
    u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F));
    u8[2] = (char)(0x80 | (uc & 0x3F));
    return 3;
}

static void palette_insert_editor_text(char *buf, int *len, int *pos, int max_len, WCHAR uc) {
    char u8[8];
    int n = palette_key_char_to_utf8(uc, u8);
    if (n <= 0 || *len + n > max_len) return;
    memmove(buf + *pos + n, buf + *pos, (size_t)(*len - *pos + 1));
    memcpy(buf + *pos, u8, (size_t)n);
    *len += n;
    *pos += n;
}

static void palette_cancel_editor(void) {
    if (g_mux.palette_edit_new && g_mux.palette_edit_idx == g_chooser_item_count - 1 &&
        g_chooser_item_count > 0) {
        g_chooser_item_count--;
    }
    g_mux.palette_edit_new = 0;
    g_mux.palette_edit_idx = -1;
}

static void handle_palette_editor_key(KEY_EVENT_RECORD *ke) {
    WORD vk = ke->wVirtualKeyCode;
    DWORD ctrl = ke->dwControlKeyState;
    WCHAR uc = ke->uChar.UnicodeChar;
    BOOL is_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    BOOL is_shift = (ctrl & SHIFT_PRESSED) != 0;

    if (vk == VK_ESCAPE) {
        palette_cancel_editor();
        palette_pop_page();
        return;
    }
    if (vk == VK_RETURN || (is_ctrl && vk == 'S') || uc == 0x13) {
        if (g_mux.palette_edit_idx >= 0 && g_mux.palette_edit_idx < g_chooser_item_count) {
            save_editor_to_item(g_mux.palette_edit_idx);
        }
        g_mux.palette_edit_new = 0;
        g_mux.palette_edit_idx = -1;
        palette_pop_page();
        return;
    }
    if (vk == VK_TAB) {
        g_mux.palette_field = is_shift ? (g_mux.palette_field + 3) % 4 : (g_mux.palette_field + 1) % 4;
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_mux.palette_field == 3) {
        /* v1.8.9: 颜色行不是输入框，←/→ 或数字 0-8 直接选色。 */
        if (vk == VK_LEFT) {
            g_edit_color = (g_edit_color + 8) % 9;
        } else if (vk == VK_RIGHT) {
            g_edit_color = (g_edit_color + 1) % 9;
        } else if (uc >= '0' && uc <= '8') {
            g_edit_color = (int)(uc - '0');
        }
        g_mux.needs_redraw = 1;
        return;
    }

    char *buf = NULL;
    int *len = NULL;
    int *pos = NULL;
    int max_len = 0;
    if (g_mux.palette_field == 0) {
        buf = g_edit_name; len = &g_edit_name_len; pos = &g_edit_name_pos; max_len = (int)sizeof(g_edit_name) - 1;
    } else if (g_mux.palette_field == 1) {
        buf = g_edit_cmd; len = &g_edit_cmd_len; pos = &g_edit_cmd_pos; max_len = (int)sizeof(g_edit_cmd) - 1;
    } else {
        buf = g_edit_dir; len = &g_edit_dir_len; pos = &g_edit_dir_pos; max_len = (int)sizeof(g_edit_dir) - 1;
    }

    if (vk == VK_LEFT) {
        *pos = utf8_prev_grapheme(buf, *pos);
    } else if (vk == VK_RIGHT) {
        *pos = utf8_next_grapheme(buf, *len, *pos);
    } else if (vk == VK_HOME) {
        *pos = 0;
    } else if (vk == VK_END) {
        *pos = *len;
    } else if (vk == VK_BACK) {
        buf_backspace(buf, len, pos);
    } else if (vk == VK_DELETE) {
        buf_delete(buf, len, pos);
    } else if (uc) {
        palette_insert_editor_text(buf, len, pos, max_len, uc);
    } else {
        return;
    }
    g_mux.needs_redraw = 1;
}

static int palette_move_menu_item(const int *filtered, int count, int selected, int delta) {
    if (g_mux.palette_page != PALETTE_PAGE_MENU_SETTINGS || g_mux.palette_query_len > 0 ||
        !filtered || selected < 0 || selected >= count || delta == 0)
        return 0;

    int item_index = filtered[selected];
    int target = item_index + delta;
    if (item_index < 0 || item_index >= g_chooser_item_count || target < 0 ||
        target >= g_chooser_item_count)
        return 0;

    ChooserItem moved = g_chooser_items[item_index];
    g_chooser_items[item_index] = g_chooser_items[target];
    g_chooser_items[target] = moved;
    save_config();
    g_mux.palette_sel = selected + delta;
    if (g_mux.palette_sel < 0) g_mux.palette_sel = 0;
    if (g_mux.palette_sel >= count) g_mux.palette_sel = count - 1;
    g_mux.needs_redraw = 1;
    return 1;
}

static int palette_delete_menu_item(const int *filtered, int count, int selected) {
    if (g_mux.palette_page != PALETTE_PAGE_MENU_SETTINGS || !filtered ||
        selected < 0 || selected >= count)
        return 0;

    int item_index = filtered[selected];
    if (item_index < 0 || item_index >= g_chooser_item_count || g_chooser_item_count <= 1)
        return 0;

    for (int i = item_index; i + 1 < g_chooser_item_count; i++)
        g_chooser_items[i] = g_chooser_items[i + 1];
    g_chooser_item_count--;
    save_config();
    if (g_mux.palette_sel >= g_chooser_item_count) g_mux.palette_sel = g_chooser_item_count - 1;
    if (g_mux.palette_sel < 0) g_mux.palette_sel = 0;
    if (g_mux.palette_scroll > g_mux.palette_sel) g_mux.palette_scroll = g_mux.palette_sel;
    g_mux.needs_redraw = 1;
    return 1;
}

void handle_palette_key(KEY_EVENT_RECORD *ke) {
    if (g_mux.palette_page == PALETTE_PAGE_PANEL_EDITOR) {
        handle_palette_editor_key(ke);
        return;
    }

    WORD vk = ke->wVirtualKeyCode;
    DWORD ctrl = ke->dwControlKeyState;
    WCHAR uc = ke->uChar.UnicodeChar;
    int filtered[64];
    int count = palette_filter_cmds(g_mux.palette_page, filtered, 64, g_mux.palette_query);
    int visible = palette_visible_rows(g_mux.host_rows);
    int has_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

    if (vk == VK_TAB) {
        g_mux.palette_focus = (g_mux.palette_focus == PALETTE_FOCUS_LIST)
            ? PALETTE_FOCUS_INPUT : PALETTE_FOCUS_LIST;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_ESCAPE) {
        palette_pop_page();
        return;
    }
    if (vk == VK_RETURN) {
        if (count > 0 && g_mux.palette_sel >= 0 && g_mux.palette_sel < count)
            execute_palette_command(filtered[g_mux.palette_sel]);
        return;
    }
    /* Freshly opened palette (nothing typed yet): arrow keys, PageUp/Down and
     * the 1-9 quick-pick digits should act on the result list right away
     * instead of poking at the empty query field.  Once the user has typed a
     * filter, digits stay searchable characters and Tab is the way over. */
    if (g_mux.palette_focus == PALETTE_FOCUS_INPUT && g_mux.palette_query_len == 0) {
        int jump_to_list =
            (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
             vk == VK_LEFT || vk == VK_RIGHT ||
             ((vk == 'P' || vk == 'N') && has_ctrl) ||
             (uc >= '1' && uc <= '9') || (vk >= '1' && vk <= '9') ||
             (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9));
        if (jump_to_list) {
            g_mux.palette_focus = PALETTE_FOCUS_LIST;
            g_mux.needs_redraw = 1;
        }
    }
    if (g_mux.palette_page == PALETTE_PAGE_MENU_SETTINGS &&
        g_mux.palette_focus == PALETTE_FOCUS_LIST && has_ctrl &&
        (vk == VK_UP || vk == VK_DOWN)) {
        g_mux.palette_focus = PALETTE_FOCUS_LIST;
        palette_move_menu_item(filtered, count, g_mux.palette_sel, vk == VK_UP ? -1 : 1);
        return;
    }
    if (g_mux.palette_focus == PALETTE_FOCUS_LIST &&
        (vk == VK_UP || (vk == 'P' && has_ctrl))) {
        g_mux.palette_focus = PALETTE_FOCUS_LIST;
        if (g_mux.palette_sel > 0) g_mux.palette_sel--;
        g_mux.needs_redraw = 1;
        return;
    }
    if (g_mux.palette_focus == PALETTE_FOCUS_LIST &&
        (vk == VK_DOWN || (vk == 'N' && has_ctrl))) {
        g_mux.palette_focus = PALETTE_FOCUS_LIST;
        if (g_mux.palette_sel < count - 1) g_mux.palette_sel++;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_PRIOR && g_mux.palette_focus == PALETTE_FOCUS_LIST) {
        g_mux.palette_focus = PALETTE_FOCUS_LIST;
        g_mux.palette_sel -= visible;
        if (g_mux.palette_sel < 0) g_mux.palette_sel = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_NEXT && g_mux.palette_focus == PALETTE_FOCUS_LIST) {
        g_mux.palette_focus = PALETTE_FOCUS_LIST;
        g_mux.palette_sel += visible;
        if (g_mux.palette_sel >= count) g_mux.palette_sel = count > 0 ? count - 1 : 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_LEFT && g_mux.palette_focus == PALETTE_FOCUS_LIST &&
        !g_mux.palette_query_len && g_mux.palette_page != PALETTE_PAGE_ROOT) {
        palette_pop_page();
        return;
    }
    if (vk == VK_RIGHT && g_mux.palette_focus == PALETTE_FOCUS_LIST &&
        count > 0 && g_mux.palette_sel >= 0 && g_mux.palette_sel < count) {
        execute_palette_command(filtered[g_mux.palette_sel]);
        return;
    }
    if (g_mux.palette_page == PALETTE_PAGE_MENU_SETTINGS &&
        g_mux.palette_focus == PALETTE_FOCUS_LIST) {
        int is_delete = (uc == 'x' || uc == 'X' || vk == 'X');
        if (is_delete && (has_ctrl || !g_mux.palette_query_len)) {
            /* Plain X is the compact no-query command; Ctrl+X keeps delete
             * reachable while preserving X as a search character.  U/D are
             * never consumed here, so they remain searchable characters. */
            g_mux.palette_focus = PALETTE_FOCUS_LIST;
            palette_delete_menu_item(filtered, count, g_mux.palette_sel);
            return;
        }
    }
    if (vk == VK_BACK && g_mux.palette_focus == PALETTE_FOCUS_INPUT) {
        buf_backspace(g_mux.palette_query, &g_mux.palette_query_len, &g_mux.palette_query_pos);
        g_mux.palette_sel = 0;
        g_mux.palette_scroll = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DELETE && g_mux.palette_focus == PALETTE_FOCUS_INPUT) {
        buf_delete(g_mux.palette_query, &g_mux.palette_query_len, &g_mux.palette_query_pos);
        g_mux.palette_sel = 0;
        g_mux.palette_scroll = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_HOME && g_mux.palette_focus == PALETTE_FOCUS_INPUT) {
        g_mux.palette_query_pos = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_END && g_mux.palette_focus == PALETTE_FOCUS_INPUT) {
        g_mux.palette_query_pos = g_mux.palette_query_len;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_LEFT && g_mux.palette_focus == PALETTE_FOCUS_INPUT) {
        g_mux.palette_query_pos = utf8_prev_grapheme(g_mux.palette_query, g_mux.palette_query_pos);
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_RIGHT && g_mux.palette_focus == PALETTE_FOCUS_INPUT) {
        g_mux.palette_query_pos = utf8_next_grapheme(g_mux.palette_query, g_mux.palette_query_len, g_mux.palette_query_pos);
        g_mux.needs_redraw = 1;
        return;
    }
    if (g_mux.palette_focus == PALETTE_FOCUS_LIST &&
        ((uc >= '1' && uc <= '9') || (vk >= '1' && vk <= '9') ||
         (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9))) {
        int number = (uc >= '1' && uc <= '9') ? uc - '0' :
                     ((vk >= '1' && vk <= '9') ? vk - '0' : vk - VK_NUMPAD1 + 1);
        int row = g_mux.palette_scroll + number - 1;
        int visible_count = count - g_mux.palette_scroll;
        if (visible_count > visible) visible_count = visible;
        if (number >= 1 && number <= visible_count && row >= 0 && row < count) {
            g_mux.palette_sel = row;
            execute_palette_command(filtered[row]);
            return;
        }
        return;
    }

    if (g_mux.palette_focus != PALETTE_FOCUS_INPUT)
        return;

    /* Some Windows layouts report numpad digits through the virtual-key code
     * without a Unicode character.  In the input field they must still be
     * ordinary searchable digits; result focus handled them above. */
    if (!uc && !has_ctrl && vk >= '1' && vk <= '9')
        uc = (WCHAR)vk;
    if (!uc && !has_ctrl && vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9)
        uc = (WCHAR)('1' + (vk - VK_NUMPAD1));

    if (uc) {
        char u8[8];
        int n = palette_key_char_to_utf8(uc, u8);
        if (n > 0 && g_mux.palette_query_len + n < (int)sizeof(g_mux.palette_query) - 1) {
            buf_insert_utf8(g_mux.palette_query, &g_mux.palette_query_len, &g_mux.palette_query_pos,
                            sizeof(g_mux.palette_query) - 1, u8, n);
            g_mux.palette_sel = 0;
            g_mux.palette_scroll = 0;
            g_mux.needs_redraw = 1;
            return;
        }
    }
}

static void handle_palette_editor_mouse(MOUSE_EVENT_RECORD *me) {
    int mx = me->dwMousePosition.X;
    int my = me->dwMousePosition.Y;
    int top, left, pw, ph;
    palette_editor_geom(g_mux.host_rows, g_mux.host_cols, &top, &left, &pw, &ph, NULL);
    int r = my + 1;
    int c = mx + 1;
    int press = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED |
                FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
    int in_box = (r >= top && r < top + ph && c >= left && c < left + pw);

    if (!press || (me->dwEventFlags != 0 && me->dwEventFlags != DOUBLE_CLICK)) return;

    for (int field = 0; field < 3; field++) {
        int input_row = top + 2 + field * 2;
        if (r == input_row && c >= left + 1 && c < left + pw - 1) {
            g_mux.palette_field = field;
            g_mux.needs_redraw = 1;
            return;
        }
    }

    if (r == top + 8) {   /* v1.8.9: 颜色选择条 */
        int hit = item_color_hit(left + 1, c);
        if (hit >= 0) {
            g_mux.palette_field = 3;
            g_edit_color = hit;
            g_mux.needs_redraw = 1;
            return;
        }
        if (in_box) {
            g_mux.palette_field = 3;
            g_mux.needs_redraw = 1;
            return;
        }
    }

    if (r == top + 10 && in_box) {
        if (g_mux.palette_edit_idx >= 0 && g_mux.palette_edit_idx < g_chooser_item_count)
            save_editor_to_item(g_mux.palette_edit_idx);
        g_mux.palette_edit_new = 0;
        g_mux.palette_edit_idx = -1;
        palette_pop_page();
        return;
    }

    if (!in_box) {
        palette_cancel_editor();
        palette_pop_page();
    }
}

void handle_palette_mouse(MOUSE_EVENT_RECORD *me) {
    if (g_mux.palette_page == PALETTE_PAGE_PANEL_EDITOR) {
        handle_palette_editor_mouse(me);
        return;
    }

    int mx = me->dwMousePosition.X;
    int my = me->dwMousePosition.Y;
    int top, left, pw, ph;
    palette_geom(g_mux.host_rows, g_mux.host_cols, &top, &left, &pw, &ph);

    if (me->dwEventFlags == MOUSE_WHEELED) {
        /* v1.8.8: 滚轮只滚列表窗口，选中项跟着页面整体平移，
         * 在窗口里的相对位置保持不变；滚不动时什么都不做。 */
        int direction = (short)HIWORD(me->dwButtonState);
        int filtered[64];
        int count = palette_filter_cmds(g_mux.palette_page, filtered, 64, g_mux.palette_query);
        int visible = palette_visible_rows(g_mux.host_rows);
        int step = visible > 1 ? 2 : 1;
        int max_scroll = count > visible ? count - visible : 0;
        g_mux.palette_focus = PALETTE_FOCUS_LIST;

        int before = g_mux.palette_scroll;
        g_mux.palette_scroll += (direction > 0 ? -step : step);
        if (g_mux.palette_scroll > max_scroll) g_mux.palette_scroll = max_scroll;
        if (g_mux.palette_scroll < 0) g_mux.palette_scroll = 0;

        int moved = g_mux.palette_scroll - before;
        if (moved != 0) {
            g_mux.palette_sel += moved;
            if (g_mux.palette_sel < 0) g_mux.palette_sel = 0;
            if (count > 0 && g_mux.palette_sel > count - 1) g_mux.palette_sel = count - 1;
        }
        g_mux.needs_redraw = 1;
        return;
    }

    int r = my + 1;
    int c = mx + 1;
    int visible = palette_visible_rows(g_mux.host_rows);
    int row_start = top + 3;
    int row_end = row_start + visible;
    int in_box = (r >= top && r < top + ph && c >= left && c < left + pw);

    if (r == top + 1 && c > left && c < left + pw - 1) {
        g_mux.palette_focus = PALETTE_FOCUS_INPUT;
        g_mux.needs_redraw = 1;
        return;
    }

    if (r >= row_start && r < row_end && c >= left && c < left + pw) {
        g_mux.palette_focus = PALETTE_FOCUS_LIST;
        int filtered[64];
        int count = palette_filter_cmds(g_mux.palette_page, filtered, 64, g_mux.palette_query);
        int fi = g_mux.palette_scroll + r - row_start;
        if (fi >= 0 && fi < count) {
            g_mux.palette_sel = fi;
            g_mux.needs_redraw = 1;
            int press = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED |
                        FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
            if (press && (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK))
                execute_palette_command(filtered[fi]);
        }
        return;
    }

    if (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED |
                             FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) {
        if (!in_box) palette_close();
        else g_mux.needs_redraw = 1;
    }
}

void handle_search_key(KEY_EVENT_RECORD *ke) {
    WORD vk = ke->wVirtualKeyCode; WCHAR uc = ke->uChar.UnicodeChar;

    if (vk == VK_ESCAPE) {
        g_search_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (vk == VK_RETURN) {
        g_search_mode = 0;
        execute_search();
        g_mux.needs_redraw = 1;
        return;
    }

    if (vk == VK_LEFT) {
        g_search_pos = utf8_prev_grapheme(g_search_buf, g_search_pos);
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_RIGHT) {
        g_search_pos = utf8_next_grapheme(g_search_buf, g_search_len, g_search_pos);
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_HOME) {
        g_search_pos = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_END) {
        g_search_pos = g_search_len;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_BACK) {
        buf_backspace(g_search_buf, &g_search_len, &g_search_pos);
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DELETE) {
        buf_delete(g_search_buf, &g_search_len, &g_search_pos);
        g_mux.needs_redraw = 1;
        return;
    }

    if (uc >= 0xD800 && uc <= 0xDBFF) {
        g_high_surrogate = uc;
        return;
    }
    if (uc) {
        char u8[8] = {0}; int u8_count = 0;
        if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
            unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
            g_high_surrogate = 0;
            u8[0] = (char)(0xF0 | (cp >> 18));
            u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            u8[3] = (char)(0x80 | (cp & 0x3F));
            u8_count = 4;
        } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
            g_high_surrogate = 0;
            if (uc < 0x80) { u8[0] = (char)uc; u8_count = 1; }
            else if (uc < 0x800) { u8[0] = (char)(0xC0 | (uc >> 6)); u8[1] = (char)(0x80 | (uc & 0x3F)); u8_count = 2; }
            else { u8[0] = (char)(0xE0 | (uc >> 12)); u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F)); u8[2] = (char)(0x80 | (uc & 0x3F)); u8_count = 3; }
        }
        if (u8_count > 0 && g_search_len + u8_count < (int)sizeof(g_search_buf) - 1) {
            buf_insert_utf8(g_search_buf, &g_search_len, &g_search_pos, sizeof(g_search_buf) - 1, u8, u8_count);
            g_mux.needs_redraw = 1;
            return;
        }
    }
}

void copy_range_to_clipboard(Pane *p, int sx, int sy_abs, int ex, int ey_abs) {
    copy_selection_to_clipboard(p, sx, sy_abs, ex, ey_abs, 0);
}

/* 16 色 attr -> RGB（默认 Campbell 调色板，与 Windows Terminal 一致）。
 * attr 是 Windows 控制台属性字：低 4 位前景、高 4 位背景。
 * v1.8.16 修复：这个 nibble 是「Windows 控制台颜色位」序（红=4、蓝=1、
 * 黄=6、青=3），而 cliphtml 的 16 色调色板表是 ANSI/VT 序（红=1、蓝=4、
 * 黄=3、青=6）。两者必须先用 win_to_ansi[] 转换——否则复制 HTML 里红↔蓝、
 * 黄↔青会对调（render.c 往终端写 SGR 时早就用了同一张 m[] 表，复制这条
 * 路径漏了）。 */
static void attr_palette_rgb(WORD attr, int is_bg, int *r, int *g, int *b) {
    static const int win_to_ansi[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    int nibble = is_bg ? ((attr >> 4) & 0x0F) : (attr & 0x0F);
    int ansi = win_to_ansi[nibble & 7] | (nibble & 8);
    cliphtml_palette16(ansi, r, g, b);
}

/* 取复制光标行（复制模式 cy 可视行，受 scroll_offset 影响）的字符缓冲，
 * 供宽字符吸附/步进使用。无内容时返回 NULL。 */
static const WCHAR *copy_line_at(ScreenBuffer *s, Pane *p, int cy) {
    if (s->in_alt_screen && s->alt_buffer)
        return &s->alt_buffer[(size_t)cy * s->cols].Char.UnicodeChar;
    int ar = (p->scroll_offset > 0 && !s->in_alt_screen)
             ? screen_phys_row(s, cy - p->scroll_offset) : -1;
    if (ar >= 0 && s->lines && s->lines[ar].cells)
        return &s->lines[ar].cells[0].Char.UnicodeChar;
    return NULL;
}

/* 复制时判断某个 cell 是否为「宽字符占位格」。宽字符（中文/全角/BMP 宽符号）
 * 在屏幕上占两列：主格写字、次格写 0 占位（vt.c screen_put_cp）。复制必须跳过
 * 这个次格，否则每个汉字后多一个空格——"保留所有权利"变成"保 留 所 有 权 利"。
 * 判据：本格 ch==0，且左邻是占两列的宽字符。非 BMP emoji 的次格存的是低代理
 * （ch!=0），由 cliphtml 代理对逻辑处理，不在此列；高代理（emoji 主格）的右邻
 * 是低代理而非 0，也不会误判。 */
static int copy_cell_is_wide_spacer(WCHAR ch, WCHAR prev_ch) {
    if (ch != 0) return 0;
    if (prev_ch >= 0xD800 && prev_ch <= 0xDBFF) return 0;  /* non-BMP 高代理，次格是低代理 */
    return is_wide_cp((unsigned int)prev_ch) ? 1 : 0;
}

/* 复制成 HTML 时一行的右边界：纯文本会把行尾连续空格整段裁掉，但 HTML 里
 * “带颜色的空格”是看得见的内容——它铺着色块底色（colortool 网格里每个色块
 * 都是 '  gYw  '，末尾两个空格同样有背景色）。v1.8.17 修复：行尾空格只要
 * 带背景色（或非默认前景色）就必须保留，只裁真正透明（无底色）的行尾空白；
 * 一旦碰到无颜色的空格，它后面全是默认背景，直接停住。返回最后一个应输出的
 * cell 下标（无内容时返回 x_start-1，与旧 valid_x1 语义一致）。 */
static int cliphtml_row_right_boundary(const ClipHtmlCell *cells, int x_start, int valid_nonspace, int x_end) {
    int right = valid_nonspace;
    for (int x = valid_nonspace + 1; cells && x <= x_end; x++) {
        const ClipHtmlCell *c = &cells[x];
        if (c->ch != 0 && c->ch != (unsigned short)L' ') break;  /* 正常不会出现非空格 */
        if (!c->bg_valid && !c->fg_valid) break;                /* 透明空格：行尾空白，停 */
        right = x;                                              /* 带颜色的空格：保留 */
    }
    if (right < x_start - 1) right = x_start - 1;
    return right;
}

void copy_selection_to_clipboard(Pane *p, int sx, int sy_abs, int ex, int ey_abs, int block) {
    if (!p) return;
    ScreenBuffer *s = &p->screen;
    if (sy_abs > ey_abs || (sy_abs == ey_abs && sx > ex)) {
        int tx = sx; sx = ex; ex = tx;
        int ty = sy_abs; sy_abs = ey_abs; ey_abs = ty;
    }
    int block_x0 = sx < ex ? sx : ex;
    int block_x1 = sx > ex ? sx : ex;
    if (block && sy_abs > ey_abs) {
        int ty = sy_abs; sy_abs = ey_abs; ey_abs = ty;
    }
    int total_lines = ey_abs - sy_abs + 1;
    if (total_lines <= 0 || total_lines > s->total_lines) return;

    int max_chars = total_lines * (s->cols + 2) + 64;
    WCHAR *wbuf = (WCHAR *)malloc(max_chars * sizeof(WCHAR));
    ClipHtmlCell *row_cells = (ClipHtmlCell *)malloc((s->cols > 0 ? s->cols : 1) * sizeof(ClipHtmlCell));
    ClipHtmlBuf html;
    cliphtml_init(&html);
    int html_ok = (wbuf != NULL && row_cells != NULL);
    if (!wbuf) { free(row_cells); cliphtml_free(&html); return; }
    if (!row_cells) html_ok = 0;
    int wlen = 0;

    EnterCriticalSection(&g_mux.cs);
    if (html_ok) cliphtml_frag_begin(&html);
    int first_row = 1;
    for (int abs_y = sy_abs; abs_y <= ey_abs; abs_y++) {
        /* A block selection takes the same column window out of every row;
         * the stream selection runs from the start point to the end point. */
        int x_start = block ? block_x0 : ((abs_y == sy_abs) ? sx : 0);
        int x_end = block ? block_x1 : ((abs_y == ey_abs) ? ex : s->cols - 1);
        if (x_start < 0) x_start = 0;
        if (x_end >= s->cols) x_end = s->cols - 1;

        int pr = -1;
        if (!s->in_alt_screen) {
            int ar = abs_y;
            if (ar >= 0 && ar < s->total_lines)
                pr = (s->scroll_top - s->hist_lines + ar + s->total_lines * 2) % s->total_lines;
            if (pr < 0 || pr >= s->total_lines || !s->lines || !s->lines[pr].cells) pr = -1;
        }
        int rel_y = s->in_alt_screen ? abs_y : abs_y - s->hist_lines;

        /* 选区端点吸附到完整字符：与渲染高亮一致，宽字符（中文/全角/emoji）
         * 不被切半——首行左端点落在宽字符次格则左退一格，末行右端点落在宽字符
         * 主格则右扩一格；块选每行两端都吸附。 */
        {
            const WCHAR *sline = NULL;
            int sn = s->cols;
            if (s->in_alt_screen && s->alt_buffer)
                sline = &s->alt_buffer[(size_t)abs_y * s->cols].Char.UnicodeChar;
            else if (pr >= 0 && s->lines && s->lines[pr].cells)
                sline = &s->lines[pr].cells[0].Char.UnicodeChar;
            if (sline) {
                if (block) {
                    x_start = snap_left_to_char(sline, sn, x_start);
                    x_end = snap_right_to_char(sline, sn, x_end);
                } else {
                    if (abs_y == sy_abs) x_start = snap_left_to_char(sline, sn, x_start);
                    if (abs_y == ey_abs) x_end = snap_right_to_char(sline, sn, x_end);
                }
                if (x_start < 0) x_start = 0;
                if (x_end >= s->cols) x_end = s->cols - 1;
            }
        }

        int row_wlen_start = wlen;
        int valid_x1 = x_start - 1;  /* 行内最后一个非空格 cell（行尾空格裁掉） */
        for (int x = x_start; x <= x_end; x++) {
            CHAR_INFO *cell = NULL;
            if (s->in_alt_screen) {
                if (abs_y >= 0 && abs_y < s->rows && s->alt_buffer) cell = &s->alt_buffer[abs_y * s->cols + x];
            } else if (pr >= 0) {
                cell = &s->lines[pr].cells[x];
            }

            WCHAR ch = 0;
            WORD attr = 0x07;
            if (cell) {
                ch = cell->Char.UnicodeChar;
                attr = cell->Attributes;
            }

            /* 宽字符（中文/全角/BMP 宽符号，如"保"占两列）写入时主格写字、
             * 次格写 0 占位（vt.c screen_put_cp）。复制必须把这个占位格整体
             * 跳过，否则每个汉字后多一个空格——"保留所有权利"变成
             * "保 留 所 有 权 利"。判据：本格 ch=0 且左邻是占两列的宽字符；
             * 非 BMP emoji 的次格存的是低代理（ch!=0），由 cliphtml 代理对
             * 逻辑处理，不在此列。 */
            WCHAR prev_ch = 0;
            if (ch == 0 && x > x_start) {
                if (s->in_alt_screen) {
                    if (s->alt_buffer && abs_y >= 0 && abs_y < s->rows)
                        prev_ch = s->alt_buffer[abs_y * s->cols + (x - 1)].Char.UnicodeChar;
                } else if (pr >= 0 && s->lines[pr].cells) {
                    prev_ch = s->lines[pr].cells[x - 1].Char.UnicodeChar;
                }
            }
            int is_wide_spacer = copy_cell_is_wide_spacer(ch, prev_ch);
            if (is_wide_spacer) {
                if (html_ok) {
                    ClipHtmlCell *hc = &row_cells[x];
                    memset(hc, 0, sizeof(*hc));
                    hc->skip = 1;   /* 通知 cliphtml_frag_row 跳过此占位格 */
                }
                continue;
            }

            WCHAR text_ch = (ch != 0) ? ch : L' ';
            wbuf[wlen++] = text_ch;
            if (text_ch != L' ') valid_x1 = x;   /* 末尾连续空格整体裁掉 */

            if (html_ok) {
                ClipHtmlCell *hc = &row_cells[x];
                memset(hc, 0, sizeof(*hc));
                hc->ch = (unsigned short)text_ch;

                WORD frgb, brgb; int fgv, bgv;
                cell_truecolor(s, rel_y, x, pr, &frgb, &brgb, &fgv, &bgv);
                if (fgv) {
                    int cr, cg, cb; rgb565_split(frgb, &cr, &cg, &cb);
                    hc->fg_valid = 1; hc->r = (unsigned char)cr; hc->g = (unsigned char)cg; hc->b = (unsigned char)cb;
                } else if ((attr & 0x0F) != 0x07) {
                    /* 默认前景（灰 7）不染色，保持目标应用自身文字色；
                     * 其余 16 色按 Campbell 调色板给出。 */
                    int cr, cg, cb; attr_palette_rgb(attr, 0, &cr, &cg, &cb);
                    hc->fg_valid = 1; hc->r = (unsigned char)cr; hc->g = (unsigned char)cg; hc->b = (unsigned char)cb;
                }
                if (bgv) {
                    int cr, cg, cb; rgb565_split(brgb, &cr, &cg, &cb);
                    hc->bg_valid = 1; hc->br = (unsigned char)cr; hc->bg = (unsigned char)cg; hc->bb = (unsigned char)cb;
                } else if ((attr >> 4) & 0x0F) {
                    /* 背景 nibble 为 0（默认黑）时不染色。 */
                    int cr, cg, cb; attr_palette_rgb(attr, 1, &cr, &cg, &cb);
                    hc->bg_valid = 1; hc->br = (unsigned char)cr; hc->bg = (unsigned char)cg; hc->bb = (unsigned char)cb;
                }
                hc->bold = (attr & FOREGROUND_INTENSITY) ? 1 : 0;
                hc->underline = (attr & COMMON_LVB_UNDERSCORE) ? 1 : 0;
            }
        }
        if (block) {
            while (wlen > row_wlen_start && wbuf[wlen - 1] == L' ') wlen--;
        }
        if (abs_y < ey_abs) {
            while (wlen > row_wlen_start && wbuf[wlen - 1] == L' ') wlen--;
            wbuf[wlen++] = L'\r';
            wbuf[wlen++] = L'\n';
        }

        if (html_ok) {
            /* HTML 行右边界：只裁透明（无底色）的行尾空白；带背景/前景色的
             * 尾随空格是看得见的色块，必须保留（v1.8.17）。 */
            int html_x1 = cliphtml_row_right_boundary(row_cells, x_start, valid_x1, x_end);
            if (!first_row) cliphtml_frag_break(&html);
            first_row = 0;
            cliphtml_frag_row(&html, row_cells, x_start, html_x1);
        }
    }
    while (wlen > 0 && wbuf[wlen - 1] == L' ') wlen--;
    wbuf[wlen] = 0;

    int html_ready = 0;
    if (html_ok) html_ready = cliphtml_finalize(&html);
    LeaveCriticalSection(&g_mux.cs);

    /* 剪贴板 API 在锁外调用，不阻塞读线程。 */
    if (wlen > 0 && OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(WCHAR));
        if (hMem) {
            WCHAR *pMem = (WCHAR *)GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, wbuf, (wlen + 1) * sizeof(WCHAR));
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        if (html_ready && html.len > 0) {
            /* HTML Format 是 UTF-8 字节，额外注册一个剪贴板格式，与纯文本并存：
             * 浏览器 / Word / 邮件 / 富文本编辑器取彩色，记事本取纯文本。 */
            UINT cf = RegisterClipboardFormatA("HTML Format");
            HGLOBAL hHtml = GlobalAlloc(GMEM_MOVEABLE, html.len + 1);
            if (cf && hHtml) {
                char *pH = (char *)GlobalLock(hHtml);
                if (pH) {
                    memcpy(pH, html.data, html.len);
                    pH[html.len] = 0;
                    GlobalUnlock(hHtml);
                    SetClipboardData(cf, hHtml);
                }
            }
        }
        CloseClipboard();
    }
    cliphtml_free(&html);
    free(row_cells);
    free(wbuf);
}

static void copy_mode_leave(Pane *p) {
    g_copy_mode = 0;
    g_copy_sel_active = 0;
    g_copy_quick = 0;
    g_copy_block = 0;
    if (p) p->scroll_offset = 0;
    g_mux.needs_redraw = 1;
}

static void copy_mode_yank(Pane *p, ScreenBuffer *s) {
    if (!g_copy_sel_active) return;
    int cur_abs_y = screen_to_abs_row(s, g_copy_cy, p->scroll_offset);
    copy_selection_to_clipboard(p, g_copy_anchor_x, g_copy_anchor_abs_y,
                                g_copy_cx, cur_abs_y, g_copy_block);
}

static void copy_mode_anchor_here(Pane *p, ScreenBuffer *s) {
    g_copy_anchor_x = g_copy_cx;
    g_copy_anchor_abs_y = screen_to_abs_row(s, g_copy_cy, p->scroll_offset);
    g_copy_sel_active = 1;
}

/* Copy mode entered by clicking two points with Shift/Alt is a one-shot
 * session: Ctrl+C / Enter copy and close, Esc closes, and any other key closes
 * and is handed back to the pane (that is what the 1 return value means). */
int handle_copy_mode_key(KEY_EVENT_RECORD *ke) {
    WORD vk = ke->wVirtualKeyCode;
    WCHAR uc = ke->uChar.UnicodeChar;
    DWORD ctrl = ke->dwControlKeyState;
    int has_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    int has_shift = (ctrl & SHIFT_PRESSED) != 0;
    int has_alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) {
        copy_mode_leave(NULL);
        return 0;
    }
    Pane *p = &g_mux.panes[g_mux.active_pane];
    ScreenBuffer *s = &p->screen;

    /* Modifier keys arrive as their own key events; they must never be treated
     * as "some other key" or the quick session would close instantly. */
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_CAPITAL ||
        vk == VK_NUMLOCK || vk == VK_SCROLL || vk == VK_LWIN || vk == VK_RWIN) {
        return 0;
    }

    if (vk == VK_ESCAPE) {
        copy_mode_leave(p);
        return 0;
    }

    int is_ctrl_c = (has_ctrl && (vk == 'C' || uc == 3));
    if (is_ctrl_c || vk == VK_RETURN) {
        copy_mode_yank(p, s);
        copy_mode_leave(p);
        return 0;
    }

    int is_motion = (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
                     vk == VK_PRIOR || vk == VK_NEXT || vk == VK_HOME || vk == VK_END);

    /* Shift/Alt reshape the selection: Shift keeps the text-flow selection,
     * Alt switches to a rectangular block.  Either one starts a selection at
     * the cursor if none is open yet. */
    if (is_motion && (has_shift || has_alt)) {
        if (!g_copy_sel_active) copy_mode_anchor_here(p, s);
        g_copy_block = has_alt ? 1 : 0;
    } else if (g_copy_quick) {
        copy_mode_leave(p);
        return 1;
    }

    if (uc == 'q' || uc == 'Q') {
        copy_mode_leave(p);
        return 0;
    }

    if (uc == '/' || vk == VK_OEM_2) {
        g_search_mode = 1;
        ui_modes_claim();
        g_search_len = 0;
        g_search_pos = 0;
        g_search_buf[0] = 0;
        g_mux.needs_redraw = 1;
        return 0;
    }

    if (vk == VK_SPACE || uc == 'v' || uc == 'V') {
        g_copy_sel_active = !g_copy_sel_active;
        if (g_copy_sel_active) {
            copy_mode_anchor_here(p, s);
            g_copy_block = (uc == 'V');   /* Shift+V 直接开块选 */
        }
        g_mux.needs_redraw = 1;
        return 0;
    }

    if (uc == 'b' || uc == 'B') {
        g_copy_block = !g_copy_block;
        if (!g_copy_sel_active) copy_mode_anchor_here(p, s);
        g_mux.needs_redraw = 1;
        return 0;
    }

    if (uc == 'y' || uc == 'Y') {
        copy_mode_yank(p, s);
        copy_mode_leave(p);
        return 0;
    }

    if (vk == VK_LEFT || uc == 'h' || uc == 'H') {
        /* 左移一次跨过整个宽字符（中文/全角/emoji），不停在次格（占位）上。 */
        int vo = p->scroll_offset;
        int ar = (vo > 0 && !s->in_alt_screen) ? screen_phys_row(s, g_copy_cy - vo) : -1;
        const WCHAR *sl = (s->in_alt_screen && s->alt_buffer)
            ? &s->alt_buffer[(size_t)g_copy_cy * s->cols].Char.UnicodeChar
            : (ar >= 0 && s->lines && s->lines[ar].cells)
                ? &s->lines[ar].cells[0].Char.UnicodeChar : NULL;
        g_copy_cx = sl ? copy_step_char(sl, s->cols, g_copy_cx, -1)
                       : (g_copy_cx > 0 ? g_copy_cx - 1 : 0);
        g_mux.needs_redraw = 1;
        return 0;
    }
    if (vk == VK_RIGHT || uc == 'l' || uc == 'L') {
        /* 右移一次跨过整个宽字符（中文/全角/emoji），不停在次格（占位）上。 */
        int vo = p->scroll_offset;
        int ar = (vo > 0 && !s->in_alt_screen) ? screen_phys_row(s, g_copy_cy - vo) : -1;
        const WCHAR *sl = (s->in_alt_screen && s->alt_buffer)
            ? &s->alt_buffer[(size_t)g_copy_cy * s->cols].Char.UnicodeChar
            : (ar >= 0 && s->lines && s->lines[ar].cells)
                ? &s->lines[ar].cells[0].Char.UnicodeChar : NULL;
        g_copy_cx = sl ? copy_step_char(sl, s->cols, g_copy_cx, +1)
                       : (g_copy_cx < s->cols - 1 ? g_copy_cx + 1 : s->cols - 1);
        g_mux.needs_redraw = 1;
        return 0;
    }
    if (vk == VK_UP || uc == 'k' || uc == 'K') {
        if (g_copy_cy > 0) {
            g_copy_cy--;
        } else if (p->scroll_offset < s->hist_lines) {
            p->scroll_offset++;
        }
        g_mux.needs_redraw = 1;
        return 0;
    }
    if (vk == VK_DOWN || uc == 'j' || uc == 'J') {
        if (g_copy_cy < s->rows - 1) {
            g_copy_cy++;
        } else if (p->scroll_offset > 0) {
            p->scroll_offset--;
        }
        g_mux.needs_redraw = 1;
        return 0;
    }
    if (vk == VK_PRIOR) {
        p->scroll_offset += s->rows / 2;
        if (p->scroll_offset > s->hist_lines) p->scroll_offset = s->hist_lines;
        g_mux.needs_redraw = 1;
        return 0;
    }
    if (vk == VK_NEXT) {
        p->scroll_offset -= s->rows / 2;
        if (p->scroll_offset < 0) p->scroll_offset = 0;
        g_mux.needs_redraw = 1;
        return 0;
    }
    if (vk == VK_HOME || uc == '0' || uc == '^') {
        g_copy_cx = 0;
        g_mux.needs_redraw = 1;
        return 0;
    }
    if (vk == VK_END || uc == '$') {
        g_copy_cx = s->cols - 1;
        g_mux.needs_redraw = 1;
        return 0;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * 设置页新增分类的键盘处理：外观 / 键位 / 行为
 * ------------------------------------------------------------------------- */

static void settings_leave_subpage(void) {
    g_key_capture_active = 0;
    g_hex_edit_active = 0;
    g_hex_edit_role = -1;
    g_settings_nav = SETTINGS_NAV_STARTUP;
    g_mux.needs_redraw = 1;
}

static void settings_hex_edit_begin(int role) {
    int r, g, b;
    theme_role_rgb(role, &r, &g, &b);
    snprintf(g_hex_edit_buf, sizeof(g_hex_edit_buf), "%02x%02x%02x", r, g, b);
    g_hex_edit_len = (int)strlen(g_hex_edit_buf);
    g_hex_edit_role = role;
    g_hex_edit_active = 1;
}

static int is_hex_char(WCHAR uc) {
    return (uc >= '0' && uc <= '9') || (uc >= 'a' && uc <= 'f') || (uc >= 'A' && uc <= 'F');
}

/* 十六进制输入框（外观页的语义色编辑） */
static void handle_hex_edit_key(WORD vk, WCHAR uc) {
    if (vk == VK_ESCAPE) {
        g_hex_edit_active = 0;
        g_hex_edit_role = -1;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_BACK) {
        if (g_hex_edit_len > 0) g_hex_edit_buf[--g_hex_edit_len] = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_RETURN) {
        if (g_hex_edit_len == 6 && g_hex_edit_role >= 0)
            theme_set_role_hex(theme_role_name(g_hex_edit_role), g_hex_edit_buf);
        theme_apply();
        save_config();
        g_hex_edit_active = 0;
        g_hex_edit_role = -1;
        g_mux.needs_redraw = 1;
        return;
    }
    if (is_hex_char(uc) && g_hex_edit_len < 6) {
        g_hex_edit_buf[g_hex_edit_len++] = (char)tolower((unsigned char)uc);
        g_hex_edit_buf[g_hex_edit_len] = 0;
        g_mux.needs_redraw = 1;
    }
}

/* 键位录制：下一次真实按键即为新键位 */
static void handle_key_capture(WORD vk, DWORD ctrl, WCHAR uc) {
    if (vk == VK_ESCAPE) {
        g_key_capture_active = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    char text[24];
    if (!keymap_key_text_from_event(vk, ctrl, uc, text, sizeof(text))) return;

    if (g_settings_keys_sel == 0) {
        keymap_set_prefix(text);
    } else {
        int action = keymap_action_at(g_settings_keys_sel - 1);
        const char *name = keymap_action_name(action);
        keymap_unbind(name);          /* 覆盖式重绑，避免同一动作堆多个键位 */
        keymap_bind(name, text);
    }
    save_config();
    g_key_capture_active = 0;
    g_mux.needs_redraw = 1;
}

static void settings_appearance_activate(void) {
    int tc = theme_count();
    if (g_settings_theme_sel < tc) {
        theme_set_by_name(theme_name_at(g_settings_theme_sel));
        theme_apply();
        save_config();
    } else {
        settings_hex_edit_begin(g_settings_theme_sel - tc);
    }
    g_mux.needs_redraw = 1;
}

static void handle_settings_appearance_key(WORD vk, WCHAR uc, BOOL is_ctrl) {
    int tc = theme_count();
    int total = tc + TH_ROLE_COUNT;

    if (vk == VK_ESCAPE) { settings_leave_subpage(); return; }
    if (vk == VK_UP) {
        if (g_settings_theme_sel > 0) g_settings_theme_sel--;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DOWN) {
        if (g_settings_theme_sel < total - 1) g_settings_theme_sel++;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        if (g_settings_theme_sel >= tc) {
            int role = g_settings_theme_sel - tc;
            int target = vk == VK_RIGHT ? role + SETTINGS_ROLE_ROWS : role - SETTINGS_ROLE_ROWS;
            if (target >= 0 && target < TH_ROLE_COUNT) g_settings_theme_sel = tc + target;
        }
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_RETURN || vk == VK_SPACE) { settings_appearance_activate(); return; }
    if (is_ctrl && (vk == 'R' || uc == 0x12)) {
        theme_clear_overrides();
        theme_apply();
        save_config();
        g_mux.needs_redraw = 1;
        return;
    }
    if (uc == 'r' || uc == 'R') {
        if (g_settings_theme_sel >= tc) {
            theme_clear_role_override(g_settings_theme_sel - tc);
            theme_apply();
            save_config();
            g_mux.needs_redraw = 1;
        }
        return;
    }
}

static void settings_keys_reset_entry(int entry) {
    if (entry == 0) keymap_set_prefix("C-b");
    else keymap_unbind(keymap_action_name(keymap_action_at(entry - 1)));
    save_config();
    g_mux.needs_redraw = 1;
}

/* 前缀行（entry 0）本身没有“是否使用前缀”的概念，其余动作可以切成直接键。 */
static void settings_keys_toggle_prefix(int entry) {
    if (entry <= 0) return;
    int action = keymap_action_at(entry - 1);
    if (action == ACT_NONE) return;
    if (!keymap_set_action_prefix(action, !keymap_action_uses_prefix(action))) return;
    save_config();
    g_mux.needs_redraw = 1;
}

static void handle_settings_keys_key(WORD vk, WCHAR uc, BOOL is_ctrl) {
    int total = settings_keys_rows();
    if (vk == VK_ESCAPE) { settings_leave_subpage(); return; }
    if (vk == VK_UP) {
        if (g_settings_keys_sel > 0) g_settings_keys_sel--;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DOWN) {
        if (g_settings_keys_sel < total - 1) g_settings_keys_sel++;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_RETURN) {
        g_key_capture_active = 1;
        g_mux.needs_redraw = 1;
        return;
    }
    if (is_ctrl && (vk == 'R' || uc == 0x12)) {
        keymap_set_prefix("C-b");
        for (int i = 0; i < keymap_action_count(); i++)
            keymap_unbind(keymap_action_name(keymap_action_at(i)));
        save_config();
        g_mux.needs_redraw = 1;
        return;
    }
    if (uc == 'r' || uc == 'R') { settings_keys_reset_entry(g_settings_keys_sel); return; }
    if (uc == 'p' || uc == 'P') { settings_keys_toggle_prefix(g_settings_keys_sel); return; }
}

static void settings_behavior_toggle(int idx) {
    if (idx == 0) g_mouse_enabled = !g_mouse_enabled;
    else if (idx == 1) g_copy_on_select = !g_copy_on_select;
    else if (idx == 2) g_confirm_on_exit = !g_confirm_on_exit;
    else if (idx == 3) {
        g_search_case_sensitive = !g_search_case_sensitive;
        if (g_search_active) execute_search();   /* 立刻按新规则重新匹配 */
    }
    save_config();
    g_mux.needs_redraw = 1;
}

static void settings_scrollback_step(int delta) {
    int n = g_scrollback_lines + delta;
    if (n < 200) n = 200;
    if (n > 500000) n = 500000;
    g_scrollback_lines = n;
    save_config();
    g_mux.needs_redraw = 1;
}

static void handle_settings_behavior_key(WORD vk, WCHAR uc) {
    if (vk == VK_ESCAPE) { settings_leave_subpage(); return; }
    if (vk == VK_UP) {
        if (g_settings_behavior_sel > 0) g_settings_behavior_sel--;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DOWN) {
        if (g_settings_behavior_sel < SETTINGS_BEHAVIOR_TOGGLES) g_settings_behavior_sel++;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        if (g_settings_behavior_sel == SETTINGS_BEHAVIOR_TOGGLES)
            settings_scrollback_step(vk == VK_RIGHT ? 1000 : -1000);
        else settings_behavior_toggle(g_settings_behavior_sel);
        return;
    }
    if (vk == VK_RETURN || vk == VK_SPACE || uc == ' ') {
        if (g_settings_behavior_sel < SETTINGS_BEHAVIOR_TOGGLES)
            settings_behavior_toggle(g_settings_behavior_sel);
        return;
    }
}

void handle_settings_key(KEY_EVENT_RECORD *ke) {
    WORD vk = ke->wVirtualKeyCode; DWORD ctrl = ke->dwControlKeyState; WCHAR uc = ke->uChar.UnicodeChar;
    BOOL is_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    BOOL is_shift = (ctrl & SHIFT_PRESSED) != 0;
    BOOL is_alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

    /* 录制键位 / 编辑十六进制时独占键盘 */
    if (g_key_capture_active) { handle_key_capture(vk, ctrl, uc); return; }
    if (g_hex_edit_active) { handle_hex_edit_key(vk, uc); return; }

    if (g_settings_show_presets) {
        if (vk == VK_ESCAPE) {
            g_settings_show_presets = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_UP) {
            g_preset_sel = (g_preset_sel - 1 + g_preset_count) % g_preset_count;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_DOWN) {
            g_preset_sel = (g_preset_sel + 1) % g_preset_count;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RETURN) {
            int i = g_preset_sel;
            if (i >= 0 && i < g_preset_count) {
                if (g_settings_nav >= 1) {
                    int idx = g_settings_nav - 1;
                    if (idx >= 0 && idx < g_chooser_item_count) {
                        strncpy(g_edit_name, g_presets[i].name, sizeof(g_edit_name) - 1);
                        g_edit_name_len = (int)strlen(g_edit_name);
                        g_edit_name_pos = g_edit_name_len;
                        strncpy(g_edit_cmd, g_presets[i].cmd, sizeof(g_edit_cmd) - 1);
                        g_edit_cmd_len = (int)strlen(g_edit_cmd);
                        g_edit_cmd_pos = g_edit_cmd_len;
                        save_editor_to_item(idx);
                    }
                } else if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                    int idx = g_chooser_item_count++;
                    strncpy(g_chooser_items[idx].name, g_presets[i].name, sizeof(g_chooser_items[0].name) - 1);
                    strncpy(g_chooser_items[idx].cmd, g_presets[i].cmd, sizeof(g_chooser_items[0].cmd) - 1);
                    g_chooser_items[idx].workdir[0] = 0;
                    g_chooser_items[idx].color = 0;
                    save_config();
                }
                g_settings_show_presets = 0;
                g_mux.needs_redraw = 1;
                return;
            }
        }
        for (int i = 0; i < g_preset_count; i++) {
            char digit = (char)('1' + i);
            if (uc == digit || vk == ('1' + i) || vk == (VK_NUMPAD1 + i)) {
                if (g_settings_nav >= 1) {
                    int idx = g_settings_nav - 1;
                    if (idx >= 0 && idx < g_chooser_item_count) {
                        strncpy(g_edit_name, g_presets[i].name, sizeof(g_edit_name) - 1);
                        g_edit_name_len = (int)strlen(g_edit_name);
                        g_edit_name_pos = g_edit_name_len;
                        strncpy(g_edit_cmd, g_presets[i].cmd, sizeof(g_edit_cmd) - 1);
                        g_edit_cmd_len = (int)strlen(g_edit_cmd);
                        g_edit_cmd_pos = g_edit_cmd_len;
                        save_editor_to_item(idx);
                    }
                } else if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                    int idx = g_chooser_item_count++;
                    strncpy(g_chooser_items[idx].name, g_presets[i].name, sizeof(g_chooser_items[0].name) - 1);
                    strncpy(g_chooser_items[idx].cmd, g_presets[i].cmd, sizeof(g_chooser_items[0].cmd) - 1);
                    g_chooser_items[idx].workdir[0] = 0;
                    g_chooser_items[idx].color = 0;
                    save_config();
                }
                g_settings_show_presets = 0;
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;
    }

    if ((vk == 'S' && is_ctrl) || (uc == 0x13)) {
        if (g_settings_nav >= 1 && g_settings_nav <= g_chooser_item_count) {
            save_editor_to_item(g_settings_nav - 1);
        } else {
            save_config();
        }
        g_mux.needs_redraw = 1;
        return;
    }

    if (((vk == 'P' || vk == 'p') && (is_ctrl || is_alt)) || uc == 0x10) {
        g_settings_show_presets = 1;
        g_preset_sel = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_settings_nav == 0 && is_ctrl &&
        (vk == VK_UP || vk == VK_DOWN)) {
        int i = g_settings_table_sel;
        int target = i + (vk == VK_UP ? -1 : 1);
        if (i >= 0 && target >= 0 && target < g_chooser_item_count) {
            ChooserItem tmp = g_chooser_items[i];
            g_chooser_items[i] = g_chooser_items[target];
            g_chooser_items[target] = tmp;
            g_settings_table_sel = target;
            save_config();
        }
        g_mux.needs_redraw = 1;
        return;
    }

    if ((is_ctrl || is_alt) && (vk == VK_UP || vk == VK_DOWN)) {
        int idx = settings_nav_index_of(g_settings_nav) + (vk == VK_UP ? -1 : 1);
        if (idx >= 0 && idx < settings_nav_order_count()) {
            g_settings_nav = settings_nav_at(idx);
            if (g_settings_nav >= 1 && g_settings_nav <= g_chooser_item_count)
                load_item_to_editor(g_settings_nav - 1);
            g_mux.needs_redraw = 1;
        }
        return;
    }

    if (g_settings_nav == SETTINGS_NAV_APPEARANCE) { handle_settings_appearance_key(vk, uc, is_ctrl); return; }
    if (g_settings_nav == SETTINGS_NAV_KEYS) { handle_settings_keys_key(vk, uc, is_ctrl); return; }
    if (g_settings_nav == SETTINGS_NAV_BEHAVIOR) { handle_settings_behavior_key(vk, uc); return; }

    if (g_settings_nav == 0) {
        if (vk == VK_ESCAPE) {
            int c = g_mux.active_pane;
            int n = find_next_active_pane(c);
            close_pane(c);
            if (n >= 0 && g_mux.panes[n].active) switch_pane(n);
            else {
                int f = 0;
                for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) { switch_pane(i); f = 1; break; }
                if (!f) g_mux.running = 0;
            }
            g_mux.needs_redraw = 1;
            return;
        }

        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_SPACE || uc == 't' || uc == 'T' || uc == 'h' || uc == 'H') {
            if (uc == 't' || uc == 'T') g_default_startup = 0;
            else if (uc == 'h' || uc == 'H') g_default_startup = 1;
            else g_default_startup = !g_default_startup;
            save_config();
            g_mux.needs_redraw = 1;
            return;
        }

        if (vk == VK_UP) {
            if (g_settings_table_sel > 0) g_settings_table_sel--;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_DOWN) {
            if (g_settings_table_sel < g_chooser_item_count - 1) g_settings_table_sel++;
            g_mux.needs_redraw = 1;
            return;
        }

        if (vk == VK_RETURN || uc == 'e' || uc == 'E') {
            int i = g_settings_table_sel;
            if (i >= 0 && i < g_chooser_item_count) {
                g_settings_nav = i + 1;
                load_item_to_editor(i);
                g_mux.needs_redraw = 1;
                return;
            }
        }

        if (vk == VK_DELETE || vk == VK_BACK || uc == 'x' || uc == 'X') {
            int i = g_settings_table_sel;
            if (g_chooser_item_count > 1 && i >= 0 && i < g_chooser_item_count) {
                for (int k = i; k < g_chooser_item_count - 1; k++)
                    g_chooser_items[k] = g_chooser_items[k + 1];
                g_chooser_item_count--;
                if (g_settings_table_sel >= g_chooser_item_count) g_settings_table_sel = g_chooser_item_count - 1;
                save_config();
                g_mux.needs_redraw = 1;
                return;
            }
        }

        if (uc == '+' || uc == '=' || uc == 'a' || uc == 'A' || uc == 'n' || uc == 'N') {
            if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                int idx = g_chooser_item_count++;
                snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[0].name), "新终端");
                snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[0].cmd), "cmd.exe");
                g_chooser_items[idx].workdir[0] = 0;
                g_chooser_items[idx].color = 0;
                save_config();
                g_settings_table_sel = idx;
                g_settings_nav = idx + 1;
                load_item_to_editor(idx);
                g_mux.needs_redraw = 1;
                return;
            }
        }

        if (uc == 'p' || uc == 'P') {
            g_settings_show_presets = 1;
            g_preset_sel = 0;
            g_mux.needs_redraw = 1;
            return;
        }

        /* 三个分类页的快捷入口：F2 外观 / F3 键位 / F4 行为，字母 K / B 同义 */
        if (vk == VK_F2) { g_settings_nav = SETTINGS_NAV_APPEARANCE; g_mux.needs_redraw = 1; return; }
        if (vk == VK_F3 || uc == 'k' || uc == 'K') { g_settings_nav = SETTINGS_NAV_KEYS; g_mux.needs_redraw = 1; return; }
        if (vk == VK_F4 || uc == 'b' || uc == 'B') { g_settings_nav = SETTINGS_NAV_BEHAVIOR; g_mux.needs_redraw = 1; return; }

        if ((uc >= '1' && uc <= '9') || (vk >= '1' && vk <= '9') || (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9)) {
            int num = (uc >= '1' && uc <= '9') ? (uc - '0') : ((vk >= '1' && vk <= '9') ? (vk - '0') : (vk - VK_NUMPAD1 + 1));
            if (num <= g_chooser_item_count) {
                g_settings_table_sel = num - 1;
                g_settings_nav = num;
                load_item_to_editor(num - 1);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;
    }

    if (g_settings_nav >= 1) {
        if (vk == VK_ESCAPE) {
            g_settings_nav = 0;
            g_mux.needs_redraw = 1;
            return;
        }

        if (vk == VK_TAB) {
            if (is_shift) {
                g_settings_field = (g_settings_field + 3) % 4;
            } else {
                g_settings_field = (g_settings_field + 1) % 4;
            }
            g_mux.needs_redraw = 1;
            return;
        }

        if (((vk == 'D' || vk == 'd') && (is_ctrl || is_alt)) || uc == 0x04) {
            int item_idx = g_settings_nav - 1;
            if (g_chooser_item_count > 1 && item_idx >= 0 && item_idx < g_chooser_item_count) {
                for (int k = item_idx; k < g_chooser_item_count - 1; k++)
                    g_chooser_items[k] = g_chooser_items[k + 1];
                g_chooser_item_count--;
                g_settings_nav = 0;
                if (g_settings_table_sel >= g_chooser_item_count) g_settings_table_sel = g_chooser_item_count - 1;
                save_config();
                g_mux.needs_redraw = 1;
                return;
            }
        }

        if (vk == VK_RETURN) {
            save_editor_to_item(g_settings_nav - 1);
            g_mux.needs_redraw = 1;
            return;
        }

        if (g_settings_field == 3) {
            /* v1.8.9: 颜色行不是输入框，←/→ 或数字 0-8 直接选色。 */
            if (vk == VK_LEFT) {
                g_edit_color = (g_edit_color + 8) % 9;
                g_mux.needs_redraw = 1;
            } else if (vk == VK_RIGHT) {
                g_edit_color = (g_edit_color + 1) % 9;
                g_mux.needs_redraw = 1;
            } else if (uc >= '0' && uc <= '8') {
                g_edit_color = (int)(uc - '0');
                g_mux.needs_redraw = 1;
            }
            return;
        }

        char *buf = NULL;
        int *len = NULL;
        int *pos = NULL;
        int max_len = 0;
        if (g_settings_field == 0) {
            buf = g_edit_name; len = &g_edit_name_len; pos = &g_edit_name_pos; max_len = 31;
        } else if (g_settings_field == 1) {
            buf = g_edit_cmd; len = &g_edit_cmd_len; pos = &g_edit_cmd_pos; max_len = 255;
        } else if (g_settings_field == 2) {
            buf = g_edit_dir; len = &g_edit_dir_len; pos = &g_edit_dir_pos; max_len = 255;
        }
        if (!buf || !len || !pos) return;

        if (vk == VK_LEFT) {
            *pos = utf8_prev_grapheme(buf, *pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RIGHT) {
            *pos = utf8_next_grapheme(buf, *len, *pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_HOME) {
            *pos = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_END) {
            *pos = *len;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_BACK) {
            if (*pos > 0) {
                int p_prev = utf8_prev_grapheme(buf, *pos);
                memmove(buf + p_prev, buf + *pos, *len - *pos + 1);
                *len -= (*pos - p_prev);
                *pos = p_prev;
                g_mux.needs_redraw = 1;
            }
            return;
        }
        if (vk == VK_DELETE) {
            if (*pos < *len) {
                int p_next = utf8_next_grapheme(buf, *len, *pos);
                memmove(buf + *pos, buf + p_next, *len - p_next + 1);
                *len -= (p_next - *pos);
                g_mux.needs_redraw = 1;
            }
            return;
        }
        if (uc >= 0xD800 && uc <= 0xDBFF) {
            g_high_surrogate = uc;
            return;
        }
        if (uc) {
            char u8[8] = {0}; int u8_count = 0;
            if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
                unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
                g_high_surrogate = 0;
                u8[0] = (char)(0xF0 | (cp >> 18));
                u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                u8[3] = (char)(0x80 | (cp & 0x3F));
                u8_count = 4;
            } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
                g_high_surrogate = 0;
                if (uc < 0x80) { u8[0] = (char)uc; u8_count = 1; }
                else if (uc < 0x800) { u8[0] = (char)(0xC0 | (uc >> 6)); u8[1] = (char)(0x80 | (uc & 0x3F)); u8_count = 2; }
                else { u8[0] = (char)(0xE0 | (uc >> 12)); u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F)); u8[2] = (char)(0x80 | (uc & 0x3F)); u8_count = 3; }
            }
            if (u8_count > 0 && *len + u8_count <= max_len) {
                memmove(buf + *pos + u8_count, buf + *pos, *len - *pos + 1);
                memcpy(buf + *pos, u8, u8_count);
                *pos += u8_count;
                *len += u8_count;
                g_mux.needs_redraw = 1;
                return;
            }
        }
    }
}

void handle_settings_mouse(MOUSE_EVENT_RECORD *me) {
    int mx = me->dwMousePosition.X, my = me->dwMousePosition.Y;
    int press = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
    if (!press || (me->dwEventFlags != 0 && me->dwEventFlags != DOUBLE_CLICK)) return;

    int host_rows = g_mux.host_rows;
    int host_cols = g_mux.host_cols;
    int sb_w = SETTINGS_SIDEBAR_W;
    if (sb_w > host_cols / 2) sb_w = host_cols / 2;
    if (sb_w < 15) sb_w = 15;
    if (sb_w > host_cols) sb_w = host_cols;
    if (sb_w < 1) sb_w = 1;
    int main_left = sb_w + 3;
    int right_max_w = host_cols - main_left - 2;
    if (right_max_w < 10) right_max_w = 10;
    int input_w = right_max_w - 4;
    if (input_w > 50) input_w = 50;
    if (input_w < 20) input_w = 20;
    int r = my + 1, c = mx + 1;

    if (g_settings_show_presets) {
        int top, left, pw, ph, mnw, mcw;
        presets_geom(host_rows, host_cols, &top, &left, &pw, &ph, &mnw, &mcw);
        int in_box = (r >= top && r < top + ph && c >= left && c < left + pw);
        if (in_box) {
            for (int i = 0; i < g_preset_count; i++) {
                if (r == top + 1 + i) {
                    if (g_settings_nav >= 1) {
                        int idx = g_settings_nav - 1;
                        if (idx >= 0 && idx < g_chooser_item_count) {
                            strncpy(g_edit_name, g_presets[i].name, sizeof(g_edit_name) - 1);
                            g_edit_name_len = (int)strlen(g_edit_name);
                            g_edit_name_pos = g_edit_name_len;
                            strncpy(g_edit_cmd, g_presets[i].cmd, sizeof(g_edit_cmd) - 1);
                            g_edit_cmd_len = (int)strlen(g_edit_cmd);
                            g_edit_cmd_pos = g_edit_cmd_len;
                            save_editor_to_item(idx);
                        }
                    } else if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                        int idx = g_chooser_item_count++;
                        strncpy(g_chooser_items[idx].name, g_presets[i].name, sizeof(g_chooser_items[0].name) - 1);
                        strncpy(g_chooser_items[idx].cmd, g_presets[i].cmd, sizeof(g_chooser_items[0].cmd) - 1);
                        g_chooser_items[idx].workdir[0] = 0;
                        g_chooser_items[idx].color = 0;
                        save_config();
                    }
                    g_settings_show_presets = 0;
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
        }
        g_settings_show_presets = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    /* The divider itself is ANSI column sb_w and is still part of the
     * sidebar hit region rendered above. */
    if (c <= sb_w) {
        if (r == 5) {
            g_settings_nav = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        for (int i = 0; i < g_chooser_item_count; i++) {
            if (r == 7 + i) {
                g_settings_nav = i + 1;
                load_item_to_editor(i);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        if (r == 7 + g_chooser_item_count) {
            if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                int idx = g_chooser_item_count++;
                snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[0].name), "新终端");
                snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[0].cmd), "cmd.exe");
                g_chooser_items[idx].workdir[0] = 0;
                g_chooser_items[idx].color = 0;
                save_config();
                g_settings_nav = idx + 1;
                load_item_to_editor(idx);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        if (r == 8 + g_chooser_item_count) {
            g_settings_show_presets = 1;
            g_preset_sel = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        {
            int app_r, keys_r, beh_r;
            settings_sidebar_extra_rows(&app_r, &keys_r, &beh_r);
            if (r == app_r || r == keys_r || r == beh_r) {
                g_key_capture_active = 0;
                g_hex_edit_active = 0;
                g_settings_nav = (r == app_r) ? SETTINGS_NAV_APPEARANCE
                               : (r == keys_r) ? SETTINGS_NAV_KEYS : SETTINGS_NAV_BEHAVIOR;
                g_mux.needs_redraw = 1;
                return;
            }
        }
        if (r == host_rows) {
            if (g_settings_nav >= 1) {
                save_editor_to_item(g_settings_nav - 1);
            } else {
                save_config();
            }
            g_mux.needs_redraw = 1;
            return;
        }
    }

    if (c >= main_left) {
        if (g_settings_nav == SETTINGS_NAV_APPEARANCE) {
            int tc = theme_count();
            for (int i = 0; i < tc; i++) {
                if (r == settings_theme_row(i)) {
                    g_settings_theme_sel = i;
                    theme_set_by_name(theme_name_at(i));
                    theme_apply();
                    save_config();
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
            for (int role = 0; role < TH_ROLE_COUNT; role++) {
                int row = settings_role_row(role);
                int col = settings_role_col(main_left, role);
                if (r == row && c >= col && c < col + SETTINGS_ROLE_COL_W - 1) {
                    g_settings_theme_sel = tc + role;
                    settings_hex_edit_begin(role);
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
            return;
        }
        if (g_settings_nav == SETTINGS_NAV_KEYS) {
            int entry = settings_keys_entry_at(host_rows, r);
            if (entry >= 0) {
                g_settings_keys_sel = entry;
                if (c >= main_left + SETTINGS_KEYS_RESET_COL && c < main_left + SETTINGS_KEYS_RESET_COL + 6) {
                    settings_keys_reset_entry(entry);
                } else if (c >= main_left + SETTINGS_KEYS_EDIT_COL && c < main_left + SETTINGS_KEYS_EDIT_COL + 4) {
                    g_key_capture_active = 1;
                } else if (c >= main_left + SETTINGS_KEYS_PREFIX_COL && c < main_left + SETTINGS_KEYS_PREFIX_COL + 6) {
                    settings_keys_toggle_prefix(entry);
                }
                g_mux.needs_redraw = 1;
            }
            return;
        }
        if (g_settings_nav == SETTINGS_NAV_BEHAVIOR) {
            for (int i = 0; i < SETTINGS_BEHAVIOR_TOGGLES; i++) {
                if (r == SETTINGS_BEHAVIOR_ROW0 + i) {
                    g_settings_behavior_sel = i;
                    settings_behavior_toggle(i);
                    return;
                }
            }
            if (r == SETTINGS_BEHAVIOR_ROW0 + SETTINGS_BEHAVIOR_TOGGLES) {
                g_settings_behavior_sel = SETTINGS_BEHAVIOR_TOGGLES;
                if (c >= main_left + SETTINGS_SB_MINUS_COL && c < main_left + SETTINGS_SB_MINUS_COL + 3)
                    settings_scrollback_step(-1000);
                else if (c >= main_left + SETTINGS_SB_PLUS_COL && c < main_left + SETTINGS_SB_PLUS_COL + 3)
                    settings_scrollback_step(1000);
                g_mux.needs_redraw = 1;
                return;
            }
            return;
        }
        if (g_settings_nav == 0) {
            if (r == 5) {
                if (c >= main_left && c < main_left + 26) {
                    g_default_startup = 0;
                    save_config();
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (c >= main_left + 29 && c < main_left + 51) {
                    g_default_startup = 1;
                    save_config();
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
            for (int i = 0; i < g_chooser_item_count; i++) {
                if (r == 10 + i) {
                    int h_up = (c >= main_left + 53 && c <= main_left + 55);
                    int h_dn = (c >= main_left + 56 && c <= main_left + 58);
                    int h_ed = (c >= main_left + 59 && c <= main_left + 62);
                    int h_del = (c >= main_left + 63 && c <= main_left + 66);
                    if (h_up) {
                        if (i > 0) {
                            ChooserItem tmp = g_chooser_items[i];
                            g_chooser_items[i] = g_chooser_items[i - 1];
                            g_chooser_items[i - 1] = tmp;
                            g_settings_table_sel = i - 1;
                            save_config();
                            g_mux.needs_redraw = 1;
                        }
                        return;
                    }
                    if (h_dn) {
                        if (i < g_chooser_item_count - 1) {
                            ChooserItem tmp = g_chooser_items[i];
                            g_chooser_items[i] = g_chooser_items[i + 1];
                            g_chooser_items[i + 1] = tmp;
                            g_settings_table_sel = i + 1;
                            save_config();
                            g_mux.needs_redraw = 1;
                        }
                        return;
                    }
                    if (h_ed) {
                        g_settings_table_sel = i;
                        g_settings_nav = i + 1;
                        load_item_to_editor(i);
                        g_mux.needs_redraw = 1;
                        return;
                    }
                    if (h_del) {
                        if (g_chooser_item_count > 1) {
                            for (int k = i; k < g_chooser_item_count - 1; k++)
                                g_chooser_items[k] = g_chooser_items[k + 1];
                            g_chooser_item_count--;
                            if (g_settings_table_sel >= g_chooser_item_count) g_settings_table_sel = g_chooser_item_count - 1;
                            save_config();
                            g_mux.needs_redraw = 1;
                        }
                        return;
                    }
                    g_settings_table_sel = i;
                    g_settings_nav = i + 1;
                    load_item_to_editor(i);
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
            int btn_r = 10 + g_chooser_item_count + 1;
            if (r == btn_r) {
                if (c >= main_left && c < main_left + 14) {
                    if (g_chooser_item_count < MAX_CHOOSER_ITEMS) {
                        int idx = g_chooser_item_count++;
                        snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[0].name), "新终端");
                        snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[0].cmd), "cmd.exe");
                        g_chooser_items[idx].workdir[0] = 0;
                        g_chooser_items[idx].color = 0;
                        save_config();
                        g_settings_nav = idx + 1;
                        load_item_to_editor(idx);
                        g_mux.needs_redraw = 1;
                        return;
                    }
                }
                if (c >= main_left + 16 && c < main_left + 30) {
                    g_settings_show_presets = 1;
                    g_preset_sel = 0;
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
        } else {
            int item_idx = g_settings_nav - 1;
            if (r == 6 && c >= main_left && c < main_left + input_w + 4) {
                g_settings_field = 0;
                g_mux.needs_redraw = 1;
                return;
            }
            if (r == 9 && c >= main_left && c < main_left + input_w + 4) {
                g_settings_field = 1;
                g_mux.needs_redraw = 1;
                return;
            }
            if (r == 12 && c >= main_left && c < main_left + input_w + 4) {
                g_settings_field = 2;
                g_mux.needs_redraw = 1;
                return;
            }
            if (r == 15) {   /* v1.8.9: 启动默认颜色选择条 */
                int hit = item_color_hit(main_left, c);
                if (hit >= 0) {
                    g_settings_field = 3;
                    g_edit_color = hit;
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
            if (r == 17) {
                if (c >= main_left && c < main_left + 18) {
                    save_editor_to_item(item_idx);
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (c >= main_left + 20 && c < main_left + 36) {
                    g_settings_show_presets = 1;
                    g_preset_sel = 0;
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (c >= main_left + 38 && c < main_left + 50) {
                    if (g_chooser_item_count > 1) {
                        for (int k = item_idx; k < g_chooser_item_count - 1; k++)
                            g_chooser_items[k] = g_chooser_items[k + 1];
                        g_chooser_item_count--;
                        g_settings_nav = 0;
                        save_config();
                        g_mux.needs_redraw = 1;
                        return;
                    }
                }
            }
        }
    }
}

/* 关闭所有模态弹窗，动作执行前统一调用。 */
static void dismiss_overlays(void) {
    g_mux.ctx_mode = 0;
    g_mux.rename_mode = 0;
    g_mux.custom_cmd_mode = 0;
    g_mux.chooser_mode = 0;
    g_mux.palette_mode = 0;
}

void action_execute(int action, int arg, DWORD ctrl) {
    switch (action) {
        case ACT_SEND_PREFIX: {
            char c = keymap_prefix_char();
            if (c) write_to_pane(&c, 1);
            break;
        }
        case ACT_NEW_PANE_MENU: {
            dismiss_overlays();
            g_mux.help_mode = 0;
            g_mux.chooser_mode = 1;
            g_pop_anchor_x = 20;
            for (int k = 0; k < g_mux.tab_count; k++) {
                if (g_mux.tab_info[k].pane_idx == -1) {
                    g_pop_anchor_x = g_mux.tab_info[k].start_col;
                    break;
                }
            }
            g_mux.needs_redraw = 1;
            break;
        }
        case ACT_COMMAND_PALETTE: {
            dismiss_overlays();
            g_mux.help_mode = 0;
            open_command_palette();
            break;
        }
        case ACT_SEARCH: {
            g_mux.palette_mode = 0;
            g_search_mode = 1;
            ui_modes_claim();
            g_search_len = 0;
            g_search_pos = 0;
            g_search_buf[0] = 0;
            g_mux.needs_redraw = 1;
            break;
        }
        case ACT_HELP: {
            g_mux.help_mode = !g_mux.help_mode;
            if (!g_mux.help_mode) g_mux.help_scroll = 0;
            dismiss_overlays();
            g_mux.needs_redraw = 1;
            break;
        }
        case ACT_COPY_MODE: {
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
                Pane *p = &g_mux.panes[g_mux.active_pane];
                g_copy_mode = 1;
                ui_modes_claim();
                g_copy_sel_active = 0;
                g_copy_quick = 0;
                g_copy_block = 0;
                g_copy_cx = p->screen.cursor_x;
                g_copy_cy = p->screen.cursor_y;
                g_mux.needs_redraw = 1;
            }
            break;
        }
        case ACT_RELOAD_CONFIG: {
            load_config();
            g_mux.needs_redraw = 1;
            break;
        }
        case ACT_NEXT_THEME: {
            int next = (theme_index() + 1) % theme_count();
            theme_set_by_name(theme_name_at(next));
            theme_apply();
            save_config();
            g_mux.needs_redraw = 1;
            break;
        }
        case ACT_NEW_PANE: {
            int i = create_pane();
            if (i >= 0) switch_pane(i);
            break;
        }
        case ACT_NEXT_PANE: {
            int n = find_next_active_pane(g_mux.active_pane);
            if (n >= 0) switch_pane(n);
            break;
        }
        case ACT_PREV_PANE: {
            for (int i = 1; i <= g_mux.pane_count; i++) {
                int n = (g_mux.active_pane - i + g_mux.pane_count) % g_mux.pane_count;
                if (g_mux.panes[n].active) { switch_pane(n); break; }
            }
            break;
        }
        case ACT_CLOSE_PANE: {
            int c = g_mux.active_pane, n = find_next_active_pane(c);
            close_pane(c);
            if (n >= 0 && g_mux.panes[n].active) switch_pane(n);
            else {
                int f = 0;
                for (int i = 0; i < g_mux.pane_count; i++)
                    if (g_mux.panes[i].active) { switch_pane(i); f = 1; break; }
                if (!f) g_mux.running = 0;
            }
            break;
        }
        case ACT_QUIT: {
            if (g_confirm_on_exit) {
                dismiss_overlays();
                g_mux.confirm_exit_mode = 1;
                g_mux.needs_redraw = 1;
            } else {
                g_mux.running = 0;
            }
            break;
        }
        case ACT_TAB_COLOR_NEXT:
        case ACT_TAB_COLOR_PREV: {
            (void)ctrl;
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
                Pane *p = &g_mux.panes[g_mux.active_pane];
                if (!p->is_about && !p->is_settings) {
                    int c = p->color + (action == ACT_TAB_COLOR_PREV ? -1 : 1);
                    if (c > 8) c = 1;
                    if (c < 1) c = 8;
                    p->color = c;
                    g_mux.needs_redraw = 1;
                }
            }
            break;
        }
        case ACT_SETTINGS: {
            dismiss_overlays();
            g_mux.help_mode = 0;
            open_settings_pane();
            g_mux.needs_redraw = 1;
            break;
        }
        case ACT_SELECT_PANE: {
            if (arg >= 0 && arg < g_mux.pane_count && g_mux.panes[arg].active) switch_pane(arg);
            break;
        }
        default:
            break;
    }
}

void handle_prefix(WORD vk, DWORD ctrl, WCHAR uc) {
    g_mux.prefix_mode = 0;
    /* 连按两次前缀键 = 把前缀键本身发给当前 pane */
    if (keymap_is_prefix(vk, ctrl, uc)) {
        action_execute(ACT_SEND_PREFIX, 0, ctrl);
        return;
    }
    int arg = 0;
    int action = keymap_lookup(vk, ctrl, uc, &arg);
    if (action != ACT_NONE) action_execute(action, arg, ctrl);
}

static int key_input_modal_active(void) {
    if (g_mux.palette_mode || g_mux.rename_mode || g_mux.custom_cmd_mode) return 1;
    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count &&
        g_mux.panes[g_mux.active_pane].active && g_mux.panes[g_mux.active_pane].is_settings) {
        /* 文本编辑（菜单项详情 / 颜色十六进制）与键位录制期间独占键盘，
         * 否则外观 / 键位 / 行为页仍然允许全局 Ctrl+B 前缀。 */
        if (g_settings_show_presets || g_key_capture_active || g_hex_edit_active) return 1;
        if (g_settings_nav >= 1 && g_settings_nav <= g_chooser_item_count) return 1;
    }
    return 0;
}

void handle_key(KEY_EVENT_RECORD *ke) {
    if (g_hover_preview_active || g_hover_chooser_active || g_hover_settings_name_active || g_hover_settings_cmd_active) {
        g_hover_preview_active = 0;
        g_hover_preview_pane = -1;
        g_hover_chooser_active = 0;
        g_hover_chooser_idx = -1;
        g_hover_settings_name_active = 0;
        g_hover_settings_name_idx = -1;
        g_hover_settings_cmd_active = 0;
        g_hover_settings_cmd_idx = -1;
        g_mux.needs_redraw = 1;
    }
    if (!ke->bKeyDown) {
        if (!g_mux.prefix_mode && !g_mux.rename_mode && !g_mux.custom_cmd_mode &&
            !g_mux.chooser_mode && !g_mux.ctx_mode && !g_mux.help_mode &&
            !g_mux.palette_mode && !g_search_mode && !g_copy_mode) {
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active && !g_mux.panes[g_mux.active_pane].is_settings) {
                Pane *pane = &g_mux.panes[g_mux.active_pane];
                if (pane->screen.win32_input_mode && !(ke->uChar.UnicodeChar >= 0xD800 && ke->uChar.UnicodeChar <= 0xDFFF)) {
                    char seq[64];
                    int sl = snprintf(seq, sizeof(seq), "\x1b[%u;%u;%u;0;%lu;%u_",
                                      (unsigned int)ke->wVirtualKeyCode,
                                      (unsigned int)ke->wVirtualScanCode,
                                      (unsigned int)ke->uChar.UnicodeChar,
                                      (unsigned long)ke->dwControlKeyState,
                                      (unsigned int)ke->wRepeatCount);
                    write_to_pane(seq, sl);
                }
            }
        }
        return;
    }
    WORD vk = ke->wVirtualKeyCode; DWORD ctrl = ke->dwControlKeyState; WCHAR uc = ke->uChar.UnicodeChar;
    BOOL is_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0, is_alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0, is_shift = (ctrl & SHIFT_PRESSED) != 0;

    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].exited_hold) {
        int c = g_mux.active_pane;
        int n = find_next_active_pane(c);
        pane_mark_dead(c);
        close_pane(c);
        if (n >= 0 && g_mux.panes[n].active) switch_pane(n);
        else {
            int f = -1;
            for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) { f = i; break; }
            if (f >= 0) switch_pane(f); else g_mux.running = 0;
        }
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_mux.confirm_exit_mode) {
        if (uc == 'y' || uc == 'Y' || vk == VK_RETURN) {
            g_mux.confirm_exit_mode = 0;
            g_mux.running = 0;
        } else if (uc == 'n' || uc == 'N' || vk == VK_ESCAPE) {
            g_mux.confirm_exit_mode = 0;
            g_mux.needs_redraw = 1;
        }
        return;
    }

    if (g_mux.palette_mode) {
        handle_palette_key(ke);
        return;
    }

    if (g_search_mode) {
        handle_search_key(ke);
        return;
    }

    if (g_search_active && !g_mux.prefix_mode && !g_copy_mode && !key_input_modal_active()) {
        if (vk == VK_ESCAPE) {
            g_search_active = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        /* U = 上一个, D = 下一个（与状态徽章上的两个按钮一致）；
         * 老的 n / N 继续保留。 */
        if ((uc == 'd' || uc == 'D' || uc == 'n') && !is_ctrl && !is_alt) {
            search_jump_next();
            return;
        }
        if ((uc == 'u' || uc == 'U' || uc == 'N' || (vk == 'N' && is_shift)) && !is_ctrl && !is_alt) {
            search_jump_prev();
            return;
        }
    }

    if (g_mux.prefix_mode) {
        if (key_input_modal_active()) {
            /* A pending global prefix must never leak into a text editor.
             * Consume the prefix and let the current key reach the modal
             * editor instead of opening a new pane/popup on the next key. */
            g_mux.prefix_mode = 0;
        } else {
            if (vk == VK_SHIFT || vk == 0x10 || vk == 0xA0 || vk == 0xA1 ||
                vk == VK_CONTROL || vk == 0x11 || vk == 0xA2 || vk == 0xA3 ||
                vk == VK_MENU || vk == 0x12 || vk == 0xA4 || vk == 0xA5 ||
                vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) {
                return;
            }
            handle_prefix(vk, ctrl, uc);
            return;
        }
    }
    if (g_copy_mode && !g_mux.prefix_mode) {
        /* A quick Shift/Alt click session forwards the key that closed it, so
         * typing straight after selecting does not lose a character. */
        if (!handle_copy_mode_key(ke)) return;
    }

    /* 被标记为“直接键”的动作不需要前缀；只有用户显式设置过的绑定会进这里，
     * 所以普通输入不会被吃掉。 */
    if (!key_input_modal_active()) {
        int direct_arg = 0;
        int direct = keymap_lookup_direct(vk, ctrl, uc, &direct_arg);
        if (direct != ACT_NONE) {
            action_execute(direct, direct_arg, 0);
            return;
        }
    }

    if (!key_input_modal_active() && keymap_is_prefix(vk, ctrl, uc)) {
        g_mux.prefix_mode = 1;
        return;
    }

    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active && g_mux.panes[g_mux.active_pane].is_settings) {
        handle_settings_key(ke);
        return;
    }

    if (g_mux.rename_mode) {
        if (vk == VK_ESCAPE) {
            g_mux.rename_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RETURN) {
            if (g_mux.ctx_pane >= 0 && g_mux.ctx_pane < g_mux.pane_count && g_mux.rename_len > 0) {
                if (!g_mux.panes[g_mux.ctx_pane].is_about && !g_mux.panes[g_mux.ctx_pane].is_settings) {
                    g_mux.rename_buf[g_mux.rename_len] = 0;
                    if (g_mux.rename_len > 63) g_mux.rename_len = 63;
                    memcpy(g_mux.panes[g_mux.ctx_pane].title, g_mux.rename_buf, g_mux.rename_len);
                    g_mux.panes[g_mux.ctx_pane].title[g_mux.rename_len] = 0;
                    strncpy(g_mux.panes[g_mux.ctx_pane].full_title, g_mux.rename_buf, sizeof(g_mux.panes[g_mux.ctx_pane].full_title) - 1);
                    g_mux.panes[g_mux.ctx_pane].full_title[sizeof(g_mux.panes[g_mux.ctx_pane].full_title) - 1] = 0;
                    if (g_mux.ctx_pane == g_mux.active_pane) update_host_title();
                }
            }
            g_mux.rename_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_LEFT) {
            g_mux.rename_pos = utf8_prev_grapheme(g_mux.rename_buf, g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RIGHT) {
            g_mux.rename_pos = utf8_next_grapheme(g_mux.rename_buf, g_mux.rename_len, g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_HOME) {
            g_mux.rename_pos = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_END) {
            g_mux.rename_pos = g_mux.rename_len;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_BACK) {
            buf_backspace(g_mux.rename_buf, &g_mux.rename_len, &g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_DELETE) {
            buf_delete(g_mux.rename_buf, &g_mux.rename_len, &g_mux.rename_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (uc >= 0xD800 && uc <= 0xDBFF) {
            g_high_surrogate = uc;
            return;
        }
        if (uc) {
            char u8[8] = {0};
            int u8_count = 0;
            if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
                unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
                g_high_surrogate = 0;
                u8[0] = (char)(0xF0 | (cp >> 18));
                u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                u8[3] = (char)(0x80 | (cp & 0x3F));
                u8_count = 4;
            } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
                g_high_surrogate = 0;
                if (uc < 0x80) {
                    u8[0] = (char)uc;
                    u8_count = 1;
                } else if (uc < 0x800) {
                    u8[0] = (char)(0xC0 | (uc >> 6));
                    u8[1] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 2;
                } else {
                    u8[0] = (char)(0xE0 | (uc >> 12));
                    u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F));
                    u8[2] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 3;
                }
            }
            if (u8_count > 0) {
                buf_insert_utf8(g_mux.rename_buf, &g_mux.rename_len, &g_mux.rename_pos, sizeof(g_mux.rename_buf) - 1, u8, u8_count);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;
    }
    if (g_mux.custom_cmd_mode) {
        if (vk == VK_ESCAPE) {
            g_mux.custom_cmd_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RETURN) {
            g_mux.custom_cmd_mode = 0;
            WCHAR wcmd[256] = {0};
            if (g_mux.custom_cmd_len > 0) {
                MultiByteToWideChar(CP_UTF8, 0, g_mux.custom_cmd_buf, -1, wcmd, 255);
            } else {
                wcscpy(wcmd, L"cmd.exe");
            }
            int ni = create_pane_shell(wcmd);
            if (ni >= 0) switch_pane(ni);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_LEFT) {
            g_mux.custom_cmd_pos = utf8_prev_grapheme(g_mux.custom_cmd_buf, g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_RIGHT) {
            g_mux.custom_cmd_pos = utf8_next_grapheme(g_mux.custom_cmd_buf, g_mux.custom_cmd_len, g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_HOME) {
            g_mux.custom_cmd_pos = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_END) {
            g_mux.custom_cmd_pos = g_mux.custom_cmd_len;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_BACK) {
            buf_backspace(g_mux.custom_cmd_buf, &g_mux.custom_cmd_len, &g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_DELETE) {
            buf_delete(g_mux.custom_cmd_buf, &g_mux.custom_cmd_len, &g_mux.custom_cmd_pos);
            g_mux.needs_redraw = 1;
            return;
        }
        if (uc >= 0xD800 && uc <= 0xDBFF) {
            g_high_surrogate = uc;
            return;
        }
        if (uc) {
            char u8[8] = {0};
            int u8_count = 0;
            if (uc >= 0xDC00 && uc <= 0xDFFF && g_high_surrogate) {
                unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
                g_high_surrogate = 0;
                u8[0] = (char)(0xF0 | (cp >> 18));
                u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                u8[3] = (char)(0x80 | (cp & 0x3F));
                u8_count = 4;
            } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
                g_high_surrogate = 0;
                if (uc < 0x80) {
                    u8[0] = (char)uc;
                    u8_count = 1;
                } else if (uc < 0x800) {
                    u8[0] = (char)(0xC0 | (uc >> 6));
                    u8[1] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 2;
                } else {
                    u8[0] = (char)(0xE0 | (uc >> 12));
                    u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F));
                    u8[2] = (char)(0x80 | (uc & 0x3F));
                    u8_count = 3;
                }
            }
            if (u8_count > 0) {
                buf_insert_utf8(g_mux.custom_cmd_buf, &g_mux.custom_cmd_len, &g_mux.custom_cmd_pos, sizeof(g_mux.custom_cmd_buf) - 1, u8, u8_count);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        return;
    }

    if (g_mux.chooser_mode) {
        if (vk == VK_ESCAPE) { g_mux.chooser_mode = 0; g_mux.needs_redraw = 1; return; }
        if (uc == 'a' || uc == 'A') {
            g_mux.chooser_mode = 0;
            int ni = create_about_pane();
            if (ni >= 0) switch_pane(ni);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk >= '1' && vk <= '9') {
            int idx = (vk - '0') - 1;
            if (idx < g_chooser_item_count) {
                g_mux.chooser_mode = 0;
                int ni = create_pane_from_item(idx);
                if (ni >= 0) switch_pane(ni);
                g_mux.needs_redraw = 1;
                return;
            }
        }
        g_mux.chooser_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_mux.ctx_mode == 1) {
        if (uc == '1' || vk == '1') { g_mux.ctx_mode = 2; g_mux.needs_redraw = 1; return; }
        if (uc == '2' || vk == '2') {
            g_mux.ctx_mode = 0;
            g_mux.rename_mode = 1;
            g_mux.rename_len = 0;
            g_mux.rename_buf[0] = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        g_mux.ctx_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (g_mux.ctx_mode == 2) {
        if ((uc >= '1' && uc <= '8') || (vk >= '1' && vk <= '8')) {
            int ci = (uc >= '1' && uc <= '8') ? (uc - '0') : (vk - '0');
            if (g_mux.ctx_pane >= 0 && g_mux.ctx_pane < g_mux.pane_count) {
                if (!g_mux.panes[g_mux.ctx_pane].is_about && !g_mux.panes[g_mux.ctx_pane].is_settings)
                    g_mux.panes[g_mux.ctx_pane].color = ci;
            }
            g_mux.ctx_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        g_mux.ctx_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_mux.help_mode) {
        if (vk == VK_PRIOR) {
            g_mux.help_scroll -= (g_mux.host_rows > 2 ? g_mux.host_rows - 2 : 1);
            if (g_mux.help_scroll < 0) g_mux.help_scroll = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_NEXT) {
            g_mux.help_scroll += (g_mux.host_rows > 2 ? g_mux.host_rows - 2 : 1);
            g_mux.needs_redraw = 1;
            return;
        }
        if (vk == VK_UP) {
            if (g_mux.help_scroll > 0) { g_mux.help_scroll--; g_mux.needs_redraw = 1; }
            return;
        }
        if (vk == VK_DOWN) {
            g_mux.help_scroll++;
            g_mux.needs_redraw = 1;
            return;
        }
        g_mux.help_mode = 0;
        g_mux.help_scroll = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return;
    Pane *pane = &g_mux.panes[g_mux.active_pane]; if (!pane->active) return;
    if (pane->scroll_offset > 0 && !pane->screen.in_alt_screen && vk != VK_PRIOR && vk != VK_NEXT) { pane->scroll_offset = 0; g_mux.needs_redraw = 1; }
    ScreenBuffer *scr = &pane->screen;

    if (vk == VK_BACK) {
        int del_wchars = get_prev_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (del_wchars < 1) del_wchars = 1;
        if (pane->input_history_pos >= del_wchars) {
            if (pane->input_history_pos < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos - del_wchars,
                        pane->input_history + pane->input_history_pos,
                        (pane->input_history_len - pane->input_history_pos) * sizeof(WCHAR));
            }
            pane->input_history_len -= del_wchars;
            pane->input_history_pos -= del_wchars;
        } else {
            pane->input_history_len = 0;
            pane->input_history_pos = 0;
        }

        if (scr->win32_input_mode) {
            for (int b = 0; b < del_wchars; b++) {
                char seq_d[64], seq_u[64];
                int sld = snprintf(seq_d, sizeof(seq_d), "\x1b[8;14;8;1;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_d, sld);
                int slu = snprintf(seq_u, sizeof(seq_u), "\x1b[8;14;8;0;%lu;1_", (unsigned long)ke->dwControlKeyState);
                write_to_pane(seq_u, slu);
            }
            return;
        } else {
            for (int b = 0; b < del_wchars; b++) {
                char c = is_ctrl ? 0x08 : 0x7F;
                write_to_pane(&c, 1);
            }
            return;
        }
    }

    if (vk == VK_DELETE) {
        int del_wchars = get_next_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (del_wchars < 1) del_wchars = 1;
        if (pane->input_history_pos + del_wchars <= pane->input_history_len) {
            if (pane->input_history_pos + del_wchars < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos,
                        pane->input_history + pane->input_history_pos + del_wchars,
                        (pane->input_history_len - pane->input_history_pos - del_wchars) * sizeof(WCHAR));
            }
            pane->input_history_len -= del_wchars;
        } else {
            pane->input_history_len = 0;
        }
    }

    if (vk == VK_LEFT) {
        int steps = get_prev_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (steps < 1) steps = 1;
        pane->input_history_pos -= steps;
        if (pane->input_history_pos < 0) pane->input_history_pos = 0;
    } else if (vk == VK_RIGHT) {
        int steps = get_next_grapheme_wchars(pane->input_history, pane->input_history_len, pane->input_history_pos);
        if (steps < 1) steps = 1;
        pane->input_history_pos += steps;
        if (pane->input_history_pos > pane->input_history_len) pane->input_history_pos = pane->input_history_len;
    } else if (vk == VK_HOME) {
        pane->input_history_pos = 0;
    } else if (vk == VK_END) {
        pane->input_history_pos = pane->input_history_len;
    } else if (vk == VK_RETURN) {
        pane->input_history_len = 0;
        pane->input_history_pos = 0;
    } else if (uc >= 0xD800 && uc <= 0xDBFF) {
        if (pane->input_history_len < 255) {
            if (pane->input_history_pos < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos + 1,
                        pane->input_history + pane->input_history_pos,
                        (pane->input_history_len - pane->input_history_pos) * sizeof(WCHAR));
            }
            pane->input_history[pane->input_history_pos++] = uc;
            pane->input_history_len++;
        }
    } else if (uc >= 0xDC00 && uc <= 0xDFFF) {
        if (pane->input_history_len < 255) {
            if (pane->input_history_pos < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos + 1,
                        pane->input_history + pane->input_history_pos,
                        (pane->input_history_len - pane->input_history_pos) * sizeof(WCHAR));
            }
            pane->input_history[pane->input_history_pos++] = uc;
            pane->input_history_len++;
        }
    } else if (uc >= 0x20 || uc == 0x200D || (uc >= 0xFE00 && uc <= 0xFE0F)) {
        if (pane->input_history_len < 255) {
            if (pane->input_history_pos < pane->input_history_len) {
                memmove(pane->input_history + pane->input_history_pos + 1,
                        pane->input_history + pane->input_history_pos,
                        (pane->input_history_len - pane->input_history_pos) * sizeof(WCHAR));
            }
            pane->input_history[pane->input_history_pos++] = uc;
            pane->input_history_len++;
        }
    }

    if (scr->win32_input_mode && !(uc >= 0xD800 && uc <= 0xDFFF)) {
        char seq[64];
        int sl = snprintf(seq, sizeof(seq), "\x1b[%u;%u;%u;1;%lu;%u_",
                          (unsigned int)ke->wVirtualKeyCode,
                          (unsigned int)ke->wVirtualScanCode,
                          (unsigned int)ke->uChar.UnicodeChar,
                          (unsigned long)ke->dwControlKeyState,
                          (unsigned int)ke->wRepeatCount);
        write_to_pane(seq, sl);
        return;
    }

    if (vk == VK_PRIOR) { if (!scr->in_alt_screen) do_scroll(scr->rows / 2); return; }
    if (vk == VK_NEXT) { if (!scr->in_alt_screen) do_scroll(-scr->rows / 2); return; }

    const char *seq = NULL;
    switch (vk) {
        case VK_UP: seq = scr->app_cursor_keys ? "\x1bOA" : "\x1b[A"; break;
        case VK_DOWN: seq = scr->app_cursor_keys ? "\x1bOB" : "\x1b[B"; break;
        case VK_RIGHT: seq = scr->app_cursor_keys ? "\x1bOC" : "\x1b[C"; break;
        case VK_LEFT: seq = scr->app_cursor_keys ? "\x1bOD" : "\x1b[D"; break;
        case VK_HOME: seq = "\x1b[H"; break;
        case VK_END: seq = "\x1b[F"; break;
        case VK_INSERT: seq = "\x1b[2~"; break;
        case VK_DELETE: seq = "\x1b[3~"; break;
        case VK_F1: seq = "\x1bOP"; break;
        case VK_F2: seq = "\x1bOQ"; break;
        case VK_F3: seq = "\x1bOR"; break;
        case VK_F4: seq = "\x1bOS"; break;
        case VK_F5: seq = "\x1b[15~"; break;
        case VK_F6: seq = "\x1b[17~"; break;
        case VK_F7: seq = "\x1b[18~"; break;
        case VK_F8: seq = "\x1b[19~"; break;
        case VK_F9: seq = "\x1b[20~"; break;
        case VK_F10: seq = "\x1b[21~"; break;
        case VK_F11: seq = "\x1b[23~"; break;
        case VK_F12: seq = "\x1b[24~"; break;
    }
    if (seq) {
        int sl = (int)strlen(seq);
        for (WORD r = 0; r < ke->wRepeatCount; r++) write_to_pane(seq, sl);
        return;
    }
    if (uc >= 0xD800 && uc <= 0xDBFF) { g_high_surrogate = uc; return; }
    if (uc >= 0xDC00 && uc <= 0xDFFF) {
        if (g_high_surrogate) {
            unsigned int cp = 0x10000 + (((unsigned int)(g_high_surrogate & 0x3FF)) << 10) + (uc & 0x3FF);
            g_high_surrogate = 0;
            char u8[4];
            u8[0] = (char)(0xF0 | (cp >> 18));
            u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            u8[3] = (char)(0x80 | (cp & 0x3F));
            for (WORD r = 0; r < ke->wRepeatCount; r++) write_to_pane(u8, 4);
            return;
        }
    }
    g_high_surrogate = 0;
    if (uc) {
        char u8[4]; int len = 0;
        if (uc < 0x80) { u8[0] = (char)uc; len = 1; }
        else if (uc < 0x800) { u8[0] = (char)(0xC0 | (uc >> 6)); u8[1] = (char)(0x80 | (uc & 0x3F)); len = 2; }
        else { u8[0] = (char)(0xE0 | (uc >> 12)); u8[1] = (char)(0x80 | ((uc >> 6) & 0x3F)); u8[2] = (char)(0x80 | (uc & 0x3F)); len = 3; }
        for (WORD r = 0; r < ke->wRepeatCount; r++) write_to_pane(u8, len);
        return;
    }
}

/* The exit confirmation used to swallow every mouse event, so its buttons
 * looked clickable but were not.  Hit testing reuses confirm_exit_button_geom
 * so the highlight and the click target are the same rectangle. */
static void handle_confirm_exit_mouse(MOUSE_EVENT_RECORD *me) {
    int mx = me->dwMousePosition.X, my = me->dwMousePosition.Y;
    if (mx != g_mouse_x || my != g_mouse_y) {
        g_mouse_x = mx;
        g_mouse_y = my;
        g_mux.needs_redraw = 1;
    }
    int pressed = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED |
                                        FROM_LEFT_2ND_BUTTON_PRESSED |
                                        RIGHTMOST_BUTTON_PRESSED)) != 0;
    if (!pressed || (me->dwEventFlags != 0 && me->dwEventFlags != DOUBLE_CLICK))
        return;
    int row, ys, ye, ns, ne;
    confirm_exit_button_geom(g_mux.host_rows, g_mux.host_cols, &row, &ys, &ye, &ns, &ne);
    int r = my + 1, c = mx + 1;
    if (r != row) return;
    if (c >= ys && c < ye) {
        g_mux.confirm_exit_mode = 0;
        g_mux.running = 0;
        return;
    }
    if (c >= ns && c < ne) {
        g_mux.confirm_exit_mode = 0;
        g_mux.needs_redraw = 1;
    }
}

/* 右上角状态徽章的鼠标交互：悬停要重绘（提示展开 / 按钮高亮），点击命中
 * 搜索的上一个 / 下一个 / 关闭。热区来自渲染同款 status_badge_layout()。 */
static int handle_status_badge_mouse(MOUSE_EVENT_RECORD *me) {
    StatusBadge b;
    if (!status_badge_layout(g_mux.host_cols, &b)) return 0;
    int mx = me->dwMousePosition.X, my = me->dwMousePosition.Y;
    int r = my + 1, c = mx + 1;
    int inside = (r == b.row && c >= b.start && c < b.end);
    if ((mx != g_mouse_x || my != g_mouse_y) && (inside || status_badge_hovered(&b))) {
        g_mouse_x = mx;
        g_mouse_y = my;
        g_mux.needs_redraw = 1;
    }
    if (!inside || b.kind != 2) return 0;
    int pressed = (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    if (!pressed || (me->dwEventFlags != 0 && me->dwEventFlags != DOUBLE_CLICK)) return inside;
    if (c >= b.prev_s && c < b.prev_e) { search_jump_prev(); return 1; }
    if (c >= b.next_s && c < b.next_e) { search_jump_next(); return 1; }
    if (c >= b.close_s && c < b.close_e) {
        g_search_active = 0;
        g_search_match_count = 0;
        g_search_match_cur = -1;
        g_ui_mode_pane = -1;
        g_mux.needs_redraw = 1;
        return 1;
    }
    return 1;
}

void handle_mouse(MOUSE_EVENT_RECORD *me) {
    if (!g_mouse_enabled) return;
    ui_modes_sync_pane();
    if (handle_status_badge_mouse(me)) return;
    if (g_mux.confirm_exit_mode) {
        handle_confirm_exit_mouse(me);
        return;
    }
    int mx = me->dwMousePosition.X, my = me->dwMousePosition.Y;
    log_mouse_event("ev", me);

    if (mx != g_mouse_x || my != g_mouse_y) {
        int prev_in = g_mouse_prev_in_tabbar;
        g_mouse_x = mx; g_mouse_y = my;
        int popup_open = (g_mux.settings_mode || g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode || g_mux.custom_cmd_mode || g_search_mode || g_mux.palette_mode);
        int now_in = (!popup_open && my == 0);

        int hover_pane = -1;
        if (!popup_open && my == 0) {
            for (int i = 0; i < g_mux.tab_count; i++) {
                PaneTabInfo *t = &g_mux.tab_info[i];
                if (mx >= t->start_col && mx < t->end_col && t->pane_idx >= 0) {
                    hover_pane = t->pane_idx;
                    break;
                }
            }
        }
        if (hover_pane >= 0 && hover_pane < g_mux.pane_count && g_mux.panes[hover_pane].active) {
            Pane *hp = &g_mux.panes[hover_pane];
            const char *full_title = hp->full_title[0] ? hp->full_title : (hp->title[0] ? hp->title : "cmd");
            if (utf8_cols(full_title, (int)strlen(full_title)) <= 15) {
                hover_pane = -1;
            }
        }
        if (hover_pane != g_hover_preview_pane) {
            if (g_hover_preview_active) g_mux.needs_redraw = 1;
            g_hover_preview_pane = hover_pane;
            g_hover_preview_start = (hover_pane >= 0) ? GetTickCount64() : 0;
            g_hover_preview_active = 0;
        }

        int sb_fade_active = 0;
        if (!popup_open && g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
            Pane *p = &g_mux.panes[g_mux.active_pane];
            if (!p->screen.in_alt_screen && (my >= 1 || prev_in == 0 || g_sb_dragging)) {
                sb_fade_active = 1;
            }
        }
        int in_settings_pane = 0;
        if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count &&
            g_mux.panes[g_mux.active_pane].active && g_mux.panes[g_mux.active_pane].is_settings) {
            in_settings_pane = 1;
        }
        if (now_in || (prev_in && !now_in) || sb_fade_active || g_sb_dragging ||
            in_settings_pane || g_mouse_selecting || g_copy_mode ||
            g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode || g_mux.custom_cmd_mode || g_search_mode || g_mux.palette_mode)
            g_mux.needs_redraw = 1;
        g_mouse_prev_in_tabbar = now_in;

        if (!prev_in && now_in) {
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
                ScreenBuffer *s = &g_mux.panes[g_mux.active_pane].screen;
                if (s->mouse_tracking) {
                    int x = mx + 1;
                    int safe_y = s->rows > 2 ? s->rows : 2;
                    char seq[64];
                    int len = 0;
                    if (s->mouse_sgr) {
                        len = snprintf(seq, sizeof(seq), "\x1b[<35;%d;%dm", x, safe_y);
                    } else if (x <= 223 && safe_y <= 223) {
                        seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'M';
                        seq[3] = 32 + 35; seq[4] = 32 + x; seq[5] = 32 + safe_y; len = 6;
                    }
                    if (len > 0) write_to_pane(seq, len);
                }
            }
        }
    }

    if (g_mux.palette_mode) {
        handle_palette_mouse(me);
        return;
    }

    if (my == 0) {
        int mbtn = -1;
        if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) mbtn = 0;
        else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) mbtn = 2;
        else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) mbtn = 1;

        if (mbtn >= 0 && (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK)) {
            if ((g_mux.chooser_mode || g_mux.ctx_mode || g_mux.rename_mode || g_mux.custom_cmd_mode || g_search_mode || g_mux.palette_mode)) {
                g_mux.chooser_mode = 0;
                g_mux.ctx_mode = 0;
                g_mux.rename_mode = 0;
                g_mux.custom_cmd_mode = 0;
                g_search_mode = 0;
                g_mux.palette_mode = 0;
                g_mux.needs_redraw = 1;
                return;
            }

            for (int i = 0; i < g_mux.tab_count; i++) {
                PaneTabInfo *t = &g_mux.tab_info[i];
                if (mx < t->start_col || mx >= t->end_col) continue;

                if (mbtn == 1 && t->pane_idx >= 0) {
                    int ci = t->pane_idx;
                    int was_active = (ci == g_mux.active_pane);
                    close_pane(ci);
                    if (was_active) {
                        int n = find_next_active_pane(ci);
                        if (n >= 0) { g_mux.active_pane = n; g_mux.panes[n].scroll_offset = 0; }
                        else {
                            int f = -1;
                            for (int k = 0; k < g_mux.pane_count; k++) if (g_mux.panes[k].active) { f = k; break; }
                            g_mux.active_pane = f;
                            if (f < 0) { g_mux.running = 0; return; }
                            g_mux.panes[f].scroll_offset = 0;
                        }
                    }
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (t->pane_idx == -2) {
                    g_mux.help_mode = !g_mux.help_mode;
                    if (!g_mux.help_mode) g_mux.help_scroll = 0;
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (t->pane_idx == -1) {
                    g_mux.ctx_mode = 0;
                    g_mux.rename_mode = 0;
                    g_mux.custom_cmd_mode = 0;
                    g_mux.chooser_mode = 1;
                    g_pop_anchor_x = mx;
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (t->pane_idx == -3) {
                    g_mux.chooser_mode = 0;
                    g_mux.ctx_mode = 0;
                    g_mux.rename_mode = 0;
                    g_mux.custom_cmd_mode = 0;
                    g_mux.help_mode = 0;
                    open_settings_pane();
                    g_mux.needs_redraw = 1;
                    return;
                }
                if (mbtn == 2) {
                    if (t->pane_idx >= 0 && t->pane_idx < g_mux.pane_count && g_mux.panes[t->pane_idx].active) {
                        if (!g_mux.panes[t->pane_idx].is_about && !g_mux.panes[t->pane_idx].is_settings) {
                            g_mux.chooser_mode = 0;
                            g_mux.rename_mode = 0;
                            g_mux.custom_cmd_mode = 0;
                            g_mux.ctx_mode = 1;
                            g_mux.ctx_pane = t->pane_idx;
                            g_pop_anchor_x = mx;
                            g_mux.needs_redraw = 1;
                        }
                    }
                    return;
                }
                if (mbtn != 0) return;
                if (!g_mux.panes[t->pane_idx].active) continue;
                if (mx >= t->close_start && mx < t->close_end) {
                    int ci = t->pane_idx;
                    int was_active = (ci == g_mux.active_pane);
                    close_pane(ci);
                    if (was_active) {
                        int n = find_next_active_pane(ci);
                        if (n >= 0) { g_mux.active_pane = n; g_mux.panes[n].scroll_offset = 0; }
                        else {
                            int f = -1;
                            for (int k = 0; k < g_mux.pane_count; k++) if (g_mux.panes[k].active) { f = k; break; }
                            g_mux.active_pane = f;
                            if (f < 0) { g_mux.running = 0; return; }
                            g_mux.panes[f].scroll_offset = 0;
                        }
                    }
                    g_mux.needs_redraw = 1;
                    return;
                }
                g_mux.help_mode = 0;
                switch_pane(t->pane_idx);
                return;
            }
            return;
        }
        return;
    }

    int popup_open = g_mux.chooser_mode || g_mux.ctx_mode;
    if (popup_open) {
        int pbtn = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
        if (pbtn && (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK)) {
            if (g_mux.ctx_mode) {
                int top = 2;
                int anchor0 = (g_pop_anchor_x >= 0) ? g_pop_anchor_x : g_mouse_x;
                int popup_w = (g_mux.ctx_mode == 1) ? CTX_W : CP_W;
                int left = popup_left_1based(anchor0, popup_w, g_mux.host_cols);
                int r = my + 1, c = mx + 1;
                if (g_mux.ctx_mode == 1) {
                    if (r == top + 1 && c >= left && c < left + CTX_W) {
                        g_mux.ctx_mode = 2;
                        g_mux.needs_redraw = 1;
                        return;
                    }
                    if (r == top + 2 && c >= left && c < left + CTX_W) {
                        g_mux.ctx_mode = 0;
                        g_mux.rename_mode = 1;
                        g_mux.rename_len = 0;
                        g_mux.rename_pos = 0;
                        g_mux.rename_buf[0] = 0;
                        g_mux.needs_redraw = 1;
                        return;
                    }
                } else {
                    int swatch = -1;
                    if (r == top + 1 || r == top + 2) {
                        int base = (r == top + 1) ? 1 : 5;
                        int dc = c - (left + 2);
                        if (dc >= 0) {
                            int which = dc / 4;
                            if (which >= 0 && which < 4) swatch = base + which;
                        }
                    }
                    if (swatch >= 1 && swatch <= 8) {
                        if (g_mux.ctx_pane >= 0 && g_mux.ctx_pane < g_mux.pane_count) {
                            if (!g_mux.panes[g_mux.ctx_pane].is_about && !g_mux.panes[g_mux.ctx_pane].is_settings)
                                g_mux.panes[g_mux.ctx_pane].color = swatch;
                        }
                        g_mux.ctx_mode = 0;
                        g_mux.needs_redraw = 1;
                        return;
                    }
                }
                g_mux.ctx_mode = 0;
                g_mux.needs_redraw = 1;
                return;
            }
            int top, left, cw, ch;
            chooser_geom(g_mux.host_rows, g_mux.host_cols, &top, &left, &cw, &ch);
            int r = my + 1, c = mx + 1;
            int in_box = (r >= top && r < top + ch && c >= left && c < left + cw);
            if (in_box) {
                for (int i = 0; i < g_chooser_item_count; i++) {
                    if (r == top + 1 + i) {
                        g_mux.chooser_mode = 0;
                        int ni = create_pane_from_item(i);
                        if (ni >= 0) switch_pane(ni);
                        g_mux.needs_redraw = 1;
                        return;
                    }
                }
                if (r == top + 1 + g_chooser_item_count) {
                    g_mux.chooser_mode = 0;
                    int ni = create_about_pane();
                    if (ni >= 0) switch_pane(ni);
                    g_mux.needs_redraw = 1;
                    return;
                }
            }
            g_mux.chooser_mode = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        return;
    }

    if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active && g_mux.panes[g_mux.active_pane].is_settings && my >= 1) {
        handle_settings_mouse(me);
        return;
    }
    if (g_mux.rename_mode || g_mux.custom_cmd_mode || g_search_mode) {
        return;
    }
    if (g_mux.help_mode) {
        if (me->dwEventFlags == MOUSE_WHEELED) {
            int d = (short)HIWORD(me->dwButtonState);
            g_mux.help_scroll += (d > 0 ? -3 : 3);
            g_mux.needs_redraw = 1;
        }
        return;
    }
    if (g_mux.active_pane < 0) return;
    Pane *p = &g_mux.panes[g_mux.active_pane];
    if (!p->active) return;
    ScreenBuffer *s = &p->screen;
    if (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) {
        p->input_history_len = 0;
        p->input_history_pos = 0;
    }

    if (s->hist_lines > 0 && !s->in_alt_screen) {
        int has_btn = (me->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) != 0;
        if (has_btn) {
            int hist = s->hist_lines;
            int pane_rows = s->rows < g_mux.host_rows ? s->rows : g_mux.host_rows;
            if (pane_rows > 1) {
                int total = hist + pane_rows;
                int th = (pane_rows * pane_rows) / total;
                if (th < 1) th = 1;
                if (th >= pane_rows) th = pane_rows - 1;
                int max_tpos = pane_rows - th;
                if (max_tpos <= 0) max_tpos = 1;

                int vtop = hist - p->scroll_offset;
                int cur_tpos = (vtop * max_tpos + hist / 2) / hist;
                if (cur_tpos < 0) cur_tpos = 0;
                if (cur_tpos + th > pane_rows) cur_tpos = pane_rows - th;
                int sb_top = cur_tpos;
                int sb_bot = cur_tpos + th;

                int click_y = my - 1;
                if (click_y < 0) click_y = 0;
                if (click_y >= pane_rows) click_y = pane_rows - 1;

                if (!g_sb_dragging) {
                    if (mx == g_mux.host_cols - 1 && my >= 1) {
                        if (click_y >= sb_top && click_y < sb_bot) {
                            g_sb_dragging = 1;
                            g_sb_grab_offset = click_y - sb_top;
                            return;
                        } else {
                            int center_offset = th / 2;
                            if (th % 2 == 0) {
                                if (click_y < sb_top) center_offset = (th / 2) - 1;
                                else center_offset = th / 2;
                            }
                            g_sb_dragging = 1;
                            g_sb_grab_offset = center_offset;
                            int desired_tpos = click_y - center_offset;
                            if (desired_tpos < 0) desired_tpos = 0;
                            if (desired_tpos > max_tpos) desired_tpos = max_tpos;
                            int new_vtop = (desired_tpos * hist + max_tpos / 2) / max_tpos;
                            p->scroll_offset = hist - new_vtop;
                            if (p->scroll_offset < 0) p->scroll_offset = 0;
                            if (p->scroll_offset > hist) p->scroll_offset = hist;
                            g_mux.needs_redraw = 1;
                            return;
                        }
                    }
                } else {
                    int desired_tpos = click_y - g_sb_grab_offset;
                    if (desired_tpos < 0) desired_tpos = 0;
                    if (desired_tpos > max_tpos) desired_tpos = max_tpos;
                    int new_vtop = (desired_tpos * hist + max_tpos / 2) / max_tpos;
                    int new_vo = hist - new_vtop;
                    if (new_vo < 0) new_vo = 0;
                    if (new_vo > hist) new_vo = hist;
                    if (new_vo != p->scroll_offset) {
                        p->scroll_offset = new_vo;
                        g_mux.needs_redraw = 1;
                    }
                    return;
                }
            }
        } else {
            g_sb_dragging = 0;
            g_sb_grab_offset = 0;
        }
    } else {
        g_sb_dragging = 0;
        g_sb_grab_offset = 0;
    }

    /* Shift/Alt + click marks two corners of a selection and drops straight
     * into copy mode: Shift keeps the text-flow selection, Alt makes it a
     * rectangular block.  This deliberately runs before the drag-select and
     * before mouse_tracking forwarding, so it also works inside full-screen
     * apps that grabbed the mouse. */
    if (!p->is_settings && my >= 1 && !g_sb_dragging &&
        (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK ||
         (me->dwEventFlags == MOUSE_MOVED && g_copy_mode && g_copy_quick)) &&
        (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
        int quick_shift = (me->dwControlKeyState & SHIFT_PRESSED) != 0;
        int quick_alt = (me->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
        if (quick_shift || quick_alt || (g_copy_mode && g_copy_quick)) {
            int click_x = mx;
            if (click_x < 0) click_x = 0;
            if (click_x > s->cols - 1) click_x = s->cols - 1;
            int click_y = my - 1;
            if (click_y > s->rows - 1) click_y = s->rows - 1;
            if (!g_copy_mode || !g_copy_quick) {
                /* First corner. 起点落在宽字符次格时左退到主格，保证整字。 */
                const WCHAR *sl = copy_line_at(s, p, click_y);
                int ax = sl ? snap_left_to_char(sl, s->cols, click_x) : click_x;
                g_copy_mode = 1;
                ui_modes_claim();
                g_copy_quick = 1;
                g_copy_cx = ax;
                g_copy_cy = click_y;
                g_copy_anchor_x = ax;
                g_copy_anchor_abs_y = screen_to_abs_row(s, click_y, p->scroll_offset);
                g_copy_sel_active = 1;
                g_copy_block = quick_alt ? 1 : 0;
            } else {
                /* Second corner: only the end point moves. */
                /* 拖动过程整字化：向右扩时指针落在汉字主格就跳到次格（一次跨过
                 * 整字），向左扩时落在次格就退到主格——光标永不停在半个字上。 */
                const WCHAR *sl = copy_line_at(s, p, click_y);
                int cx = sl ? copy_quantize_cursor(sl, s->cols, click_x, g_copy_anchor_x)
                            : click_x;
                g_copy_cx = cx;
                g_copy_cy = click_y;
                if (quick_alt) g_copy_block = 1;
                else if (quick_shift) g_copy_block = 0;
            }
            g_mouse_selecting = 0;
            g_mux.needs_redraw = 1;
            return;
        }
    }

    if (!s->mouse_tracking && !p->is_settings && my >= 1 && !g_sb_dragging) {
        if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
            int abs_y = screen_to_abs_row(s, my - 1, p->scroll_offset);
            int cur_x = mx < s->cols ? mx : s->cols - 1;
            if (cur_x < 0) cur_x = 0;
            /* 普通左键拖选也整字化：起点落宽字符次格退到字首，拖动终点
             * 向右扩到字尾/向左收到字首——鼠标移过中文/emoji 一次跨过整字。 */
            const WCHAR *sl = (s->in_alt_screen && s->alt_buffer)
                ? &s->alt_buffer[(size_t)(my - 1) * s->cols].Char.UnicodeChar
                : NULL;
            if (!sl && !s->in_alt_screen) {
                int ar = screen_phys_row(s, my - 1 - p->scroll_offset);
                if (ar >= 0 && s->lines && s->lines[ar].cells)
                    sl = &s->lines[ar].cells[0].Char.UnicodeChar;
            }
            if (!g_mouse_selecting) {
                g_mouse_selecting = 1;
                int sx = sl ? snap_left_to_char(sl, s->cols, cur_x) : cur_x;
                g_mouse_sel_sx = sx;
                g_mouse_sel_s_abs_y = abs_y;
                g_mouse_sel_ex = sx;
                g_mouse_sel_e_abs_y = abs_y;
                g_mux.needs_redraw = 1;
            } else {
                g_mouse_sel_ex = sl ? copy_quantize_cursor(sl, s->cols, cur_x, g_mouse_sel_sx) : cur_x;
                g_mouse_sel_e_abs_y = abs_y;
                g_mux.needs_redraw = 1;
            }
        } else {
            if (g_mouse_selecting) {
                if (g_copy_on_select &&
                    (g_mouse_sel_sx != g_mouse_sel_ex || g_mouse_sel_s_abs_y != g_mouse_sel_e_abs_y)) {
                    copy_range_to_clipboard(p, g_mouse_sel_sx, g_mouse_sel_s_abs_y, g_mouse_sel_ex, g_mouse_sel_e_abs_y);
                }
                g_mouse_selecting = 0;
                g_mux.needs_redraw = 1;
            }
        }
    }

    if (me->dwEventFlags == MOUSE_WHEELED) {
        int d = (short)HIWORD(me->dwButtonState);
        if (s->mouse_tracking) {
            int x = mx + 1, y = my;
            if (x < 1) x = 1;
            if (x > s->cols) x = s->cols;
            if (y < 1) y = 1;
            if (y > s->rows) y = s->rows;
            char seq[64]; int len = 0;
            if (s->mouse_sgr) {
                int btn = d > 0 ? 64 : 65;
                len = snprintf(seq, sizeof(seq), "\x1b[<%d;%d;%dM", btn, x, y);
            } else if (x <= 223 && y <= 223) {
                seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'M';
                seq[3] = 32 + (d > 0 ? 64 : 65);
                seq[4] = 32 + x; seq[5] = 32 + y;
                len = 6;
            }
            if (len > 0) write_to_pane(seq, len);
            return;
        }
        if (!s->in_alt_screen) do_scroll(d > 0 ? 3 : -3);
        return;
    }
    if (s->mouse_tracking == 0) {
        return;
    }
    int x = mx + 1, y = my;
    if (x < 1) x = 1;
    if (x > s->cols) x = s->cols;
    if (y < 1) y = 1;
    if (y > s->rows) y = s->rows;
    char seq[64]; int len = 0;
    if (s->mouse_sgr) {
        int btn = 0; char act = 'M';
        if (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK) {
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn = 0;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn = 1;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn = 2;
            else { btn = 0; act = 'm'; }
        } else if (me->dwEventFlags == MOUSE_MOVED) {
            if (s->mouse_tracking < 1002) return;
            btn = 32;
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn += 0;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn += 2;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn += 1;
            else if (s->mouse_tracking < 1003) return;
            else btn += 3;
        } else return;
        if (me->dwControlKeyState & SHIFT_PRESSED) btn |= 4;
        if (me->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) btn |= 8;
        if (me->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) btn |= 16;
        len = snprintf(seq, sizeof(seq), "\x1b[<%d;%d;%d%c", btn, x, y, act);
    }
    else {
        int btn = 0;
        if (me->dwEventFlags == 0 || me->dwEventFlags == DOUBLE_CLICK) {
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn = 0;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn = 1;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn = 2;
            else btn = 3;
        } else if (me->dwEventFlags == MOUSE_MOVED) {
            if (s->mouse_tracking < 1002) return;
            btn = 32;
            if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) btn += 0;
            else if (me->dwButtonState & RIGHTMOST_BUTTON_PRESSED) btn += 2;
            else if (me->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) btn += 1;
            else if (s->mouse_tracking < 1003) return;
            else btn += 3;
        } else return;
        if (me->dwControlKeyState & SHIFT_PRESSED) btn |= 4;
        if (me->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) btn |= 8;
        if (me->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) btn |= 16;
        if (x > 223 || y > 223) return;
        seq[0] = '\x1b'; seq[1] = '['; seq[2] = 'M';
        seq[3] = 32 + btn; seq[4] = 32 + x; seq[5] = 32 + y; len = 6;
    }
    if (len > 0) write_to_pane(seq, len);
}
