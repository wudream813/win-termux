#include "keymap.h"
#include <ctype.h>

/* ---------------------------------------------------------------------------
 * 动作表
 * ------------------------------------------------------------------------- */
typedef struct {
    int action;
    const char *name;
    const char *label;
} ActionInfo;

static const ActionInfo g_actions[] = {
    {ACT_SEND_PREFIX,     "send-prefix",     "发送前缀键本身"},
    {ACT_COMMAND_PALETTE, "command-palette", "打开命令面板"},
    {ACT_NEW_PANE,        "new-pane",        "新建默认 pane"},
    {ACT_NEW_PANE_MENU,   "new-pane-menu",   "新建 pane 菜单"},
    {ACT_COPY_MODE,       "copy-mode",       "进入复制模式"},
    {ACT_SEARCH,          "search",          "搜索滚动历史"},
    {ACT_SETTINGS,        "settings",        "打开图形化设置"},
    {ACT_RELOAD_CONFIG,   "reload-config",   "热重载配置文件"},
    {ACT_HELP,            "help",            "打开 / 关闭帮助"},
    {ACT_NEXT_PANE,       "next-pane",       "下一个 pane"},
    {ACT_PREV_PANE,       "prev-pane",       "上一个 pane"},
    {ACT_CLOSE_PANE,      "close-pane",      "关闭当前 pane"},
    {ACT_QUIT,            "quit",            "退出 termux"},
    {ACT_TAB_COLOR_NEXT,  "tab-color-next",  "下一个标签颜色"},
    {ACT_TAB_COLOR_PREV,  "tab-color-prev",  "上一个标签颜色"},
    {ACT_SELECT_PANE,     "select-pane",     "按编号跳转 pane"},
    {ACT_NEXT_THEME,      "next-theme",      "切换下一个主题"},
};
static const int g_action_count = (int)(sizeof(g_actions) / sizeof(g_actions[0]));

/* ---------------------------------------------------------------------------
 * 默认键位（完整复刻 v1.8.3 的行为，含各种键盘布局兜底）
 * ------------------------------------------------------------------------- */
#define VKEY(v, sh)     {(WORD)(v), 0, 0, 0, (unsigned char)(sh), 0}
#define VKEY_ANY(v)     {(WORD)(v), 0, 0, 0, 0, 1}
#define CHR(c)          {0, (WCHAR)(c), 0, 0, 0, 1}
#define VKEY_SHIFT(v)   {(WORD)(v), 0, 0, 0, 1, 0}

