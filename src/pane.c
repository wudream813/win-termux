#include "pane.h"
#include "render.h"

void write_to_pane_internal(Pane *pane, const char *data, int len) {
    if (!pane || !pane->active) return;
    DWORD w;
    WriteFile(pane->pipe_in, data, len, &w, NULL);
}

void write_to_pane(const char *data, int len) {
    if (g_mux.active_pane < 0 || g_mux.active_pane >= g_mux.pane_count) return;
    write_to_pane_internal(&g_mux.panes[g_mux.active_pane], data, len);
}

void pane_mark_dead(int idx) {
    if (idx < 0 || idx >= MAX_PANES) return;
    EnterCriticalSection(&g_mux.cs);
    Pane *pane = &g_mux.panes[idx];
    if (!pane->active) { LeaveCriticalSection(&g_mux.cs); return; }
    pane->active = 0;
    int next = -1;
    for (int i = 0; i < g_mux.pane_count; i++) if (g_mux.panes[i].active && i != idx) { next = i; break; }
    if (idx == g_mux.active_pane) {
        if (next >= 0) { g_mux.active_pane = next; g_mux.panes[next].scroll_offset = 0; }
        else g_mux.running = 0;
    }
    g_mux.needs_redraw = 1;
    LeaveCriticalSection(&g_mux.cs);
}

void reap_dead_panes(void) {
    for (int i = 0; i < g_mux.pane_count; i++) {
        Pane *p = &g_mux.panes[i];
        if (!p->active) {
            if (p->read_thread != NULL) close_pane(i);
            continue;
        }
        if (p->exited_hold) {
            continue;
        }
        if (p->process != NULL && WaitForSingleObject(p->process, 0) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(p->process, &exit_code);
            if (exit_code != 0) {
                if (p->read_thread != NULL)
                    WaitForSingleObject(p->read_thread, 250);
                char msg[256];
                int mlen = snprintf(msg, sizeof(msg),
                    "\r\n\x1b[31;1m[进程异常退出，退出码: %lu (0x%lX)]\x1b[0m \x1b[33m按任意键关闭该标签页...\x1b[0m\r\n",
                    (unsigned long)exit_code, (unsigned long)exit_code);
                EnterCriticalSection(&g_mux.cs);
                screen_process_output(&p->screen, msg, mlen);
                p->exited_hold = 1;
                p->exit_code = exit_code;
                g_mux.needs_redraw = 1;
                LeaveCriticalSection(&g_mux.cs);
                continue;
            }
            if (p->read_thread != NULL)
                WaitForSingleObject(p->read_thread, 250);
            pane_mark_dead(i);
            close_pane(i);
        }
    }
}

