#include "loghist.h"
#include "utf8.h"

#include <stdlib.h>
#include <string.h>

/* 一个 glyph（显示单元）占多少列：BMP 宽字符占 2，emoji 代理对合起来占 2。
 * 这里按「主格」统计：高代理占 2、低代理（次格）占 0。普通窄字符占 1。 */
static int glyph_width(WCHAR ch) {
    if (ch >= 0xDC00 && ch <= 0xDFFF) return 0;   /* emoji 低代理（次格） */
    if (ch >= 0xD800 && ch <= 0xDBFF) return 2;   /* emoji 高代理（主格，跨2列） */
    if (ch == 0) return 0;                          /* BMP 宽字符的次格占位 */
    return is_wide_cp((unsigned int)ch) ? 2 : 1;
}

void loghist_init(LogHistory *h, int cap_lines) {
    h->cap = cap_lines > 1 ? cap_lines : 1;
    h->count = 0;
    h->head = 0;
    h->lines = (ScreenLine *)calloc((size_t)h->cap, sizeof(ScreenLine));
    if (!h->lines) h->cap = 0;
}

void loghist_clear(LogHistory *h) {
    if (!h->lines) { h->count = 0; h->head = 0; return; }
    for (int i = 0; i < h->cap; i++) {
        free(h->lines[i].cells); free(h->lines[i].fg_rgb);
        free(h->lines[i].bg_rgb); free(h->lines[i].rgb_valid);
        h->lines[i].cells = NULL; h->lines[i].fg_rgb = NULL;
        h->lines[i].bg_rgb = NULL; h->lines[i].rgb_valid = NULL;
        h->lines[i].len = 0;
        h->lines[i].used = 0;
    }
    h->count = 0;
    h->head = 0;
}

void loghist_free(LogHistory *h) {
    loghist_clear(h);
    free(h->lines);
    h->lines = NULL;
    h->cap = 0;
}

/* 环形槽位：age（0=最老）对应 lines 下标。 */
static int slot_of(const LogHistory *h, int age) {
    return (h->head + age) % h->cap;
}

static int cell_is_blank(const CHAR_INFO *c) {
    return c->Char.UnicodeChar == L' ' || c->Char.UnicodeChar == 0;
}

/* 把 count 个单元追加到逻辑行 slot（cells/fg/bg/valid 动态生长）。 */
static void line_grow(ScreenLine *ln, const CHAR_INFO *cells,
                      const WORD *fg, const WORD *bg,
                      const unsigned char *valid, int count) {
    if (count <= 0) return;
    int old = ln->len;
    int need = old + count;
    /* 四个并行数组必须原子扩容。分别 realloc 会出现“cells 成功、fg 失败”的
     * 半成功状态：len 仍增长后，后续颜色写入越过旧 fg 容量。改为先完整分配，
     * 任一失败就全部回滚，旧行保持不变。 */
    CHAR_INFO *nc = (CHAR_INFO *)malloc((size_t)need * sizeof(CHAR_INFO));
    WORD *nfg = (WORD *)malloc((size_t)need * sizeof(WORD));
    WORD *nbg = (WORD *)malloc((size_t)need * sizeof(WORD));
    unsigned char *nv = (unsigned char *)malloc((size_t)need);
    if (!nc || !nfg || !nbg || !nv) {
        free(nc); free(nfg); free(nbg); free(nv);
        return;
    }
    if (old > 0) {
        memcpy(nc, ln->cells, (size_t)old * sizeof(CHAR_INFO));
        memcpy(nfg, ln->fg_rgb, (size_t)old * sizeof(WORD));
        memcpy(nbg, ln->bg_rgb, (size_t)old * sizeof(WORD));
        memcpy(nv, ln->rgb_valid, (size_t)old);
    }
    memcpy(nc + old, cells, (size_t)count * sizeof(CHAR_INFO));
    if (fg) memcpy(nfg + old, fg, (size_t)count * sizeof(WORD));
    else memset(nfg + old, 0xFF, (size_t)count * sizeof(WORD));
    if (bg) memcpy(nbg + old, bg, (size_t)count * sizeof(WORD));
    else memset(nbg + old, 0, (size_t)count * sizeof(WORD));
    if (valid) memcpy(nv + old, valid, (size_t)count);
    else memset(nv + old, 0, (size_t)count);
    free(ln->cells); free(ln->fg_rgb); free(ln->bg_rgb); free(ln->rgb_valid);
    ln->cells = nc; ln->fg_rgb = nfg; ln->bg_rgb = nbg; ln->rgb_valid = nv;
    ln->len = need;
    ln->used = need;
}

/* 取最新（最新一条逻辑行）的槽位；没有则开一条新的。 */
static ScreenLine *latest_line(LogHistory *h) {
    if (h->count < h->cap) {
        int idx = slot_of(h, h->count);
        return &h->lines[idx];
    }
    /* 满：覆盖最老（head 槽位），它将作为「最新」——把 head 前移一位，
     * 旧最老变成新最新。 */
    int oldest = h->head;
    h->head = (h->head + 1) % h->cap;
    ScreenLine *ln = &h->lines[oldest];
    ln->len = 0;   /* 逻辑行重开（cells 缓冲可复用） */
    return ln;
}

