#include "config.h"

ChooserItem g_chooser_items[MAX_CHOOSER_ITEMS];
int g_chooser_item_count = 0;

int g_settings_nav = 0;
int g_settings_field = 0;
int g_settings_table_sel = 0;
int g_default_startup = 0;
int g_settings_show_presets = 0;
int g_preset_sel = 0;

char g_edit_name[32] = {0};
int g_edit_name_len = 0, g_edit_name_pos = 0;
char g_edit_cmd[256] = {0};
int g_edit_cmd_len = 0, g_edit_cmd_pos = 0;
char g_edit_dir[256] = {0};
int g_edit_dir_len = 0, g_edit_dir_pos = 0;

const ChooserItem g_presets[] = {
    {"cmd", "cmd.exe", ""},
    {"PowerShell", "powershell.exe", ""},
    {"Pwsh", "pwsh.exe", ""},
    {"WSL", "wsl.exe", ""},
    {"Git Bash", "bash.exe", ""},
    {"Python", "python -i", ""},
    {"Node.js", "node", ""},
    {"自定义命令行", ":custom", ""},
};
const int g_preset_count = (int)(sizeof(g_presets) / sizeof(g_presets[0]));

void init_default_config(void) {
    g_default_startup = 0;
    g_chooser_item_count = 3;
    snprintf(g_chooser_items[0].name, sizeof(g_chooser_items[0].name), "cmd");
    snprintf(g_chooser_items[0].cmd, sizeof(g_chooser_items[0].cmd), "cmd.exe");
    g_chooser_items[0].workdir[0] = 0;

    snprintf(g_chooser_items[1].name, sizeof(g_chooser_items[1].name), "PowerShell");
    snprintf(g_chooser_items[1].cmd, sizeof(g_chooser_items[1].cmd), "powershell.exe");
    g_chooser_items[1].workdir[0] = 0;

    snprintf(g_chooser_items[2].name, sizeof(g_chooser_items[2].name), "自定义命令行");
    snprintf(g_chooser_items[2].cmd, sizeof(g_chooser_items[2].cmd), ":custom");
    g_chooser_items[2].workdir[0] = 0;
}

void load_config(void) {
    init_default_config();

    WCHAR exe_path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    WCHAR *last_bs = wcsrchr(exe_path, L'\\');
    WCHAR ini_path[MAX_PATH] = {0};
    if (last_bs) {
        *last_bs = 0;
        _snwprintf(ini_path, MAX_PATH - 1, L"%s\\termux.ini", exe_path);
    } else {
        wcscpy(ini_path, L"termux.ini");
    }

    FILE *f = _wfopen(ini_path, L"rb");
    if (!f) {
        const WCHAR *prof = _wgetenv(L"USERPROFILE");
        if (prof) {
            WCHAR user_ini[MAX_PATH] = {0};
            _snwprintf(user_ini, MAX_PATH - 1, L"%s\\.termux.ini", prof);
            f = _wfopen(user_ini, L"rb");
        }
    }

    if (!f) {
        save_config();
        return;
    }

    char line[512];
    int parsed_count = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == ';' || *p == '\r' || *p == '\n' || *p == '[') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p;
        while (*key == ' ' || *key == '\t') key++;
        int klen = (int)strlen(key);
        while (klen > 0 && ((unsigned char)key[klen - 1] <= ' ')) key[--klen] = 0;

        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        if (_stricmp(key, "default_startup") == 0) {
            g_default_startup = atoi(val);
            continue;
        }

        char *comma1 = strchr(val, ',');
        if (!comma1) continue;
        *comma1 = 0;
        char *name = val;
        char *cmd = comma1 + 1;
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        char default_workdir[1] = {0};
        char *workdir = default_workdir;
        char *comma2 = strchr(cmd, ',');
        if (comma2) {
            *comma2 = 0;
            workdir = comma2 + 1;
            while (*workdir == ' ' || *workdir == '\t') workdir++;
        }

        int nlen = (int)strlen(name);
        while (nlen > 0 && ((unsigned char)name[nlen - 1] <= ' ' || name[nlen - 1] == '\r' || name[nlen - 1] == '\n')) name[--nlen] = 0;

        int clen = (int)strlen(cmd);
        while (clen > 0 && ((unsigned char)cmd[clen - 1] <= ' ' || cmd[clen - 1] == '\r' || cmd[clen - 1] == '\n')) cmd[--clen] = 0;

        int wlen = (int)strlen(workdir);
        while (wlen > 0 && ((unsigned char)workdir[wlen - 1] <= ' ' || workdir[wlen - 1] == '\r' || workdir[wlen - 1] == '\n')) workdir[--wlen] = 0;

        if (nlen > 0 && clen > 0 && parsed_count < MAX_CHOOSER_ITEMS) {
            strncpy(g_chooser_items[parsed_count].name, name, sizeof(g_chooser_items[0].name) - 1);
            g_chooser_items[parsed_count].name[sizeof(g_chooser_items[0].name) - 1] = 0;

            strncpy(g_chooser_items[parsed_count].cmd, cmd, sizeof(g_chooser_items[0].cmd) - 1);
            g_chooser_items[parsed_count].cmd[sizeof(g_chooser_items[0].cmd) - 1] = 0;

            strncpy(g_chooser_items[parsed_count].workdir, workdir, sizeof(g_chooser_items[0].workdir) - 1);
            g_chooser_items[parsed_count].workdir[sizeof(g_chooser_items[0].workdir) - 1] = 0;

            parsed_count++;
        }
    }
    fclose(f);

    if (parsed_count > 0) {
        g_chooser_item_count = parsed_count;
    }
}