unsigned __stdcall pane_read_thread(void *arg) {
    int idx = (int)(intptr_t)arg;
    Pane *pane = &g_mux.panes[idx];
    char buf[READ_BUF_SIZE];
    while (pane->active) {
        DWORD br = 0;
        if (!ReadFile(pane->pipe_out, buf, sizeof(buf), &br, NULL) || br == 0) break;
        dump_pane_bytes(idx, buf, (int)br);
        EnterCriticalSection(&g_mux.cs);
        screen_process_output(&pane->screen, buf, br);
        DWORD avail = 0;
        while (PeekNamedPipe(pane->pipe_out, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            DWORD to_read = avail > sizeof(buf) ? sizeof(buf) : avail;
            DWORD br2 = 0;
            if (!ReadFile(pane->pipe_out, buf, to_read, &br2, NULL) || br2 == 0) break;
            dump_pane_bytes(idx, buf, (int)br2);
            screen_process_output(&pane->screen, buf, br2);
        }
        if (pane->screen.response_len > 0) {
            write_to_pane_internal(pane, pane->screen.response_buf, pane->screen.response_len);
            pane->screen.response_len = 0;
        }
        if (idx == g_mux.active_pane) g_mux.needs_redraw = 1;
        LeaveCriticalSection(&g_mux.cs);
    }
    pane_mark_dead(idx);
    return 0;
}

typedef LSTATUS (APIENTRY *RegOpenKeyExW_fn)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LSTATUS (APIENTRY *RegQueryValueExW_fn)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LSTATUS (APIENTRY *RegCloseKey_fn)(HKEY);

void get_system_version_string(char *out, int max_len) {
    out[0] = 0;
    HMODULE hAdv = LoadLibraryA("advapi32.dll");
    if (hAdv) {
        RegOpenKeyExW_fn pOpen = (RegOpenKeyExW_fn)(void*)GetProcAddress(hAdv, "RegOpenKeyExW");
        RegQueryValueExW_fn pQuery = (RegQueryValueExW_fn)(void*)GetProcAddress(hAdv, "RegQueryValueExW");
        RegCloseKey_fn pClose = (RegCloseKey_fn)(void*)GetProcAddress(hAdv, "RegCloseKey");
        if (pOpen && pQuery && pClose) {
            HKEY hKey;
            if (pOpen(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                WCHAR prod_name[128] = {0};
                WCHAR display_ver[64] = {0};
                WCHAR current_build[64] = {0};
                DWORD ubr = 0;
                DWORD size = sizeof(prod_name);
                pQuery(hKey, L"ProductName", NULL, NULL, (LPBYTE)prod_name, &size);
                size = sizeof(display_ver);
                pQuery(hKey, L"DisplayVersion", NULL, NULL, (LPBYTE)display_ver, &size);
                if (!display_ver[0]) {
                    size = sizeof(display_ver);
                    pQuery(hKey, L"ReleaseId", NULL, NULL, (LPBYTE)display_ver, &size);
                }
                size = sizeof(current_build);
                pQuery(hKey, L"CurrentBuild", NULL, NULL, (LPBYTE)current_build, &size);
                size = sizeof(ubr);
                pQuery(hKey, L"UBR", NULL, NULL, (LPBYTE)&ubr, &size);
                pClose(hKey);

                char u8_prod[128] = {0}, u8_disp[64] = {0}, u8_build[64] = {0};
                WideCharToMultiByte(CP_UTF8, 0, prod_name, -1, u8_prod, sizeof(u8_prod) - 1, NULL, NULL);
                WideCharToMultiByte(CP_UTF8, 0, display_ver, -1, u8_disp, sizeof(u8_disp) - 1, NULL, NULL);
                WideCharToMultiByte(CP_UTF8, 0, current_build, -1, u8_build, sizeof(u8_build) - 1, NULL, NULL);

                if (u8_prod[0] && u8_build[0]) {
                    if (ubr > 0 && u8_disp[0]) {
                        snprintf(out, max_len, "%s %s (Build %s.%lu)", u8_prod, u8_disp, u8_build, (unsigned long)ubr);
                    } else if (u8_disp[0]) {
                        snprintf(out, max_len, "%s %s (Build %s)", u8_prod, u8_disp, u8_build);
                    } else if (ubr > 0) {
                        snprintf(out, max_len, "%s (Build %s.%lu)", u8_prod, u8_build, (unsigned long)ubr);
                    } else {
                        snprintf(out, max_len, "%s (Build %s)", u8_prod, u8_build);
                    }
                    FreeLibrary(hAdv);
                    return;
                }
            }
        }
        FreeLibrary(hAdv);
    }
    snprintf(out, max_len, "Windows 10 / Windows 11 (NT 10.0)");
}

int create_about_pane(void) {
    int idx = -1;
    for (int i = 0; i < MAX_PANES; i++) {
        if (!g_mux.panes[i].active && g_mux.panes[i].read_thread == NULL) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;

    Pane *pane = &g_mux.panes[idx];
    memset(pane, 0, sizeof(*pane));
    int pane_cols = g_mux.host_cols;
    if (!screen_init(&pane->screen, pane_cols, g_mux.host_rows)) return -1;
    pane->screen.pane_index = idx;
    pane->screen.in_alt_screen = 1;
    pane->active = 1;
    pane->is_about = 1;
    pane->color = 7;
    snprintf(pane->title, sizeof(pane->title), "关于");
    snprintf(pane->full_title, sizeof(pane->full_title), "关于 termux (About)");

    if (idx >= g_mux.pane_count) g_mux.pane_count = idx + 1;

    char sys_ver[128] = {0};
    get_system_version_string(sys_ver, sizeof(sys_ver));

    char about_buf[2048];
    int len = snprintf(about_buf, sizeof(about_buf),
        "\x1b[?1049h\x1b[?25l\r\n"
        "  \x1b[38;2;255;255;255m\x1b[48;2;217;119;54;1m ╔══════════════════════════════════════════════════════════╗ \x1b[0m\r\n"
        "  \x1b[38;2;255;255;255m\x1b[48;2;217;119;54;1m ║                  termux - 关于 (About)                   ║ \x1b[0m\r\n"
        "  \x1b[38;2;255;255;255m\x1b[48;2;217;119;54;1m ╚══════════════════════════════════════════════════════════╝ \x1b[0m\r\n\r\n"
        "  \x1b[38;2;217;119;54;1mWindows 终端复用器 (Terminal Multiplexer)\x1b[0m\r\n"
        "  \x1b[38;2;139;148;158m基于 Windows ConPTY 的高性能单文件 C 终端复用多标签环境\x1b[0m\r\n\r\n"
        "  \x1b[38;2;48;54;61m────────────────────────────────────────────────────────────\x1b[0m\r\n"
        "  \x1b[38;2;217;119;54;1m■ 版本号 (Version)      :\x1b[0m \x1b[38;2;230;237;243;1mv" TERMUX_VERSION "\x1b[0m\r\n"
        "  \x1b[38;2;217;119;54;1m■ 作  者 (Author)       :\x1b[0m \x1b[38;2;63;185;80;1mwu_dream813\x1b[0m\r\n"
        "  \x1b[38;2;217;119;54;1m■ 系统版本 (OS Version) :\x1b[0m \x1b[38;2;230;237;243m%s\x1b[0m\r\n"
        "  \x1b[38;2;48;54;61m────────────────────────────────────────────────────────────\x1b[0m\r\n\r\n"
        "  \x1b[38;2;139;148;158m开源项目仓库 : \x1b[38;2;88;166;255;4mhttps://github.com/wudream813/win-termux\x1b[0m\r\n"
        "  \x1b[38;2;139;148;158m开源许可协议 : \x1b[38;2;230;237;243mMIT License\x1b[0m\r\n\r\n"
        "  \x1b[38;2;110;118;129m提示: 这是一个独立的关于标签页，可点击右上角 [x] 或按 Ctrl+B x 关闭\x1b[0m\r\n",
        sys_ver);

    EnterCriticalSection(&g_mux.cs);
    screen_process_output(&pane->screen, about_buf, len);
    LeaveCriticalSection(&g_mux.cs);

    return idx;
}

int create_pane_shell_with_dir(const WCHAR *shell, const WCHAR *workdir) {
    int idx = -1;
    for (int i = 0; i < MAX_PANES; i++)
        if (!g_mux.panes[i].active && g_mux.panes[i].read_thread == NULL) { idx = i; break; }
    if (idx < 0) return -1;

    Pane *pane = &g_mux.panes[idx]; memset(pane, 0, sizeof(*pane));
    int pane_cols = g_mux.host_cols;
    if (!screen_init(&pane->screen, pane_cols, g_mux.host_rows)) return -1;
    pane->screen.pane_index = idx;

    HANDLE pi_r = NULL, pi_w = NULL, po_r = NULL, po_w = NULL;
    COORD sz = {(SHORT)pane_cols, (SHORT)g_mux.host_rows};
    STARTUPINFOEXW si;
    SIZE_T as = 0;
    PROCESS_INFORMATION pi;
    WCHAR cmdline[256] = {0};
    WCHAR exp_dir[MAX_PATH] = {0};
    LPCWSTR cur_dir = NULL;
    BOOL created = FALSE;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.StartupInfo.cb = sizeof(si);

    if (!CreatePipe(&pi_r, &pi_w, NULL, 0)) goto create_fail;
    if (!CreatePipe(&po_r, &po_w, NULL, 0)) goto create_fail;
    if (FAILED(CreatePseudoConsole(sz, pi_r, po_w, 0, &pane->hpc))) goto create_fail;

    InitializeProcThreadAttributeList(NULL, 1, 0, &as);
    if (as == 0) goto create_fail;
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(as);
    if (!si.lpAttributeList) goto create_fail;
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &as)) goto attr_fail;
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pane->hpc, sizeof(HPCON), NULL, NULL)) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        goto attr_fail;
    }

    wcsncpy(cmdline, shell, 255); cmdline[255] = 0;
    if (workdir && *workdir) {
        ExpandEnvironmentStringsW(workdir, exp_dir, MAX_PATH - 1);
    }
    cur_dir = exp_dir[0] ? exp_dir : NULL;

    created = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, NULL, cur_dir,
                             &si.StartupInfo, &pi);
    if (!created && _wcsicmp(shell, L"cmd.exe") != 0 && _wcsicmp(shell, L"powershell.exe") != 0) {
        WCHAR fallback[300];
        _snwprintf(fallback, 299, L"cmd.exe /c %s", shell);
        created = CreateProcessW(NULL, fallback, NULL, NULL, FALSE,
                                 EXTENDED_STARTUPINFO_PRESENT, NULL, cur_dir,
                                 &si.StartupInfo, &pi);
    }
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    si.lpAttributeList = NULL;
    if (!created) {
        DWORD err = GetLastError();
        CloseHandle(pi_r); pi_r = NULL;
        CloseHandle(po_w); po_w = NULL;
        pane->pipe_in = pi_w; pane->pipe_out = po_r;
        pane->active = 1;
        pane->exited_hold = 1;
        pane->exit_code = err;
        char u8cmd[64] = {0};
        WideCharToMultiByte(CP_UTF8, 0, shell, -1, u8cmd, 63, NULL, NULL);
        char *space = strchr(u8cmd, ' ');
        if (space) *space = 0;
        sanitize_title(u8cmd, (int)strlen(u8cmd), pane->title, sizeof(pane->title));

        char errmsg[256];
        int elen = snprintf(errmsg, sizeof(errmsg),
            "\x1b[31;1m[启动失败: 无法执行命令 \"%s\" (错误码: %lu)]\x1b[0m\r\n\x1b[33m按任意键关闭该标签页...\x1b[0m\r\n",
            u8cmd, (unsigned long)err);
        EnterCriticalSection(&g_mux.cs);
        screen_process_output(&pane->screen, errmsg, elen);
        LeaveCriticalSection(&g_mux.cs);

        if (idx >= g_mux.pane_count) g_mux.pane_count = idx + 1;
        return idx;
    }

    CloseHandle(pi_r); pi_r = NULL;
    CloseHandle(po_w); po_w = NULL;
    pane->pipe_in = pi_w; pane->pipe_out = po_r; pane->process = pi.hProcess; pane->thread = pi.hThread; pane->active = 1;
    if (_wcsicmp(shell, L"powershell.exe") == 0 || _wcsicmp(shell, L"powershell") == 0) {
        snprintf(pane->title, sizeof(pane->title), "PowerShell");
        snprintf(pane->full_title, sizeof(pane->full_title), "powershell.exe");
    } else if (_wcsicmp(shell, L"cmd.exe") == 0 || _wcsicmp(shell, L"cmd") == 0) {
        snprintf(pane->title, sizeof(pane->title), "cmd");
        snprintf(pane->full_title, sizeof(pane->full_title), "cmd.exe");
    } else {
        char u8cmd[256] = {0};
        WideCharToMultiByte(CP_UTF8, 0, shell, -1, u8cmd, 255, NULL, NULL);
        snprintf(pane->full_title, sizeof(pane->full_title), "%s", u8cmd);
        char *space = strchr(u8cmd, ' ');
        if (space) *space = 0;
        sanitize_title(u8cmd, (int)strlen(u8cmd), pane->title, sizeof(pane->title));
    }
    if (idx >= g_mux.pane_count) g_mux.pane_count = idx + 1;
    pane->read_thread = (HANDLE)_beginthreadex(NULL, 0, pane_read_thread, (void*)(intptr_t)idx, 0, NULL);
    if (!pane->read_thread) {
        pane->active = 0;
        ClosePseudoConsole(pane->hpc);
        CloseHandle(pane->pipe_in); CloseHandle(pane->pipe_out);
        TerminateProcess(pane->process, 0); WaitForSingleObject(pane->process, 500);
        CloseHandle(pane->process); CloseHandle(pane->thread);
        screen_free(&pane->screen);
        return -1;
    }
    return idx;

