#include "input.h"

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
                WCHAR c1 = towlower(row_chars[x + k]);
                WCHAR c2 = towlower(wquery[k]);
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
    g_mux.palette_field = 0;
    g_mux.palette_edit_idx = -1;
    g_mux.palette_edit_new = 0;
}

static void palette_push_page(int page) {
    if (g_mux.palette_stack_len < PALETTE_STACK_MAX)
        g_mux.palette_stack[g_mux.palette_stack_len++] = g_mux.palette_page;
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
        g_mux.palette_page = g_mux.palette_stack[--g_mux.palette_stack_len];
        palette_reset_query();
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
    g_copy_sel_active = 0;
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
    } else if (preset_index >= 0 && preset_index < g_preset_count) {
        snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[idx].name), "%s", g_presets[preset_index].name);
        snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[idx].cmd), "%s", g_presets[preset_index].cmd);
        g_chooser_items[idx].workdir[0] = 0;
        if (strcmp(g_chooser_items[idx].cmd, ":custom") == 0)
            snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[idx].cmd), "cmd.exe");
    } else {
        snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[idx].name), "新 panel");
        snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[idx].cmd), "cmd.exe");
        g_chooser_items[idx].workdir[0] = 0;
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
        case PALETTE_ACTION_GRAPHICAL_SETTINGS:
            palette_close();
            open_settings_pane();
            g_mux.needs_redraw = 1;
            break;
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
            g_mux.running = 0;
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
        g_mux.palette_field = is_shift ? (g_mux.palette_field + 2) % 3 : (g_mux.palette_field + 1) % 3;
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

    if (vk == VK_ESCAPE) {
        palette_pop_page();
        return;
    }
    if (vk == VK_RETURN) {
        if (count > 0 && g_mux.palette_sel >= 0 && g_mux.palette_sel < count)
            execute_palette_command(filtered[g_mux.palette_sel]);
        return;
    }
    if (vk == VK_UP || (vk == 'P' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)))) {
        if (g_mux.palette_sel > 0) g_mux.palette_sel--;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DOWN || (vk == 'N' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)))) {
        if (g_mux.palette_sel < count - 1) g_mux.palette_sel++;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_PRIOR) {
        g_mux.palette_sel -= visible;
        if (g_mux.palette_sel < 0) g_mux.palette_sel = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_NEXT) {
        g_mux.palette_sel += visible;
        if (g_mux.palette_sel >= count) g_mux.palette_sel = count > 0 ? count - 1 : 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_LEFT && !g_mux.palette_query_len && g_mux.palette_page != PALETTE_PAGE_ROOT) {
        palette_pop_page();
        return;
    }
    if (vk == VK_RIGHT && count > 0 && g_mux.palette_sel >= 0 && g_mux.palette_sel < count) {
        execute_palette_command(filtered[g_mux.palette_sel]);
        return;
    }
    if (vk == VK_BACK) {
        buf_backspace(g_mux.palette_query, &g_mux.palette_query_len, &g_mux.palette_query_pos);
        g_mux.palette_sel = 0;
        g_mux.palette_scroll = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DELETE) {
        buf_delete(g_mux.palette_query, &g_mux.palette_query_len, &g_mux.palette_query_pos);
        g_mux.palette_sel = 0;
        g_mux.palette_scroll = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_HOME) {
        g_mux.palette_query_pos = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_END) {
        g_mux.palette_query_pos = g_mux.palette_query_len;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_LEFT) {
        g_mux.palette_query_pos = utf8_prev_grapheme(g_mux.palette_query, g_mux.palette_query_pos);
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_RIGHT) {
        g_mux.palette_query_pos = utf8_next_grapheme(g_mux.palette_query, g_mux.palette_query_len, g_mux.palette_query_pos);
        g_mux.needs_redraw = 1;
        return;
    }
    if (!g_mux.palette_query_len &&
        ((uc >= '1' && uc <= '9') || (vk >= '1' && vk <= '9') || (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9))) {
        int number = (uc >= '1' && uc <= '9') ? uc - '0' :
                     ((vk >= '1' && vk <= '9') ? vk - '0' : vk - VK_NUMPAD1 + 1);
        if (number >= 1 && number <= count) {
            g_mux.palette_sel = number - 1;
            execute_palette_command(filtered[g_mux.palette_sel]);
            return;
        }
    }

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

    if (r == top + 8 && in_box) {
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
        int direction = (short)HIWORD(me->dwButtonState);
        int filtered[64];
        int count = palette_filter_cmds(g_mux.palette_page, filtered, 64, g_mux.palette_query);
        int step = palette_visible_rows(g_mux.host_rows) > 1 ? 2 : 1;
        if (direction > 0) {
            g_mux.palette_sel -= step;
            if (g_mux.palette_sel < 0) g_mux.palette_sel = 0;
        } else if (count > 0) {
            g_mux.palette_sel += step;
            if (g_mux.palette_sel >= count) g_mux.palette_sel = count - 1;
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

    if (r >= row_start && r < row_end && c >= left && c < left + pw) {
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
    if (!p) return;
    ScreenBuffer *s = &p->screen;
    if (sy_abs > ey_abs || (sy_abs == ey_abs && sx > ex)) {
        int tx = sx; sx = ex; ex = tx;
        int ty = sy_abs; sy_abs = ey_abs; ey_abs = ty;
    }
    int total_lines = ey_abs - sy_abs + 1;
    if (total_lines <= 0 || total_lines > SCROLL_BUF_LINES + s->rows) return;

    int max_chars = total_lines * (s->cols + 2) + 64;
    WCHAR *wbuf = (WCHAR *)malloc(max_chars * sizeof(WCHAR));
    if (!wbuf) return;
    int wlen = 0;

    EnterCriticalSection(&g_mux.cs);
    for (int abs_y = sy_abs; abs_y <= ey_abs; abs_y++) {
        int x_start = (abs_y == sy_abs) ? sx : 0;
        int x_end = (abs_y == ey_abs) ? ex : s->cols - 1;
        if (x_start < 0) x_start = 0;
        if (x_end >= s->cols) x_end = s->cols - 1;

        int row_wlen_start = wlen;
        for (int x = x_start; x <= x_end; x++) {
            CHAR_INFO *cell = NULL;
            if (s->in_alt_screen) {
                if (abs_y >= 0 && abs_y < s->rows && s->alt_buffer) cell = &s->alt_buffer[abs_y * s->cols + x];
            } else {
                int ar = abs_y;
                if (ar >= 0 && ar < s->total_lines && s->lines) {
                    int pr = (s->scroll_top - s->hist_lines + ar + s->total_lines * 2) % s->total_lines;
                    if (pr >= 0 && pr < s->total_lines && s->lines[pr].cells)
                        cell = &s->lines[pr].cells[x];
                }
            }
            if (cell) {
                WCHAR ch = cell->Char.UnicodeChar;
                if (ch != 0) {
                    wbuf[wlen++] = ch;
                }
            } else {
                wbuf[wlen++] = L' ';
            }
        }
        if (abs_y < ey_abs) {
            while (wlen > row_wlen_start && wbuf[wlen - 1] == L' ') wlen--;
            wbuf[wlen++] = L'\r';
            wbuf[wlen++] = L'\n';
        }
    }
    while (wlen > 0 && wbuf[wlen - 1] == L' ') wlen--;
    wbuf[wlen] = 0;
    LeaveCriticalSection(&g_mux.cs);

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
        CloseClipboard();
    }
    free(wbuf);
}

void handle_copy_mode_key(KEY_EVENT_RECORD *ke) {
    WORD vk = ke->wVirtualKeyCode; WCHAR uc = ke->uChar.UnicodeChar;

    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) {
        g_copy_mode = 0; g_copy_sel_active = 0; g_mux.needs_redraw = 1; return;
    }
    Pane *p = &g_mux.panes[g_mux.active_pane];
    ScreenBuffer *s = &p->screen;

    if (vk == VK_ESCAPE || uc == 'q' || uc == 'Q') {
        g_copy_mode = 0;
        g_copy_sel_active = 0;
        p->scroll_offset = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (uc == '/' || vk == VK_OEM_2) {
        g_search_mode = 1;
        g_search_len = 0;
        g_search_pos = 0;
        g_search_buf[0] = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (vk == VK_SPACE || uc == 'v' || uc == 'V') {
        g_copy_sel_active = !g_copy_sel_active;
        if (g_copy_sel_active) {
            g_copy_anchor_x = g_copy_cx;
            g_copy_anchor_abs_y = screen_to_abs_row(s, g_copy_cy, p->scroll_offset);
        }
        g_mux.needs_redraw = 1;
        return;
    }

    if (vk == VK_RETURN || uc == 'y' || uc == 'Y') {
        if (g_copy_sel_active) {
            int cur_abs_y = screen_to_abs_row(s, g_copy_cy, p->scroll_offset);
            copy_range_to_clipboard(p, g_copy_anchor_x, g_copy_anchor_abs_y, g_copy_cx, cur_abs_y);
        }
        g_copy_mode = 0;
        g_copy_sel_active = 0;
        p->scroll_offset = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (vk == VK_LEFT || uc == 'h' || uc == 'H') {
        if (g_copy_cx > 0) g_copy_cx--;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_RIGHT || uc == 'l' || uc == 'L') {
        if (g_copy_cx < s->cols - 1) g_copy_cx++;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_UP || uc == 'k' || uc == 'K') {
        if (g_copy_cy > 0) {
            g_copy_cy--;
        } else if (p->scroll_offset < s->hist_lines) {
            p->scroll_offset++;
        }
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_DOWN || uc == 'j' || uc == 'J') {
        if (g_copy_cy < s->rows - 1) {
            g_copy_cy++;
        } else if (p->scroll_offset > 0) {
            p->scroll_offset--;
        }
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_PRIOR) {
        p->scroll_offset += s->rows / 2;
        if (p->scroll_offset > s->hist_lines) p->scroll_offset = s->hist_lines;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_NEXT) {
        p->scroll_offset -= s->rows / 2;
        if (p->scroll_offset < 0) p->scroll_offset = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_HOME || uc == '0' || uc == '^') {
        g_copy_cx = 0;
        g_mux.needs_redraw = 1;
        return;
    }
    if (vk == VK_END || uc == '$') {
        g_copy_cx = s->cols - 1;
        g_mux.needs_redraw = 1;
        return;
    }
}

void handle_settings_key(KEY_EVENT_RECORD *ke) {
    WORD vk = ke->wVirtualKeyCode; DWORD ctrl = ke->dwControlKeyState; WCHAR uc = ke->uChar.UnicodeChar;
    BOOL is_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    BOOL is_shift = (ctrl & SHIFT_PRESSED) != 0;
    BOOL is_alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

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
        if (g_settings_nav >= 1) {
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

    if ((is_ctrl || is_alt) && vk == VK_UP) {
        if (g_settings_nav > 0) {
            g_settings_nav--;
            if (g_settings_nav >= 1) load_item_to_editor(g_settings_nav - 1);
            g_mux.needs_redraw = 1;
            return;
        }
    }
    if ((is_ctrl || is_alt) && vk == VK_DOWN) {
        if (g_settings_nav < g_chooser_item_count) {
            g_settings_nav++;
            load_item_to_editor(g_settings_nav - 1);
            g_mux.needs_redraw = 1;
            return;
        }
    }

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

        if (uc == 'u' || uc == 'U' || (is_shift && vk == VK_UP)) {
            int i = g_settings_table_sel;
            if (i > 0 && i < g_chooser_item_count) {
                ChooserItem tmp = g_chooser_items[i];
                g_chooser_items[i] = g_chooser_items[i - 1];
                g_chooser_items[i - 1] = tmp;
                g_settings_table_sel = i - 1;
                save_config();
                g_mux.needs_redraw = 1;
            }
            return;
        }

        if (uc == 'd' || uc == 'D' || (is_shift && vk == VK_DOWN)) {
            int i = g_settings_table_sel;
            if (i >= 0 && i < g_chooser_item_count - 1) {
                ChooserItem tmp = g_chooser_items[i];
                g_chooser_items[i] = g_chooser_items[i + 1];
                g_chooser_items[i + 1] = tmp;
                g_settings_table_sel = i + 1;
                save_config();
                g_mux.needs_redraw = 1;
            }
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
                g_settings_field = (g_settings_field + 2) % 3;
            } else {
                g_settings_field = (g_settings_field + 1) % 3;
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
            if (r == 14) {
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

void handle_prefix(WORD vk, DWORD ctrl, WCHAR uc) {
    g_mux.prefix_mode = 0;
    if (vk == 'B' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) { char c = 2; write_to_pane(&c, 1); return; }

    if (uc == '+' || vk == VK_ADD || (vk == VK_OEM_PLUS && (ctrl & SHIFT_PRESSED))) {
        g_mux.ctx_mode = 0;
        g_mux.rename_mode = 0;
        g_mux.custom_cmd_mode = 0;
        g_mux.help_mode = 0;
        g_mux.palette_mode = 0;
        g_mux.chooser_mode = 1;
        g_pop_anchor_x = 20;
        for (int k = 0; k < g_mux.tab_count; k++) {
            if (g_mux.tab_info[k].pane_idx == -1) {
                g_pop_anchor_x = g_mux.tab_info[k].start_col;
                break;
            }
        }
        g_mux.needs_redraw = 1;
        return;
    }

    /* The command palette has one deliberate entry point: Ctrl+B :.  Keep
     * Ctrl+B p available for previous-panel navigation instead of treating
     * P/Space/another global shortcut as a palette alias. */
    /* Chinese keyboard layouts often deliver Shift+; as fullwidth U+FF1A
     * instead of ASCII ':'.  Accept both forms for Ctrl+B :. */
    if (uc == ':' || uc == 0xFF1A || (vk == VK_OEM_1 && (ctrl & SHIFT_PRESSED))) {
        g_mux.ctx_mode = 0;
        g_mux.rename_mode = 0;
        g_mux.custom_cmd_mode = 0;
        g_mux.help_mode = 0;
        g_mux.chooser_mode = 0;
        open_command_palette();
        return;
    }

    if (uc == '/' || (vk == VK_OEM_2 && !(ctrl & SHIFT_PRESSED))) {
        g_mux.palette_mode = 0;
        g_search_mode = 1;
        g_search_len = 0;
        g_search_pos = 0;
        g_search_buf[0] = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (uc == '?' || uc == 'h' || uc == 'H' || (vk == VK_OEM_2 && (ctrl & SHIFT_PRESSED))) {
        g_mux.help_mode = !g_mux.help_mode;
        if (!g_mux.help_mode) g_mux.help_scroll = 0;
        g_mux.chooser_mode = 0;
        g_mux.ctx_mode = 0;
        g_mux.rename_mode = 0;
        g_mux.custom_cmd_mode = 0;
        g_mux.palette_mode = 0;
        g_mux.needs_redraw = 1;
        return;
    }

    if (uc == '[' || vk == VK_OEM_4 || vk == '[') {
        if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
            Pane *p = &g_mux.panes[g_mux.active_pane];
            g_copy_mode = 1;
            g_copy_sel_active = 0;
            g_copy_cx = p->screen.cursor_x;
            g_copy_cy = p->screen.cursor_y;
            g_mux.needs_redraw = 1;
            return;
        }
    }

    if (vk == 'R' || uc == 'r' || uc == 'R') {
        load_config();
        g_mux.needs_redraw = 1;
        return;
    }

    switch (vk) {
        case 'C': { int i = create_pane(); if (i >= 0) switch_pane(i); break; }
        case 'N': { int n = find_next_active_pane(g_mux.active_pane); if (n >= 0) switch_pane(n); break; }
        case 'P': { for (int i = 1; i <= g_mux.pane_count; i++) { int n = (g_mux.active_pane - i + g_mux.pane_count) % g_mux.pane_count; if (g_mux.panes[n].active) { switch_pane(n); break; } } break; }
        case 'X': { int c = g_mux.active_pane, n = find_next_active_pane(c); close_pane(c); if (n >= 0 && g_mux.panes[n].active) switch_pane(n); else { int f = 0; for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active) { switch_pane(i); f = 1; break; } if (!f) g_mux.running = 0; } break; }
        case 'D': {
            g_mux.running = 0;
            break;
        }
        case 'T': {
            if (g_mux.active_pane >= 0 && g_mux.active_pane < g_mux.pane_count && g_mux.panes[g_mux.active_pane].active) {
                if (!g_mux.panes[g_mux.active_pane].is_about && !g_mux.panes[g_mux.active_pane].is_settings) {
                    int c = g_mux.panes[g_mux.active_pane].color;
                    c += (ctrl & SHIFT_PRESSED) ? -1 : 1;
                    if (c > 8) c = 1;
                    if (c < 1) c = 8;
                    g_mux.panes[g_mux.active_pane].color = c;
                    g_mux.needs_redraw = 1;
                }
            }
            break;
        }
        case 'S': {
            g_mux.chooser_mode = 0;
            g_mux.ctx_mode = 0;
            g_mux.rename_mode = 0;
            g_mux.custom_cmd_mode = 0;
            g_mux.help_mode = 0;
            open_settings_pane();
            g_mux.needs_redraw = 1;
            break;
        }
        default:
            if (vk >= '0' && vk <= '9') { int i = vk - '0'; if (i < g_mux.pane_count && g_mux.panes[i].active) switch_pane(i); }
            else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { int i = vk - VK_NUMPAD0; if (i < g_mux.pane_count && g_mux.panes[i].active) switch_pane(i); }
            break;
    }
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

    if (g_mux.palette_mode) {
        handle_palette_key(ke);
        return;
    }

    if (g_search_mode) {
        handle_search_key(ke);
        return;
    }

    if (g_search_active && !g_mux.prefix_mode && !g_copy_mode) {
        if (vk == VK_ESCAPE) {
            g_search_active = 0;
            g_mux.needs_redraw = 1;
            return;
        }
        if (uc == 'n' && !is_ctrl && !is_alt) {
            search_jump_next();
            return;
        }
        if ((uc == 'N' || (vk == 'N' && is_shift)) && !is_ctrl && !is_alt) {
            search_jump_prev();
            return;
        }
    }

    if (g_mux.prefix_mode) {
        if (vk == VK_SHIFT || vk == 0x10 || vk == 0xA0 || vk == 0xA1 ||
            vk == VK_CONTROL || vk == 0x11 || vk == 0xA2 || vk == 0xA3 ||
            vk == VK_MENU || vk == 0x12 || vk == 0xA4 || vk == 0xA5 ||
            vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) {
            return;
        }
        handle_prefix(vk, ctrl, uc);
        return;
    }
    if (g_copy_mode && !g_mux.prefix_mode) {
        handle_copy_mode_key(ke);
        return;
    }

    if ((uc == 0x02) || (vk == 'B' && is_ctrl && !is_alt && !is_shift)) { g_mux.prefix_mode = 1; return; }

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

void handle_mouse(MOUSE_EVENT_RECORD *me) {
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

    if (!s->mouse_tracking && !p->is_settings && my >= 1 && !g_sb_dragging) {
        if (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
            int abs_y = screen_to_abs_row(s, my - 1, p->scroll_offset);
            int cur_x = mx < s->cols ? mx : s->cols - 1;
            if (cur_x < 0) cur_x = 0;
            if (!g_mouse_selecting) {
                g_mouse_selecting = 1;
                g_mouse_sel_sx = cur_x;
                g_mouse_sel_s_abs_y = abs_y;
                g_mouse_sel_ex = cur_x;
                g_mouse_sel_e_abs_y = abs_y;
                g_mux.needs_redraw = 1;
            } else {
                g_mouse_sel_ex = cur_x;
                g_mouse_sel_e_abs_y = abs_y;
                g_mux.needs_redraw = 1;
            }
        } else {
            if (g_mouse_selecting) {
                if (g_mouse_sel_sx != g_mouse_sel_ex || g_mouse_sel_s_abs_y != g_mouse_sel_e_abs_y) {
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
