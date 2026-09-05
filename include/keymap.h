#ifndef WIN_TERMUX_KEYMAP_H
#define WIN_TERMUX_KEYMAP_H

#include "common.h"

/* ---------------------------------------------------------------------------
 * 键位表 (keymap)
 *
 * 所有全局动作都登记在一张动作表里，快捷键只是指向动作的绑定，因此：
 *   1. 前缀键与每个动作的按键都能在 termux.ini 的 [keys] 段里改；
 *   2. 帮助页与命令面板显示的快捷键从同一张表生成，不会再和实际键位脱节。
 * ------------------------------------------------------------------------- */

typedef enum {
    ACT_NONE = 0,
    ACT_SEND_PREFIX,        /* 把前缀键本身发给当前 pane */
    ACT_COMMAND_PALETTE,
    ACT_NEW_PANE,
    ACT_NEW_PANE_MENU,
    ACT_COPY_MODE,
    ACT_SEARCH,
    ACT_SETTINGS,
    ACT_RELOAD_CONFIG,
    ACT_HELP,
    ACT_NEXT_PANE,
    ACT_PREV_PANE,
    ACT_CLOSE_PANE,
    ACT_QUIT,
    ACT_TAB_COLOR_NEXT,
    ACT_TAB_COLOR_PREV,
    ACT_SELECT_PANE,        /* arg = pane 序号 */
    ACT_NEXT_THEME,
    ACT_SPLIT_HORIZONTAL,   /* 前缀 - ：上下分屏 */
    ACT_SPLIT_VERTICAL,     /* 前缀 | ：左右分屏 */
    ACT_SPLIT_NEXT,         /* 前缀 Tab：切换到下一个窗格 */
    ACT_SPLIT_PREV,         /* 前缀 Shift+Tab：上一个窗格 */
    ACT_SPLIT_CLOSE,        /* 前缀 X：关闭当前窗格 */
    ACT_SPLIT_ZOOM,         /* 前缀 Z：当前窗格全屏缩放 / 还原 */
    ACT_SPLIT_UP,           /* 前缀 ↑/k? 方向切换窗格 */
    ACT_SPLIT_DOWN,
    ACT_SPLIT_LEFT,
    ACT_SPLIT_RIGHT,
    ACT_SPLIT_RESIZE_UP,
    ACT_SPLIT_RESIZE_DOWN,
    ACT_SPLIT_RESIZE_LEFT,
    ACT_SPLIT_RESIZE_RIGHT,
    ACT_COUNT
} TermuxAction;

typedef struct {
    WORD vk;                /* 虚拟键码，0 表示按字符匹配 */
    WCHAR ch;               /* 字符匹配（vk 为 0 时使用） */
    unsigned char ctrl, alt, shift;
    unsigned char shift_any;/* 1 = 不关心 Shift 状态 */
} KeySpec;

typedef struct {
    KeySpec key;
    int action;
    int arg;
} KeyBinding;

#define KEYMAP_MAX_USER_BINDINGS 64

/* 恢复出厂键位（前缀 Ctrl+B + 全部默认绑定）。 */
void keymap_init(void);

/* "C-b" / "M-x" / "S-t" / "F5" / "Space" / ":" ... 解析失败返回 0。 */
int keymap_parse_key(const char *text, KeySpec *out);

/* 动作名 <-> 动作 ID（[keys] 段左侧的名字）。 */
int keymap_action_id(const char *name);
const char *keymap_action_name(int action);
const char *keymap_action_label(int action);   /* 中文描述，用于帮助页 */

/* [keys] 段：为动作重新绑定按键；成功返回 1。同一动作重复绑定会累加，
 * 首次绑定会屏蔽该动作的全部默认键位。 */
int keymap_bind(const char *action_name, const char *key_text);

/* 前缀键设置 / 判定。 */
int keymap_set_prefix(const char *key_text);
const char *keymap_prefix_text(void);          /* ini 写法，例如 "C-b" */
/* 人类可读写法，例如 "Ctrl+B"；界面一律用这个，不要直接显示 ini 文本。 */
void keymap_prefix_describe(char *out, int out_size);
int keymap_prefix_is_default(void);
int keymap_is_prefix(WORD vk, DWORD ctrl, WCHAR uc);
/* 前缀键对应的控制字符（Ctrl+B -> 0x02），无则返回 0。 */
char keymap_prefix_char(void);

/* 前缀模式下查表；返回动作 ID，arg 为附带参数（如 pane 序号）。 */
int keymap_lookup(WORD vk, DWORD ctrl, WCHAR uc, int *arg);
/* 免前缀（直接键）查表：只匹配被标记为“不使用前缀”的绑定。 */
int keymap_lookup_direct(WORD vk, DWORD ctrl, WCHAR uc, int *arg);

/* 某个动作是否需要先按前缀键；可在设置页 / [keys] 段里切换。
 * ini 写法：动作名 = 键 noprefix   （例如 next-pane = M-n noprefix） */
int keymap_action_uses_prefix(int action);
int keymap_set_action_prefix(int action, int use_prefix);

/* 生成可读键位文本，例如 "Ctrl+B c"。out 至少 32 字节。 */
void keymap_describe(int action, char *out, int out_size);

/* 动作表遍历（图形化设置页的键位表格用） */
int keymap_action_count(void);
int keymap_action_at(int idx);
int keymap_action_is_overridden(int action);

/* 解除某个动作的用户绑定，恢复默认键位。 */
int keymap_unbind(const char *action_name);

/* 把一次真实按键事件转成可写入 [keys] 的文本（"C-b" / "F2" / "t"）。
 * 纯修饰键等无法绑定的按键返回 0。 */
int keymap_key_text_from_event(WORD vk, DWORD ctrl, WCHAR uc, char *out, int out_size);

/* 用户是否覆盖过键位（保存配置时需要原样写回 [keys] 段）。 */
int keymap_has_user_bindings(void);
int keymap_user_binding_count(void);
const char *keymap_user_binding_action(int idx);
const char *keymap_user_binding_key(int idx);
int keymap_user_binding_no_prefix(int idx);

#endif /* WIN_TERMUX_KEYMAP_H */