attr_fail:
    free(si.lpAttributeList);
create_fail:
    if (pane->hpc) ClosePseudoConsole(pane->hpc);
    if (pi_r) CloseHandle(pi_r);
    if (pi_w) CloseHandle(pi_w);
    if (po_r) CloseHandle(po_r);
    if (po_w) CloseHandle(po_w);
    screen_free(&pane->screen);
    memset(pane, 0, sizeof(*pane));
    return -1;
}

int create_pane_shell(const WCHAR *shell) {
    return create_pane_shell_with_dir(shell, NULL);
}

int create_pane_from_item(int idx) {
    if (idx < 0 || idx >= g_chooser_item_count) return create_pane();
    if (strcmp(g_chooser_items[idx].cmd, ":custom") == 0) {
        g_mux.custom_cmd_mode = 1;
        g_mux.custom_cmd_len = 0;
        g_mux.custom_cmd_pos = 0;
        g_mux.custom_cmd_buf[0] = 0;
        g_pop_anchor_x = g_mouse_x >= 0 ? g_mouse_x : 10;
        g_mux.needs_redraw = 1;
        return -1;
    }
    WCHAR wcmd[256] = {0};
    WCHAR wdir[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, g_chooser_items[idx].cmd, -1, wcmd, 255);
    if (g_chooser_items[idx].workdir[0]) {
        MultiByteToWideChar(CP_UTF8, 0, g_chooser_items[idx].workdir, -1, wdir, 255);
    }
    int p = create_pane_shell_with_dir(wcmd, wdir[0] ? wdir : NULL);
    if (p >= 0) {
        strncpy(g_mux.panes[p].title, g_chooser_items[idx].name, sizeof(g_mux.panes[p].title) - 1);
        strncpy(g_mux.panes[p].full_title, g_chooser_items[idx].name, sizeof(g_mux.panes[p].full_title) - 1);
    }
    return p;
}