static const KeyBinding g_default_bindings[] = {
    /* 新建 pane 菜单：'+' / 小键盘 + / Shift+= */
    {CHR('+'),                     ACT_NEW_PANE_MENU,   0},
    {VKEY_ANY(VK_ADD),             ACT_NEW_PANE_MENU,   0},
    {VKEY_SHIFT(VK_OEM_PLUS),      ACT_NEW_PANE_MENU,   0},
    /* 命令面板：':'（含中文全角冒号）/ Shift+; */
    {CHR(':'),                     ACT_COMMAND_PALETTE, 0},
    {CHR(0xFF1A),                  ACT_COMMAND_PALETTE, 0},
    {VKEY_SHIFT(VK_OEM_1),         ACT_COMMAND_PALETTE, 0},
    /* 历史搜索：'/' */
    {CHR('/'),                     ACT_SEARCH,          0},
    {VKEY(VK_OEM_2, 0),            ACT_SEARCH,          0},
    /* 帮助：'?' / 'h' */
    {CHR('?'),                     ACT_HELP,            0},
    {CHR('h'),                     ACT_HELP,            0},
    {CHR('H'),                     ACT_HELP,            0},
    {VKEY_SHIFT(VK_OEM_2),         ACT_HELP,            0},
    /* 复制模式：'[' */
    {CHR('['),                     ACT_COPY_MODE,       0},
    {VKEY_ANY(VK_OEM_4),           ACT_COPY_MODE,       0},
    /* 其余单字母动作 */
    {VKEY_ANY('R'),                ACT_RELOAD_CONFIG,   0},
    {VKEY_ANY('C'),                ACT_NEW_PANE,        0},
    {VKEY_ANY('N'),                ACT_NEXT_PANE,       0},
    {VKEY_ANY('P'),                ACT_PREV_PANE,       0},
    {VKEY_ANY('X'),                ACT_CLOSE_PANE,      0},
    {VKEY_ANY('D'),                ACT_QUIT,            0},
    {VKEY_ANY('S'),                ACT_SETTINGS,        0},
    {VKEY_SHIFT('T'),              ACT_TAB_COLOR_PREV,  0},
    {VKEY('T', 0),                 ACT_TAB_COLOR_NEXT,  0},
    /* pane 跳转：主键盘与小键盘数字 */
    {VKEY_ANY('0'),                ACT_SELECT_PANE,     0},
    {VKEY_ANY('1'),                ACT_SELECT_PANE,     1},
    {VKEY_ANY('2'),                ACT_SELECT_PANE,     2},
    {VKEY_ANY('3'),                ACT_SELECT_PANE,     3},
    {VKEY_ANY('4'),                ACT_SELECT_PANE,     4},
    {VKEY_ANY('5'),                ACT_SELECT_PANE,     5},
    {VKEY_ANY('6'),                ACT_SELECT_PANE,     6},
    {VKEY_ANY('7'),                ACT_SELECT_PANE,     7},
    {VKEY_ANY('8'),                ACT_SELECT_PANE,     8},
    {VKEY_ANY('9'),                ACT_SELECT_PANE,     9},
    {VKEY_ANY(VK_NUMPAD0),         ACT_SELECT_PANE,     0},
    {VKEY_ANY(VK_NUMPAD1),         ACT_SELECT_PANE,     1},
    {VKEY_ANY(VK_NUMPAD2),         ACT_SELECT_PANE,     2},
    {VKEY_ANY(VK_NUMPAD3),         ACT_SELECT_PANE,     3},
    {VKEY_ANY(VK_NUMPAD4),         ACT_SELECT_PANE,     4},
    {VKEY_ANY(VK_NUMPAD5),         ACT_SELECT_PANE,     5},
    {VKEY_ANY(VK_NUMPAD6),         ACT_SELECT_PANE,     6},
    {VKEY_ANY(VK_NUMPAD7),         ACT_SELECT_PANE,     7},
    {VKEY_ANY(VK_NUMPAD8),         ACT_SELECT_PANE,     8},
    {VKEY_ANY(VK_NUMPAD9),         ACT_SELECT_PANE,     9},
};
static const int g_default_count = (int)(sizeof(g_default_bindings) / sizeof(g_default_bindings[0]));

/* ---------------------------------------------------------------------------
 * 运行时状态
 * ------------------------------------------------------------------------- */
typedef struct {
    KeyBinding bind;
    char key_text[24];
} UserBinding;

static UserBinding g_user[KEYMAP_MAX_USER_BINDINGS];
static int g_user_count = 0;
static unsigned char g_action_overridden[ACT_COUNT];
static KeySpec g_prefix;
static char g_prefix_text[24];

/* 命名键 */
typedef struct { const char *name; WORD vk; } NamedKey;
static const NamedKey g_named_keys[] = {
    {"space", VK_SPACE}, {"tab", VK_TAB}, {"enter", VK_RETURN}, {"return", VK_RETURN},
    {"esc", VK_ESCAPE}, {"escape", VK_ESCAPE}, {"backspace", VK_BACK}, {"bs", VK_BACK},
    {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT},
    {"home", VK_HOME}, {"end", VK_END}, {"pgup", VK_PRIOR}, {"pageup", VK_PRIOR},
    {"pgdn", VK_NEXT}, {"pagedown", VK_NEXT}, {"ins", VK_INSERT}, {"insert", VK_INSERT},
    {"del", VK_DELETE}, {"delete", VK_DELETE},
};
static const int g_named_key_count = (int)(sizeof(g_named_keys) / sizeof(g_named_keys[0]));

void keymap_init(void) {
    g_user_count = 0;
    memset(g_action_overridden, 0, sizeof(g_action_overridden));
    keymap_set_prefix("C-b");
}

int keymap_action_id(const char *name) {
    if (!name) return ACT_NONE;
    for (int i = 0; i < g_action_count; i++)
        if (_stricmp(name, g_actions[i].name) == 0) return g_actions[i].action;
    return ACT_NONE;
}

