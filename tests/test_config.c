/* win-termux 配置体系单元测试（主题引擎 + 键位表）
 *
 * 这些模块不依赖任何 Win32 调用，因此可以用 tests/stub 里的最小 windows.h
 * 替身在 Linux 上原生编译、直接执行断言：
 *
 *     gcc -Wall -Wextra -Itests/stub -Iinclude src/theme.c src/keymap.c \
 *         tests/test_config.c -o /tmp/test_config && /tmp/test_config
 */
#include "theme.h"
#include "keymap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failed = 0;

static void check(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failed++;
        printf("  [FAIL] %s\n", what);
    }
}

static void check_str(const char *got, const char *want, const char *what) {
    g_checks++;
    if (strcmp(got, want) != 0) {
        g_failed++;
        printf("  [FAIL] %s: got \"%s\", want \"%s\"\n", what, got, want);
    }
}

/* ------------------------------------------------------------------ 主题 */

static void test_theme_identity(void) {
    printf("theme: 默认主题必须是 identity（一个字节都不改）\n");
    theme_init();
    char buf[] = "\x1b[048;2;033;038;045m│\x1b[038;2;230;237;243m x";
    char copy[sizeof(buf)];
    memcpy(copy, buf, sizeof(buf));
    theme_remap(buf, (int)strlen(buf));
    check(memcmp(buf, copy, sizeof(buf)) == 0, "github-dark 下输出保持不变");
    check_str(theme_name(), "github-dark", "默认主题名");
}

static void test_theme_remap(void) {
    printf("theme: 换主题后 UI 色被替换、pane 内容色不受影响\n");
    theme_init();
    check(theme_set_by_name("nord") == 1, "nord 主题存在");
    check(theme_set_by_name("no-such-theme") == 0, "未知主题名被拒绝");
    theme_apply();

    char buf[128];
    /* 前半段是 UI 面板底色（零填充），后半段是 pane 内容色（%d 输出，无前导零） */
    snprintf(buf, sizeof(buf), "\x1b[048;2;033;038;045mUI\x1b[38;2;33;38;45mPANE");
    int len = (int)strlen(buf);
    theme_remap(buf, len);
    check((int)strlen(buf) == len, "重映射保持长度不变");
    check(strstr(buf, "\x1b[048;2;033;038;045m") == NULL, "UI 底色已被替换");
    check(strstr(buf, "\x1b[38;2;33;38;45m") != NULL, "pane 内容色原样保留");

    int r, g, b;
    theme_role_rgb(TH_BG2, &r, &g, &b);
    char want[32];
    snprintf(want, sizeof(want), "\x1b[048;2;%03d;%03d;%03dm", r, g, b);
    check(strstr(buf, want) != NULL, "UI 底色替换为 nord 的 panel 角色色");

    /* 带 ;1（加粗）后缀的形式也要命中 */
    snprintf(buf, sizeof(buf), "\x1b[038;2;217;119;054;1mX");
    theme_remap(buf, (int)strlen(buf));
    check(strstr(buf, "217;119;054") == NULL, "带 ;1 后缀的序列同样被替换");

    theme_init();
}

static void test_theme_override(void) {
    printf("theme: [theme] 段的单色覆盖\n");
    theme_init();
    check(theme_set_role_hex("accent", "#ff8800") == 1, "接受 #rrggbb");
    check(theme_set_role_hex("accent", "ff8800") == 1, "接受不带 # 的写法");
    check(theme_set_role_hex("accent", "#xyz") == 0, "拒绝非法十六进制");
    check(theme_set_role_hex("not-a-role", "#ffffff") == 0, "拒绝未知角色名");
    theme_apply();

    int r, g, b;
    theme_role_rgb(TH_ACCENT, &r, &g, &b);
    check(r == 0xff && g == 0x88 && b == 0x00, "覆盖值生效");
    check(theme_has_overrides() == 1, "覆盖标记置位");

    char hex[16];
    theme_get_override(TH_ACCENT, hex, sizeof(hex));
    check_str(hex, "#ff8800", "覆盖值可原样写回 ini");

    /* 覆盖了 accent，默认主题也不再是 identity */
    char buf[64];
    snprintf(buf, sizeof(buf), "\x1b[048;2;031;111;235mX");
    theme_remap(buf, (int)strlen(buf));
    check(strstr(buf, "031;111;235") == NULL, "覆盖后 accent 底色被替换");

    theme_init();
    check(theme_has_overrides() == 0, "theme_init 清空覆盖");
}