void save_config(void) {
    WCHAR exe_path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    WCHAR *last_bs = wcsrchr(exe_path, L'\\');
    WCHAR ini_path[MAX_PATH] = {0};
    if (last_bs) {
        *last_bs = 0;
        _snwprintf(ini_path, MAX_PATH - 1, L"%s\\termux.ini", exe_path);
    } else {
        wcscpy(ini_path, L"termux.ini");
    }

    FILE *f = _wfopen(ini_path, L"wb");
    if (!f) {
        const WCHAR *prof = _wgetenv(L"USERPROFILE");
        if (prof) {
            WCHAR user_ini[MAX_PATH] = {0};
            _snwprintf(user_ini, MAX_PATH - 1, L"%s\\.termux.ini", prof);
            f = _wfopen(user_ini, L"wb");
        }
    }
    if (!f) return;

    const char *header =
        "# win-termux 配置文件 (UTF-8)\r\n"
        "# 格式: 序号 = 菜单显示名称, 启动命令行, 启动目录(可选)\r\n"
        "# 特殊命令 \":custom\" 表示打开自定义命令行输入框\r\n"
        "\r\n"
        "[settings]\r\n";
    fwrite(header, 1, strlen(header), f);

    char sline[128];
    int slen = snprintf(sline, sizeof(sline), "default_startup = %d\r\n\r\n[menu]\r\n", g_default_startup);
    if (slen > 0) fwrite(sline, 1, slen, f);

    for (int i = 0; i < g_chooser_item_count; i++) {
        char line[512];
        int len;
        if (g_chooser_items[i].workdir[0]) {
            len = snprintf(line, sizeof(line), "%d = %s, %s, %s\r\n", i + 1, g_chooser_items[i].name, g_chooser_items[i].cmd, g_chooser_items[i].workdir);
        } else {
            len = snprintf(line, sizeof(line), "%d = %s, %s\r\n", i + 1, g_chooser_items[i].name, g_chooser_items[i].cmd);
        }
        if (len > 0) fwrite(line, 1, len, f);
    }
    fclose(f);
}

void load_item_to_editor(int idx) {
    if (idx < 0 || idx >= g_chooser_item_count) return;
    snprintf(g_edit_name, sizeof(g_edit_name), "%s", g_chooser_items[idx].name);
    g_edit_name_len = (int)strlen(g_edit_name);
    g_edit_name_pos = g_edit_name_len;

    snprintf(g_edit_cmd, sizeof(g_edit_cmd), "%s", g_chooser_items[idx].cmd);
    g_edit_cmd_len = (int)strlen(g_edit_cmd);
    g_edit_cmd_pos = g_edit_cmd_len;

    snprintf(g_edit_dir, sizeof(g_edit_dir), "%s", g_chooser_items[idx].workdir);
    g_edit_dir_len = (int)strlen(g_edit_dir);
    g_edit_dir_pos = g_edit_dir_len;

    g_settings_field = 0;
}

void save_editor_to_item(int idx) {
    if (idx < 0 || idx >= g_chooser_item_count) return;
    if (g_edit_name_len > 0) {
        snprintf(g_chooser_items[idx].name, sizeof(g_chooser_items[0].name), "%s", g_edit_name);
    }
    if (g_edit_cmd_len > 0) {
        snprintf(g_chooser_items[idx].cmd, sizeof(g_chooser_items[0].cmd), "%s", g_edit_cmd);
    }
    snprintf(g_chooser_items[idx].workdir, sizeof(g_chooser_items[0].workdir), "%s", g_edit_dir);
    save_config();
}