const char *keymap_action_name(int action) {
    for (int i = 0; i < g_action_count; i++)
        if (g_actions[i].action == action) return g_actions[i].name;
    return "";
}

const char *keymap_action_label(int action) {
    for (int i = 0; i < g_action_count; i++)
        if (g_actions[i].action == action) return g_actions[i].label;
    return "";
}

int keymap_parse_key(const char *text, KeySpec *out) {
    if (!text || !out) return 0;
    KeySpec spec;
    memset(&spec, 0, sizeof(spec));

    char buf[32];
    int n = 0;
    while (*text == ' ' || *text == '\t') text++;
    while (text[n] && text[n] != ' ' && text[n] != '\t' && text[n] != '\r' && text[n] != '\n' &&
           n < (int)sizeof(buf) - 1)
        { buf[n] = text[n]; n++; }
    buf[n] = 0;
    if (n == 0) return 0;

    char *p = buf;
    int explicit_shift = 0;
    /* 前缀修饰符：C- (Ctrl)、M-/A- (Alt)、S- (Shift)。单独的 "-" 表示减号键。 */
    while (p[0] && p[1] == '-' && p[2]) {
        char m = (char)toupper((unsigned char)p[0]);
        if (m == 'C') spec.ctrl = 1;
        else if (m == 'M' || m == 'A') spec.alt = 1;
        else if (m == 'S') { spec.shift = 1; explicit_shift = 1; }
        else break;
        p += 2;
    }
    if (!*p) return 0;

    int plen = (int)strlen(p);
    if (plen == 1) {
        unsigned char c = (unsigned char)p[0];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            spec.vk = (WORD)toupper(c);
            if (!explicit_shift) spec.shift_any = 1;
        } else if (c >= '0' && c <= '9') {
            spec.vk = (WORD)c;
            if (!explicit_shift) spec.shift_any = 1;
        } else {
            spec.ch = (WCHAR)c;
            spec.shift_any = 1;
        }
        *out = spec;
        return 1;
    }

    if ((p[0] == 'f' || p[0] == 'F') && plen <= 3) {
        int num = atoi(p + 1);
        if (num >= 1 && num <= 24) {
            spec.vk = (WORD)(VK_F1 + num - 1);
            if (!explicit_shift) spec.shift_any = 1;
            *out = spec;
            return 1;
        }
    }

    for (int i = 0; i < g_named_key_count; i++) {
        if (_stricmp(p, g_named_keys[i].name) == 0) {
            spec.vk = g_named_keys[i].vk;
            if (!explicit_shift) spec.shift_any = 1;
            *out = spec;
            return 1;
        }
    }
    return 0;
}

int keymap_bind(const char *action_name, const char *key_text) {
    int action = keymap_action_id(action_name);
    if (action == ACT_NONE || action >= ACT_COUNT) return 0;
    if (g_user_count >= KEYMAP_MAX_USER_BINDINGS) return 0;

    /* select-pane 需要 "select-pane 3 = C-b 3" 形式的参数，这里用动作名后缀支持：
     * select-pane 的绑定沿用默认（0-9），[keys] 中不单独重绑。 */
    KeySpec spec;
    if (!keymap_parse_key(key_text, &spec)) return 0;

    UserBinding *ub = &g_user[g_user_count++];
    ub->bind.key = spec;
    ub->bind.action = action;
    ub->bind.arg = 0;
    snprintf(ub->key_text, sizeof(ub->key_text), "%s", key_text);
    g_action_overridden[action] = 1;
    return 1;
}

int keymap_set_prefix(const char *key_text) {
    KeySpec spec;
    if (!keymap_parse_key(key_text, &spec)) return 0;
    g_prefix = spec;
    snprintf(g_prefix_text, sizeof(g_prefix_text), "%s", key_text);
    return 1;
}

const char *keymap_prefix_text(void) { return g_prefix_text; }

char keymap_prefix_char(void) {
    if (g_prefix.ctrl && g_prefix.vk >= 'A' && g_prefix.vk <= 'Z')
        return (char)(g_prefix.vk - 'A' + 1);
    return 0;
}

