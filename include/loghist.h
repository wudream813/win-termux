#ifndef WIN_TERMUX_LOGHIST_H
#define WIN_TERMUX_LOGHIST_H

#include "common.h"
#include "types.h"

/* ---- 逻辑行历史（logical-line scrollback）+ 本地 reflow 引擎 -----------------
 * 背景：ConPTY/物理环形缓冲按「物理行」存历史，宽度变化时历史行要么被裁
 * （v1.8.45 前会丢内容）要么在窄视口里只显示前几列。现代终端的做法是把滚动
 * 历史存成【逻辑行】——一条逻辑行 = 一次真实换行符（硬换行）界定的完整命令/
 * 输出；自动折行（软换行）折下来的物理行合并回同一条逻辑行。显示时按当前
 * 视口宽度对逻辑行做宽字符感知的 reflow（重新折行）：窄了折成多行、宽了折回
 * 一行，resize 只改显示宽度、内容永不丢失或重复。
 *
 * 实时屏幕（可见区）仍由 ConPTY 按视口尺寸维护；本模块只服务滚动历史。
 * 这是 v1.8.46+ 的地基模块，先用纯函数回归覆盖，后续版本逐步接入渲染 / 复制
 * 模式 / 搜索的历史数据源。 */

typedef struct {
    ScreenLine *lines;   /* 环形缓冲：每条逻辑行（cells 动态生长，len=逻辑列数） */
    int cap;             /* 最多保留逻辑行数（超出丢最老） */
    int count;           /* 现有逻辑行数 */
    int head;            /* 最老一条在 lines[] 的下标（环形头） */
} LogHistory;

void loghist_init(LogHistory *h, int cap_lines);
void loghist_free(LogHistory *h);
void loghist_clear(LogHistory *h);

/* 追加一个滚出可视区的物理行。
 *   cells/fg/bg/valid：该行第 0..col_count-1 列内容（fg/bg/valid 可传 NULL）；
 *   wrapped=1：该行是「上一行因自动折行（软换行）折下来的续行」，追加到当前
 *              逻辑行尾部；
 *   wrapped=0：一次真实换行（硬换行），开一条新逻辑行。
 * 源行尾部的空白填充会被裁掉（物理行用空格补到满宽，不属于逻辑内容）。 */
void loghist_append_row(LogHistory *h, const CHAR_INFO *cells,
                        const WORD *fg, const WORD *bg,
                        const unsigned char *valid,
                        int col_count, int wrapped);

/* 在显示宽度 w（列）下，全部逻辑行 reflow 后一共占多少显示行（>= 逻辑行数，
 * 空历史返回 0）。宽字符不会被拆到两行边界（放不下就整条移到下一显示行）。 */
int loghist_visual_lines(const LogHistory *h, int w);

/* 取第 visual 条显示行（0 = 最老一条），写入宽度 w 的输出缓冲：
 *   out[out_w] 为 CHAR_INFO（字符 + 16 色属性），ofg/obg/ovalid 为真彩（可 NULL，
 *   非 NULL 时容量需 >= out_w）。未用到的列填空格 / 默认属性。
 * 返回 1 成功、0 越界（visual 超出范围 / w 非法）。 */
int loghist_get_visual_line(const LogHistory *h, int w, int visual,
                            CHAR_INFO *out, WORD *ofg, WORD *obg,
                            unsigned char *ovalid, int out_w);

#endif
