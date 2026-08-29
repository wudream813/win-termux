#include "cliphtml.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows Terminal 默认调色板（Campbell），按 Windows 控制台 attr nibble
 * 顺序排列：0-7 暗色 BGR，8-15 亮色 BGR。16 色复制件贴进浏览器/Word/邮件
 * 时看到的颜色就和终端里一致。 */
static const unsigned char kPalette16[16][3] = {
    { 12,  12,  12},   /*  0 黑      */
    {197,  15,  31},   /*  1 红      */
    { 19, 161,  14},   /*  2 绿      */
    {193, 156,   0},   /*  3 黄(暗)  */
    {  0,  55, 218},   /*  4 蓝      */
    {136,  23, 152},   /*  5 品红    */
    { 58, 150, 221},   /*  6 青      */
    {204, 204, 204},   /*  7 白(暗)  */
    {118, 118, 118},   /*  8 亮黑    */
    {231,  72,  86},   /*  9 亮红    */
    { 22, 198,  12},   /* 10 亮绿    */
    {249, 241, 165},   /* 11 亮黄    */
    { 59, 120, 255},   /* 12 亮蓝    */
    {180,   0, 158},   /* 13 亮品红  */
    { 97, 214, 214},   /* 14 亮青    */
    {242, 242, 242},   /* 15 亮白    */
};

void cliphtml_palette16(int idx, int *r, int *g, int *b) {
    if (idx < 0 || idx > 15) idx = 7;
    if (r) *r = kPalette16[idx][0];
    if (g) *g = kPalette16[idx][1];
    if (b) *b = kPalette16[idx][2];
}

static void buf_reserve(ClipHtmlBuf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap > 0 ? b->cap : 4096;
    while (cap < b->len + extra + 1) {
        if (cap > (size_t)1 << 30) { cap = b->len + extra + 1; break; }
        cap *= 2;
    }
    char *p = (char *)realloc(b->data, cap);
    if (!p) return;
    b->data = p;
    b->cap = cap;
}

static void buf_put(ClipHtmlBuf *b, const char *s, size_t n) {
    if (n == 0) return;
    buf_reserve(b, n);
    if (!b->data || b->len + n + 1 > b->cap) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void buf_puts(ClipHtmlBuf *b, const char *s) { buf_put(b, s, strlen(s)); }

static void buf_printf(ClipHtmlBuf *b, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n < (int)sizeof(tmp)) { buf_put(b, tmp, (size_t)n); return; }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_put(b, big, (size_t)n);
    free(big);
}

