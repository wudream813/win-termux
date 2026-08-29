#ifndef WIN_TERMUX_FRAMEDIFF_H
#define WIN_TERMUX_FRAMEDIFF_H

/* ---------------------------------------------------------------------------
 * 脏区渲染：整帧 VT 流 -> 只发变化的行。
 *
 * render_screen() 仍然每帧生成完整画面（绝对光标定位 \x1b[r;cH，所有定位
 * 都显式给出）。这里把整帧流按 CUP 定位切成「每一行一段字节」，与上一帧
 * 影子逐行 memcmp：没变的行完全不发。不属于任何行的部分（如标题 OSC）
 * 始终原样发出。纯标准 C，可在 Linux 侧 ASAN 回归。
 * ------------------------------------------------------------------------- */

#include <stddef.h>

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} FrameChunk;

typedef struct {
    FrameChunk always;   /* 行外内容（标题 OSC 等），每帧都发 */
    FrameChunk *rows;    /* 按终端行 0..rows-1 的字节段 */
    int row_count;
    int rows_cap;

    FrameChunk *shadow;  /* 上一帧影子 */
    int shadow_count;
    int shadow_cap;
    char *dirty;         /* 本帧每行是否脏（规划阶段标记） */
    int force_all;       /* 尺寸变化等场景强制整帧重发 */
} FrameDiff;

void framediff_init(FrameDiff *fd);
void framediff_free(FrameDiff *fd);

/* 下一帧开始：清空 always 与行段（影子保留用于比对）。 */
void framediff_begin_frame(FrameDiff *fd, int host_rows);

/* 解析一帧 VT 流：CUP (\x1b[r;cH) 切行，OSC (\x1b]..\x07/\x1b\\) 归 always，
 * 其余字节归入当前行。 */
void framediff_scan(FrameDiff *fd, const char *buf, size_t len);

/* 与影子比对。out == NULL：只规划（标记脏行、更新影子、返回增量字节数）；
 * out != NULL：按已标记的脏行把增量写入缓冲区（不再比对）。生产路径先
 * 规划再分配再写出，保证只比对一次。 */
size_t framediff_emit(FrameDiff *fd, char *out, size_t out_cap);

/* 失效影子：下一帧整行重发（窗口尺寸变化、强制重绘）。 */
void framediff_invalidate(FrameDiff *fd);

/* 供测试：扫描 + 直接产出字符串到固定缓冲区。 */
size_t framediff_diff_test(FrameDiff *fd, const char *buf, size_t len,
                           char *out, size_t out_cap);

#endif /* WIN_TERMUX_FRAMEDIFF_H */