static void test_theme_roles(void) {
    printf("theme: 角色名 <-> 索引\n");
    check(theme_role_index("panel") == TH_BG2, "panel -> TH_BG2");
    check(theme_role_index("PANEL") == TH_BG2, "角色名大小写不敏感");
    check(theme_role_index("nope") == -1, "未知角色名返回 -1");
    for (int i = 0; i < TH_ROLE_COUNT; i++)
        check(theme_role_index(theme_role_name(i)) == i, "角色名往返一致");
    check(theme_count() >= 5, "至少内置 5 套主题");
    for (int i = 0; i < theme_count(); i++)
        check(theme_set_by_name(theme_name_at(i)) == 1, "内置主题都能按名切换");
    theme_init();
}

/* ------------------------------------------------------------------ 键位 */

#define CTRL_ONLY  (LEFT_CTRL_PRESSED)
#define ALT_ONLY   (LEFT_ALT_PRESSED)

static void test_keymap_defaults(void) {
    printf("keymap: 默认键位复刻 v1.8.3 行为\n");
    keymap_init();
    int arg = 0;

    check(keymap_lookup('C', 0, 'c', &arg) == ACT_NEW_PANE, "c -> new-pane");
    check(keymap_lookup('N', 0, 'n', &arg) == ACT_NEXT_PANE, "n -> next-pane");
    check(keymap_lookup('P', 0, 'p', &arg) == ACT_PREV_PANE, "p -> prev-pane");
    check(keymap_lookup('X', 0, 'x', &arg) == ACT_CLOSE_PANE, "x -> close-pane");
    check(keymap_lookup('D', 0, 'd', &arg) == ACT_QUIT, "d -> quit");
    check(keymap_lookup('S', 0, 's', &arg) == ACT_SETTINGS, "s -> settings");
    check(keymap_lookup('R', 0, 'r', &arg) == ACT_RELOAD_CONFIG, "r -> reload-config");
    check(keymap_lookup('T', 0, 't', &arg) == ACT_TAB_COLOR_NEXT, "t -> tab-color-next");
    check(keymap_lookup('T', SHIFT_PRESSED, 'T', &arg) == ACT_TAB_COLOR_PREV, "Shift+t -> tab-color-prev");
    check(keymap_lookup(0, 0, ':', &arg) == ACT_COMMAND_PALETTE, ": -> command-palette");
    check(keymap_lookup(0, 0, 0xFF1A, &arg) == ACT_COMMAND_PALETTE, "全角冒号同样打开命令面板");
    check(keymap_lookup(0, 0, '/', &arg) == ACT_SEARCH, "/ -> search");
    check(keymap_lookup(0, 0, '?', &arg) == ACT_HELP, "? -> help");
    check(keymap_lookup(0, 0, 'h', &arg) == ACT_HELP, "h -> help");
    check(keymap_lookup(0, 0, '[', &arg) == ACT_COPY_MODE, "[ -> copy-mode");
    check(keymap_lookup(0, 0, '+', &arg) == ACT_NEW_PANE_MENU, "+ -> new-pane-menu");
    check(keymap_lookup(VK_ADD, 0, 0, &arg) == ACT_NEW_PANE_MENU, "小键盘 + -> new-pane-menu");

    check(keymap_lookup('3', 0, '3', &arg) == ACT_SELECT_PANE && arg == 3, "3 -> select-pane 3");
    check(keymap_lookup(VK_NUMPAD7, 0, 0, &arg) == ACT_SELECT_PANE && arg == 7, "小键盘 7 -> select-pane 7");
    check(keymap_lookup(VK_F5, 0, 0, &arg) == ACT_NONE, "未绑定键返回 ACT_NONE");
}

static void test_keymap_prefix(void) {
    printf("keymap: 前缀键可配置\n");
    keymap_init();
    check(keymap_is_prefix('B', CTRL_ONLY, 0x02) == 1, "默认 Ctrl+B 是前缀");
    check(keymap_is_prefix(0, 0, 0x02) == 1, "只送出控制字符时也识别");
    check(keymap_is_prefix('A', CTRL_ONLY, 0x01) == 0, "Ctrl+A 不是默认前缀");
    check(keymap_prefix_char() == 0x02, "前缀控制字符 = 0x02");

    check(keymap_set_prefix("C-a") == 1, "prefix = C-a 解析成功");
    check(keymap_is_prefix('A', CTRL_ONLY, 0x01) == 1, "Ctrl+A 成为前缀");
    check(keymap_is_prefix('B', CTRL_ONLY, 0x02) == 0, "Ctrl+B 不再是前缀");
    check(keymap_prefix_char() == 0x01, "前缀控制字符随之变为 0x01");
    check(keymap_set_prefix("") == 0, "空前缀被拒绝");
    keymap_init();
}