static int spec_match(const KeySpec *s, WORD vk, DWORD ctrl, WCHAR uc) {
    int is_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    int is_alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    int is_shift = (ctrl & SHIFT_PRESSED) != 0;
    if (s->ctrl != (unsigned char)is_ctrl) return 0;
    if (s->alt != (unsigned char)is_alt) return 0;
    if (!s->shift_any && s->shift != (unsigned char)is_shift) return 0;
    if (s->vk) return s->vk == vk;
    if (s->ch) return uc != 0 && s->ch == uc;
    return 0;
}

int keymap_is_prefix(WORD vk, DWORD ctrl, WCHAR uc) {
    if (spec_match(&g_prefix, vk, ctrl, uc)) return 1;
    /* Ctrl+字母 在部分输入法/布局下只会送出控制字符 */
    char pc = keymap_prefix_char();
    if (pc && uc == (WCHAR)(unsigned char)pc) return 1;
    return 0;
}

int keymap_lookup(WORD vk, DWORD ctrl, WCHAR uc, int *arg) {
    if (arg) *arg = 0;
    for (int i = 0; i < g_user_count; i++) {
        if (spec_match(&g_user[i].bind.key, vk, ctrl, uc)) {
            if (arg) *arg = g_user[i].bind.arg;
            return g_user[i].bind.action;
        }
    }
    for (int i = 0; i < g_default_count; i++) {
        const KeyBinding *b = &g_default_bindings[i];
        if (g_action_overridden[b->action]) continue;
        if (spec_match(&b->key, vk, ctrl, uc)) {
            if (arg) *arg = b->arg;
            return b->action;
        }
    }
    return ACT_NONE;
}

static void spec_text(const KeySpec *s, char *out, int out_size) {
    char keyname[24];
    if (s->ch) {
        if (s->ch < 128) snprintf(keyname, sizeof(keyname), "%c", (char)s->ch);
        else snprintf(keyname, sizeof(keyname), "%s", "键");
    } else if (s->vk >= VK_F1 && s->vk <= VK_F24) {
        snprintf(keyname, sizeof(keyname), "F%d", s->vk - VK_F1 + 1);
    } else if ((s->vk >= 'A' && s->vk <= 'Z') || (s->vk >= '0' && s->vk <= '9')) {
        char c = (char)s->vk;
        /* Ctrl/Alt 组合按惯例大写显示（Ctrl+B），单键则小写（c） */
        if (c >= 'A' && c <= 'Z' && !s->shift && !s->ctrl && !s->alt) c = (char)(c - 'A' + 'a');
        snprintf(keyname, sizeof(keyname), "%c", c);
    } else {
        const char *nm = "?";
        for (int i = 0; i < g_named_key_count; i++)
            if (g_named_keys[i].vk == s->vk) { nm = g_named_keys[i].name; break; }
        snprintf(keyname, sizeof(keyname), "%s", nm);
    }
    snprintf(out, out_size, "%s%s%s%s",
             s->ctrl ? "Ctrl+" : "", s->alt ? "Alt+" : "",
             (s->shift && !s->shift_any) ? "Shift+" : "", keyname);
}

void keymap_describe(int action, char *out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = 0;
    const KeySpec *found = NULL;
    for (int i = 0; i < g_user_count && !found; i++)
        if (g_user[i].bind.action == action) found = &g_user[i].bind.key;
    for (int i = 0; i < g_default_count && !found; i++)
        if (g_default_bindings[i].action == action && !g_action_overridden[action])
            found = &g_default_bindings[i].key;
    if (!found) return;

    char prefix[24], key[24];
    spec_text(&g_prefix, prefix, sizeof(prefix));
    spec_text(found, key, sizeof(key));
    if (action == ACT_SEND_PREFIX) snprintf(out, out_size, "%s %s", prefix, prefix);
    else snprintf(out, out_size, "%s %s", prefix, key);
}

int keymap_has_user_bindings(void) { return g_user_count > 0; }
int keymap_user_binding_count(void) { return g_user_count; }

const char *keymap_user_binding_action(int idx) {
    if (idx < 0 || idx >= g_user_count) return "";
    return keymap_action_name(g_user[idx].bind.action);
}

const char *keymap_user_binding_key(int idx) {
    if (idx < 0 || idx >= g_user_count) return "";
    return g_user[idx].key_text;
}