int create_pane(void) {
    if (g_chooser_item_count > 0 && strcmp(g_chooser_items[0].cmd, ":custom") != 0) {
        return create_pane_from_item(0);
    }
    return create_pane_shell(L"cmd.exe");
}

int open_settings_pane(void) {
    for (int i = 0; i < g_mux.pane_count; i++) {
        if (g_mux.panes[i].active && g_mux.panes[i].is_settings) {
            switch_pane(i);
            return i;
        }
    }
    int idx = -1;
    for (int i = 0; i < MAX_PANES; i++) {
        if (!g_mux.panes[i].active && g_mux.panes[i].read_thread == NULL) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;
    Pane *pane = &g_mux.panes[idx];
    memset(pane, 0, sizeof(*pane));
    int pane_cols = g_mux.host_cols;
    if (!screen_init(&pane->screen, pane_cols, g_mux.host_rows)) return -1;
    pane->screen.pane_index = idx;
    pane->screen.in_alt_screen = 1;
    pane->active = 1;
    pane->is_settings = 1;
    pane->color = 6;
    snprintf(pane->title, sizeof(pane->title), "设置");
    snprintf(pane->full_title, sizeof(pane->full_title), "termux - 设置 (Settings)");

    if (idx >= g_mux.pane_count) g_mux.pane_count = idx + 1;

    g_settings_nav = 0;
    g_settings_field = 0;
    g_settings_table_sel = 0;
    g_settings_show_presets = 0;
    g_preset_sel = 0;
    if (g_chooser_item_count > 0) {
        load_item_to_editor(0);
    }
    switch_pane(idx);
    return idx;
}

void close_pane(int idx) {
    if (idx < 0 || idx >= g_mux.pane_count) return;
    Pane *pane = &g_mux.panes[idx];

    EnterCriticalSection(&g_mux.cs);
    if (!pane->active && !pane->read_thread) { LeaveCriticalSection(&g_mux.cs); return; }
    pane->active = 0;
    LeaveCriticalSection(&g_mux.cs);

    if (pane->hpc) {
        ClosePseudoConsole(pane->hpc);
        pane->hpc = NULL;
    }
    if (pane->pipe_in) {
        CloseHandle(pane->pipe_in);
        pane->pipe_in = NULL;
    }
    if (pane->pipe_out) {
        CloseHandle(pane->pipe_out);
        pane->pipe_out = NULL;
    }
    if (pane->read_thread) {
        WaitForSingleObject(pane->read_thread, 2000);
        CloseHandle(pane->read_thread);
        pane->read_thread = NULL;
    }
    if (pane->process) {
        TerminateProcess(pane->process, 0);
        WaitForSingleObject(pane->process, 500);
        CloseHandle(pane->process);
        pane->process = NULL;
    }
    if (pane->thread) {
        CloseHandle(pane->thread);
        pane->thread = NULL;
    }

    EnterCriticalSection(&g_mux.cs);
    screen_free(&pane->screen);
    LeaveCriticalSection(&g_mux.cs);
}

void switch_pane(int idx) {
    if (idx < 0 || idx >= g_mux.pane_count || !g_mux.panes[idx].active) return;
    g_mux.active_pane = idx;
    g_mux.panes[idx].scroll_offset = 0;
    g_mux.needs_redraw = 1;
}

int find_next_active_pane(int cur) {
    for (int i = 1; i <= g_mux.pane_count; i++) {
        int n = (cur + i) % g_mux.pane_count;
        if (g_mux.panes[n].active) return n;
    }
    return -1;
}