static void test_keymap_parse(void) {
    printf("keymap: 键位字符串解析\n");
    KeySpec k;
    check(keymap_parse_key("C-b", &k) && k.ctrl && !k.alt && k.vk == 'B', "C-b");
    check(keymap_parse_key("M-x", &k) && k.alt && !k.ctrl && k.vk == 'X', "M-x (Alt)");
    check(keymap_parse_key("A-x", &k) && k.alt, "A-x 等价于 M-x");
    check(keymap_parse_key("S-t", &k) && k.shift && !k.shift_any && k.vk == 'T', "S-t");
    check(keymap_parse_key("t", &k) && k.shift_any && k.vk == 'T', "无修饰字母忽略 Shift 状态");
    check(keymap_parse_key("F5", &k) && k.vk == VK_F1 + 4, "F5");
    check(keymap_parse_key("space", &k) && k.vk == VK_SPACE, "space");
    check(keymap_parse_key("PgUp", &k) && k.vk == VK_PRIOR, "命名键大小写不敏感");
    check(keymap_parse_key(":", &k) && k.ch == ':', "标点按字符匹配");
    check(keymap_parse_key("", &k) == 0, "空串失败");
    check(keymap_parse_key("nosuchkey", &k) == 0, "未知键名失败");
}

static void test_keymap_rebind(void) {
    printf("keymap: [keys] 覆盖\n");
    keymap_init();
    int arg = 0;
    check(keymap_bind("new-pane", "k") == 1, "new-pane = k 绑定成功");
    check(keymap_lookup('K', 0, 'k', &arg) == ACT_NEW_PANE, "新键位生效");
    check(keymap_lookup('C', 0, 'c', &arg) == ACT_NONE, "该动作的默认键位被顶掉");
    check(keymap_lookup('X', 0, 'x', &arg) == ACT_CLOSE_PANE, "其它动作的默认键位不受影响");
    check(keymap_bind("no-such-action", "k") == 0, "未知动作名被拒绝");
    check(keymap_bind("new-pane", "nosuchkey") == 0, "非法键位被拒绝");

    check(keymap_user_binding_count() == 1, "用户绑定计数");
    check_str(keymap_user_binding_action(0), "new-pane", "回写动作名");
    check_str(keymap_user_binding_key(0), "k", "回写键位文本");

    keymap_init();
    check(keymap_has_user_bindings() == 0, "keymap_init 清空用户绑定");
}

static void test_keymap_describe(void) {
    printf("keymap: 帮助页显示的键位跟随配置\n");
    keymap_init();
    char buf[48];
    keymap_describe(ACT_NEW_PANE, buf, sizeof(buf));
    check_str(buf, "Ctrl+B c", "默认 new-pane 描述");
    keymap_describe(ACT_COMMAND_PALETTE, buf, sizeof(buf));
    check_str(buf, "Ctrl+B :", "默认 command-palette 描述");
    keymap_describe(ACT_TAB_COLOR_PREV, buf, sizeof(buf));
    check_str(buf, "Ctrl+B Shift+T", "Shift 组合的描述");

    keymap_set_prefix("C-a");
    keymap_bind("new-pane", "F2");
    keymap_describe(ACT_NEW_PANE, buf, sizeof(buf));
    check_str(buf, "Ctrl+A F2", "改了 prefix 与键位后描述同步");
    keymap_init();
}

static void test_keymap_actions(void) {
    printf("keymap: 动作名表完整\n");
    for (int a = ACT_SEND_PREFIX; a < ACT_COUNT; a++) {
        const char *name = keymap_action_name(a);
        check(name[0] != 0, "每个动作都有名字");
        check(keymap_action_id(name) == a, "动作名往返一致");
        check(keymap_action_label(a)[0] != 0, "每个动作都有中文描述");
    }
    check(keymap_action_id("bogus") == ACT_NONE, "未知动作名返回 ACT_NONE");
}

int main(void) {
    test_theme_identity();
    test_theme_remap();
    test_theme_override();
    test_theme_roles();
    test_keymap_defaults();
    test_keymap_prefix();
    test_keymap_parse();
    test_keymap_rebind();
    test_keymap_describe();
    test_keymap_actions();

    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
