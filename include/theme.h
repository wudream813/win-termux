#ifndef WIN_TERMUX_THEME_H
#define WIN_TERMUX_THEME_H

#include "common.h"

/* ---------------------------------------------------------------------------
 * 主题引擎 (theme engine)
 *
 * 渲染层的所有 UI 配色都以 "零填充三位" 的真彩序列写在字符串字面量里，例如
 *   "\x1b[48;2;033;038;045m"
 * 而 pane 内容的颜色是用 %d 动态拼出来的（不带前导零）。这个差异让我们可以在
 * 输出前对整帧做一次「只命中 UI 配色」的等长就地替换 —— 既不用把 render.c 里
 * 300 多处字面量改成 %s，也绝不会误伤终端程序自己输出的颜色。
 *
 * 参考色板 (github-dark) 中的每个颜色都登记在 g_theme_refs 里，并映射为
 * 「语义角色 + 与底色的混合比例」，因此换主题只需要提供 16 个角色色即可。
 * 默认主题走 identity 快路径，一个字节都不会被改写。
 * ------------------------------------------------------------------------- */

enum {
    TH_BG0 = 0,      /* 最深背景 / 亮底上的文字色 */
    TH_BG1,          /* 标签栏背景 */
    TH_BG2,          /* 面板、弹窗背景 */
    TH_FG,           /* 主文字 */
    TH_FG_DIM,       /* 次要文字 */
    TH_WHITE,        /* 高亮文字 */
    TH_ACCENT,       /* 主强调色（活动标签、边框） */
    TH_CYAN,         /* 信息色 / 浅蓝 */
    TH_GREEN,
    TH_GREEN_DARK,
    TH_RED,
    TH_ORANGE,
    TH_YELLOW,
    TH_PURPLE,
    TH_PINK,
    TH_SELECTION,    /* 选区底色 */
    TH_ROLE_COUNT
};

typedef struct {
    unsigned char r, g, b;
} ThemeRGB;

typedef struct {
    const char *name;
    ThemeRGB role[TH_ROLE_COUNT];
} ThemeDef;

extern const ThemeDef g_builtin_themes[];
extern const int g_builtin_theme_count;

/* 初始化为默认主题（github-dark），清空所有 [theme] 覆盖项。 */
void theme_init(void);

/* 按名称切换内置主题；未知名称返回 0 并保持原主题。 */
int theme_set_by_name(const char *name);

/* [theme] 段的单项覆盖，如 theme_set_role_hex("accent", "#58a6ff")。 */
int theme_set_role_hex(const char *role_name, const char *hex);

/* 角色名 <-> 索引，供配置读写与验证脚本使用。 */
int theme_role_index(const char *role_name);
const char *theme_role_name(int role);

/* 重新计算替换表。修改主题或覆盖项后必须调用（load_config 内部已调用）。 */
void theme_apply(void);

/* 对渲染输出做等长就地重映射；默认主题下为空操作。 */
void theme_remap(char *buf, int len);

/* 取当前主题下某角色的实际颜色。 */
void theme_role_rgb(int role, int *r, int *g, int *b);
WORD theme_role_rgb565(int role);

const char *theme_name(void);
int theme_index(void);
int theme_count(void);
const char *theme_name_at(int idx);
/* 存在 [theme] 覆盖项时为 1（保存配置时需要原样写回）。 */
int theme_has_overrides(void);
void theme_get_override(int role, char *out_hex, int out_size);

#endif /* WIN_TERMUX_THEME_H */