void loghist_append_row(LogHistory *h, const CHAR_INFO *cells,
                        const WORD *fg, const WORD *bg,
                        const unsigned char *valid,
                        int col_count, int wrapped) {
    if (!h->lines || h->cap <= 0 || !cells || col_count <= 0) return;

    /* 裁掉物理行尾的空格填充（续行在软换行处也可能恰好满宽，尾部空白不是内容）。 */
    int n = col_count;
    while (n > 0 && cell_is_blank(&cells[n - 1])) n--;
    if (n <= 0) {
        if (!wrapped) {
            /* 空的硬换行行：仍要产生一条空逻辑行（例如连续回车）。 */
            ScreenLine *ln = latest_line(h);
            (void)ln;
            if (h->count < h->cap) h->count++;
        }
        return;
    }

    ScreenLine *ln;
    if (wrapped && h->count > 0) {
        /* 续行：追加到最新逻辑行尾部。 */
        int idx = slot_of(h, h->count - 1);
        ln = &h->lines[idx];
    } else {
        ln = latest_line(h);
        if (h->count < h->cap) h->count++;
    }
    line_grow(ln, cells, fg, bg, valid, n);
}

/* 数一条逻辑行在宽度 w 下占多少显示行（宽字符不跨行）。 */
static int line_visual_rows(const ScreenLine *ln, int w) {
    if (w < 1) w = 1;
    if (!ln || ln->len <= 0) return 1;   /* 空逻辑行占一行 */
    int rows = 1, col = 0;
    for (int k = 0; k < ln->len; k++) {
        WCHAR ch = ln->cells[k].Char.UnicodeChar;
        int gw = glyph_width(ch);
        if (gw <= 0) continue;            /* 次格占位，随主格已计 */
        if (col > 0 && col + gw > w) {    /* 行非空且放不下 -> 折行 */
            rows++;
            col = 0;
        }
        col += gw;                        /* 空行硬放（宽 glyph 视觉夹到宽度内） */
    }
    return rows;
}

int loghist_visual_lines(const LogHistory *h, int w) {
    if (!h || !h->lines || h->count <= 0) return 0;
    if (w < 1) w = 1;
    int total = 0;
    for (int a = 0; a < h->count; a++)
        total += line_visual_rows(&h->lines[slot_of(h, a)], w);
    return total;
}

int loghist_get_visual_line(const LogHistory *h, int w, int visual,
                            CHAR_INFO *out, WORD *ofg, WORD *obg,
                            unsigned char *ovalid, int out_w) {
    if (!h || !h->lines || h->count <= 0 || !out || out_w < 1) return 0;
    if (w < 1) w = 1;
    /* 先把输出清成空格 / 默认属性。 */
    for (int x = 0; x < out_w; x++) {
        out[x].Char.UnicodeChar = L' ';
        out[x].Attributes = 0x07;
        if (ofg) ofg[x] = RGB565_WHITE;
        if (obg) obg[x] = RGB565_BLACK;
        if (ovalid) ovalid[x] = 0;
    }
    int draw_w = w < out_w ? w : out_w;

    /* 定位 visual 落在第 age 条逻辑行的第几显示行（seg）。 */
    int acc = 0, age = 0, seg = 0;
    int found = 0;
    for (age = 0; age < h->count; age++) {
        int vr = line_visual_rows(&h->lines[slot_of(h, age)], w);
        if (visual < acc + vr) { seg = visual - acc; found = 1; break; }
        acc += vr;
    }
    if (!found) return 0;
    const ScreenLine *ln = &h->lines[slot_of(h, age)];

    /* 遍历该逻辑行，跳过前 seg 个显示行，把第 seg 行的单元写到输出。
     * 主格/次格必须一起搬（宽字符/emoji 代理对不拆）。 */
    int col = 0;          /* 当前显示行内列号 */
    int seg_cur = 0;      /* 当前第几显示行 */
    for (int k = 0; k < ln->len; ) {
        WCHAR ch = ln->cells[k].Char.UnicodeChar;
        int gw = glyph_width(ch);
        /* 次格占位（ch==0 / emoji 低代理）：随前一主格一起拷贝，单独跳过。 */
        if (gw <= 0) { k++; continue; }
        if (col > 0 && col + gw > w) {
            /* 当前显示行非空且该 glyph 放不下 -> 折到下一显示行，列号归零；
             * 空行（col==0）则硬放（宽 glyph 视觉上夹到宽度内）。 */
            seg_cur++;
            col = 0;
        }
        if (seg_cur == seg) {
            /* 主格在 k；宽字符/emoji 的次格在 k+1，两者必须落在同一显示行的
             * col 与 col+1，不跨行。 */
            int ncells = (gw == 2 && k + 1 < ln->len) ? 2 : 1;
            for (int g = 0; g < ncells; g++) {
                int dst = col + g;
                int src = k + g;
                if (dst < draw_w && src < ln->len) {
                    out[dst].Char.UnicodeChar = ln->cells[src].Char.UnicodeChar;
                    out[dst].Attributes = ln->cells[src].Attributes;
                    if (ofg && ln->fg_rgb) ofg[dst] = ln->fg_rgb[src];
                    if (obg && ln->bg_rgb) obg[dst] = ln->bg_rgb[src];
                    if (ovalid && ln->rgb_valid) ovalid[dst] = ln->rgb_valid[src];
                }
            }
        }
        col += gw;
        k += (gw == 2) ? 2 : 1;
    }
    return 1;
}