void cliphtml_init(ClipHtmlBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

void cliphtml_free(ClipHtmlBuf *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void cliphtml_frag_begin(ClipHtmlBuf *b) {
    /* <pre> 保留空格与行首缩进；等宽字体保证对齐。 */
    buf_puts(b, "<pre style=\"white-space:pre-wrap;font-family:Consolas,Menlo,'Courier New',monospace\">");
}

void cliphtml_frag_break(ClipHtmlBuf *b) { buf_put(b, "\n", 1); }

/* HTML 文本节点转义：& < > 是必须的；引号在文本节点里无害。 */
static void put_escaped(ClipHtmlBuf *b, unsigned int cp) {
    switch (cp) {
        case '&': buf_puts(b, "&amp;"); return;
        case '<': buf_puts(b, "&lt;"); return;
        case '>': buf_puts(b, "&gt;"); return;
        default: break;
    }
    char u8[4];
    int n;
    if (cp < 0x80) { u8[0] = (char)cp; n = 1; }
    else if (cp < 0x800) {
        u8[0] = (char)(0xC0 | (cp >> 6));
        u8[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        u8[0] = (char)(0xE0 | (cp >> 12));
        u8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u8[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        u8[0] = (char)(0xF0 | (cp >> 18));
        u8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        u8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u8[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    buf_put(b, u8, (size_t)n);
}

/* 一个样式 run 的全部属性，用来决定相邻 cell 能否合并。 */
typedef struct {
    int has_fg, has_bg, bold, underline;
    unsigned char r, g, b, br, bg2, bb;
} RunStyle;

static int style_equal(const RunStyle *a, const RunStyle *c) {
    return a->has_fg == c->has_fg && a->has_bg == c->has_bg &&
           a->bold == c->bold && a->underline == c->underline &&
           a->r == c->r && a->g == c->g && a->b == c->b &&
           a->br == c->br && a->bg2 == c->bg2 && a->bb == c->bb;
}

static void style_from_cell(RunStyle *st, const ClipHtmlCell *c) {
    st->has_fg = c->fg_valid;
    st->has_bg = c->bg_valid;
    st->bold = c->bold;
    st->underline = c->underline;
    st->r = c->r; st->g = c->g; st->b = c->b;
    st->br = c->br; st->bg2 = c->bg; st->bb = c->bb;
}

static void emit_style_open(ClipHtmlBuf *b, const RunStyle *st) {
    if (!st->has_fg && !st->has_bg && !st->bold && !st->underline) return;
    buf_puts(b, "<span style=\"");
    if (st->has_fg) buf_printf(b, "color:#%02x%02x%02x;", st->r, st->g, st->b);
    if (st->has_bg) buf_printf(b, "background-color:#%02x%02x%02x;", st->br, st->bg2, st->bb);
    if (st->bold) buf_puts(b, "font-weight:bold;");
    if (st->underline) buf_puts(b, "text-decoration:underline;");
    buf_puts(b, "\">");
}

void cliphtml_frag_row(ClipHtmlBuf *b, const ClipHtmlCell *cells, int x0, int x1) {
    if (x1 < x0) return;
    int span_open = 0;
    RunStyle cur;
    memset(&cur, 0, sizeof(cur));
    for (int x = x0; x <= x1; x++) {
        const ClipHtmlCell *c = &cells[x];

        /* 宽字符（中文/全角/BMP 宽符号）占两列：次格是占位符（ch=0），
         * 复制时整体跳过——不输出空格，也不打断同色 run（占位格与宽字符
         * 同 attr，下一格样式相同会自然并回同一 span）。否则
         * "保留所有权利" 会变成 "保 留 所 有 权 利"（v1.8.18 修复）。 */
        if (c->skip) continue;

        RunStyle st;
        style_from_cell(&st, c);

        unsigned int cp;
        unsigned short ch = c->ch;
        if (ch == 0) ch = ' ';   /* 空 cell = 空格 */

        int consumed = 0;
        if (ch >= 0xD800 && ch <= 0xDBFF && x + 1 <= x1) {
            unsigned short lo = cells[x + 1].ch;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + (((unsigned int)(ch & 0x3FF)) << 10) + (lo & 0x3FF);
                consumed = 1;
            } else {
                cp = (unsigned int)ch;   /* 孤立高代理，原样输出 */
            }
        } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
            cp = (unsigned int)ch;       /* 孤立低代理 */
        } else {
            cp = (unsigned int)ch;
        }

        if (!span_open || !style_equal(&cur, &st)) {
            if (span_open) buf_puts(b, "</span>");
            span_open = 0;
            emit_style_open(b, &st);
            cur = st;
            span_open = (st.has_fg || st.has_bg || st.bold || st.underline);
        }
        put_escaped(b, cp);
        x += consumed;
    }
    if (span_open) buf_puts(b, "</span>");
}

/* HTML Format 头：版本号 + 四个字节偏移量（0000000000 占位，10 位定长）。
 * 占位串替换成真实 10 位数字后长度不变，所以偏移量无需二次修补。 */
static const char kHeaderFmt[] =
    "Version:0.9\r\n"
    "StartHTML:%010lu\r\n"
    "EndHTML:%010lu\r\n"
    "StartFragment:%010lu\r\n"
    "EndFragment:%010lu\r\n";

static const char kDocPrefix[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body>";
static const char kFragMarkOpen[]  = "<!--StartFragment-->";
static const char kFragMarkClose[] = "<!--EndFragment-->";
static const char kDocSuffix[]     = "</body></html>";

int cliphtml_finalize(ClipHtmlBuf *b) {
    if (!b || !b->data) return 0;

    /* 先算头部实际长度（用真数字代入，保证和最终头部逐字节一致）。 */
    size_t hdr_guess = 128;
    char *hdr = (char *)malloc(hdr_guess);
    if (!hdr) return 0;
    unsigned long total_guess =
        (unsigned long)(128 + strlen(kDocPrefix) + strlen(kFragMarkOpen) +
                        b->len + strlen(kFragMarkClose) + strlen(kDocSuffix));
    int hn = snprintf(hdr, hdr_guess, kHeaderFmt, total_guess, total_guess,
                      total_guess, total_guess);
    if (hn < 0) { free(hdr); return 0; }
    if ((size_t)hn >= hdr_guess) {
        hdr_guess = (size_t)hn + 1;
        free(hdr);
        hdr = (char *)malloc(hdr_guess);
        if (!hdr) return 0;
        snprintf(hdr, hdr_guess, kHeaderFmt, total_guess, total_guess,
                 total_guess, total_guess);
    }
    size_t hdr_len = (size_t)hn;

    /* 片段当前是 <pre>...</pre>；EndFragment 标记要落在 </pre> 之后。 */
    static const char kPreClose[] = "</pre>";
    buf_puts(b, kPreClose);

    size_t start_html = hdr_len;
    /* StartFragment/EndFragment 指向各自的注释标记（约定包含标记本身）。
     * 片段里 <pre> 必须在 StartFragment 标记之后，所以把标记插在 <pre> 前。 */
    size_t frag_body_len = b->len;   /* <pre>...</pre> */
    size_t start_frag = hdr_len + strlen(kDocPrefix);
    size_t end_frag = start_frag + strlen(kFragMarkOpen) + frag_body_len;
    size_t end_html = end_frag + strlen(kFragMarkClose) + strlen(kDocSuffix);

    snprintf(hdr, hdr_len + 1, kHeaderFmt,
             (unsigned long)start_html, (unsigned long)end_html,
             (unsigned long)start_frag, (unsigned long)end_frag);

    size_t total = end_html;
    char *out = (char *)malloc(total + 1);
    if (!out) { free(hdr); return 0; }
    size_t pos = 0;
    memcpy(out + pos, hdr, hdr_len); pos += hdr_len;
    memcpy(out + pos, kDocPrefix, strlen(kDocPrefix)); pos += strlen(kDocPrefix);
    /* StartFragment 标记 → <pre>…</pre>（b->data）→ EndFragment 标记。 */
    memcpy(out + pos, kFragMarkOpen, strlen(kFragMarkOpen)); pos += strlen(kFragMarkOpen);
    memcpy(out + pos, b->data, b->len); pos += b->len;
    memcpy(out + pos, kFragMarkClose, strlen(kFragMarkClose)); pos += strlen(kFragMarkClose);
    memcpy(out + pos, kDocSuffix, strlen(kDocSuffix)); pos += strlen(kDocSuffix);
    out[pos] = 0;

    free(hdr);
    free(b->data);
    b->data = out;
    b->len = total;
    b->cap = total + 1;
    return 1;
}
