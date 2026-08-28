#include "utf8.h"

unsigned int utf8_decode_cp(const char *s, int max_len, int *adv) {
    if (max_len <= 0) { *adv = 0; return 0; }
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) { *adv = 1; return c; }
    if ((c & 0xE0) == 0xC0 && max_len >= 2) {
        *adv = 2;
        return ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && max_len >= 3) {
        *adv = 3;
        return ((c & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && max_len >= 4) {
        *adv = 4;
        return ((c & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
    }
    *adv = 1;
    return c;
}

int is_zero_width_cp(unsigned int cp) {
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 1;       // Variation Selectors
    if (cp == 0x200D) return 1;                       // Zero-Width Joiner (ZWJ)
    if (cp >= 0x0300 && cp <= 0x036F) return 1;       // Combining Diacritical Marks
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return 1;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return 1;
    if (cp >= 0x20D0 && cp <= 0x20FF) return 1;       // Combining Diacritical Marks for Symbols
    if (cp >= 0xFE20 && cp <= 0xFE2F) return 1;
    if (cp >= 0xE0020 && cp <= 0xE007F) return 1;     // Tag characters
    if (cp >= 0xE0100 && cp <= 0xE01EF) return 1;     // Variation Selectors Supplement
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return 1;     // Emoji skin tone modifiers
    return 0;
}

int is_wide_cp(unsigned int cp) {
    if (cp < 0x1100) return 0;
    if (cp <= 0x115F) return 1;                         // Hangul Jamo
    if (cp == 0x231A || cp == 0x231B) return 1;         // ⌚, ⌛
    if (cp >= 0x23E9 && cp <= 0x23EC) return 1;         // ⏩..⏬
    if (cp == 0x23F0 || cp == 0x23F3) return 1;         // ⏰, ⏳
    if (cp >= 0x25FD && cp <= 0x25FE) return 1;         // ◽, ◾
    if (cp >= 0x2614 && cp <= 0x2615) return 1;         // ☔, ☕
    if (cp >= 0x2648 && cp <= 0x2653) return 1;         // ♈..♓
    if (cp == 0x267F || cp == 0x2693 || cp == 0x26A1) return 1; // ♿, ⚓, ⚡
    if (cp >= 0x26AA && cp <= 0x26AB) return 1;         // ⚪, ⚫
    if (cp >= 0x26BD && cp <= 0x26BE) return 1;         // ⚽, ⚾
    if (cp >= 0x26C4 && cp <= 0x26C5) return 1;         // ⛄, ⛅
    if (cp == 0x26CE || cp == 0x26D4 || cp == 0x26EA) return 1; // ⛎, ⛔, ⛪
    if (cp >= 0x26F2 && cp <= 0x26F3) return 1;         // ⛲, ⛳
    if (cp == 0x26F5 || cp == 0x26FA || cp == 0x26FD) return 1; // ⛵, ⛺, ⛽
    if (cp == 0x2705) return 1;                         // ✅
    if (cp >= 0x270A && cp <= 0x270B) return 1;         // ✊, ✋
    if (cp == 0x2728) return 1;                         // ✨
    if (cp == 0x274C || cp == 0x274E) return 1;         // ❌, ❎
    if (cp >= 0x2753 && cp <= 0x2755) return 1;         // ❓..❕
    if (cp == 0x2757) return 1;                         // ❗
    if (cp >= 0x2795 && cp <= 0x2797) return 1;         // ➕..➗
    if (cp == 0x27B0 || cp == 0x27BF) return 1;         // ➰, ➿
    if (cp >= 0x2B1B && cp <= 0x2B1C) return 1;         // ⬛, ⬜
    if (cp == 0x2B50 || cp == 0x2B55) return 1;         // ⭐, ⭕
    if (cp >= 0x2E80 && cp <= 0xA4C6) return 1;         // CJK radicals, Hiragana, Katakana, CJK ideographs
    if (cp >= 0xA960 && cp <= 0xA97C) return 1;         // Hangul Jamo Extended-A
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 1;         // Hangul Syllables
    if (cp >= 0xF900 && cp <= 0xFAFF) return 1;         // CJK Compatibility Ideographs
    if (cp >= 0xFE10 && cp <= 0xFE19) return 1;         // Vertical forms
    if (cp >= 0xFE30 && cp <= 0xFE6B) return 1;         // CJK Compatibility Forms
    if (cp >= 0xFF01 && cp <= 0xFF60) return 1;         // Fullwidth forms
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 1;         // Fullwidth symbols
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return 1;       // SMP Emoji
    return 0;
}

int is_ri(unsigned int cp) {
    return (cp >= 0x1F1E6 && cp <= 0x1F1FF);
}

int is_combining_cp(unsigned int cp) {
    return is_zero_width_cp(cp);
}

int is_emoji_modifier(unsigned int cp) {
    return (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

int utf8_cols(const char *s, int len) {
    int cols = 0, i = 0;
    while (i < len) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(s + i, len - i, &adv);
        if (!is_zero_width_cp(cp)) {
            cols += is_wide_cp(cp) ? 2 : 1;
        }
        i += adv;
    }
    return cols;
}

void append_padded_utf8(char *out, int bs, int *posp, int *colsp, const char *s, int target_cols) {
    int pos = *posp;
    int len = (int)strlen(s);
    int cols = 0;
    int i = 0;
    while (i < len) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(s + i, len - i, &adv);
        if (is_zero_width_cp(cp)) {
            for (int k = 0; k < adv && pos < bs - 1; k++) out[pos++] = s[i + k];
            i += adv;
            continue;
        }
        int w = is_wide_cp(cp) ? 2 : 1;
        if (cols + w > target_cols) break;
        for (int k = 0; k < adv && pos < bs - 1; k++) out[pos++] = s[i + k];
        cols += w;
        i += adv;
    }
    while (cols < target_cols && pos < bs - 1) {
        out[pos++] = ' ';
        cols++;
    }
    *posp = pos;
    if (colsp) *colsp += cols;
}

void pad_to_right_border(char *out, int bs, int *posp, int *colsp, int target_w) {
    int pos = *posp;
    int cols = *colsp;
    while (cols < target_w - 1 && pos < bs - 8) {
        out[pos++] = ' ';
        cols++;
    }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m\x1b[048;2;033;038;045m│\x1b[0m");
    cols++;
    *posp = pos;
    *colsp = cols;
}

int utf8_next_grapheme(const char *buf, int len, int pos) {
    if (pos >= len) return len;
    int p = pos;
    int adv = 0;
    unsigned int cp = utf8_decode_cp(buf + p, len - p, &adv);
    p += adv;

    if (cp == 0x0D && p < len && (unsigned char)buf[p] == 0x0A) {
        return p + 1;
    }

    if (is_ri(cp)) {
        if (p < len) {
            int nadv = 0;
            unsigned int next_cp = utf8_decode_cp(buf + p, len - p, &nadv);
            if (is_ri(next_cp)) {
                p += nadv;
                return p;
            }
        }
    }

    unsigned int prev_cp = cp;
    while (p < len) {
        int next_adv = 0;
        unsigned int next_cp = utf8_decode_cp(buf + p, len - p, &next_adv);
        if (prev_cp == 0x200D) {
            p += next_adv;
            prev_cp = next_cp;
            continue;
        }
        if (is_zero_width_cp(next_cp)) {
            p += next_adv;
            prev_cp = next_cp;
            continue;
        }
        break;
    }
    return p;
}

int utf8_prev_grapheme(const char *buf, int pos) {
    if (pos <= 0) return 0;
    int cur = 0;
    while (cur < pos) {
        int next = utf8_next_grapheme(buf, pos, cur);
        if (next >= pos) return cur;
        cur = next;
    }
    return 0;
}

void buf_insert_utf8(char *buf, int *len, int *pos, int max_cap, const char *utf8_bytes, int byte_count) {
    if (*len + byte_count >= max_cap) return;
    if (*pos < *len) {
        memmove(buf + *pos + byte_count, buf + *pos, *len - *pos);
    }
    memcpy(buf + *pos, utf8_bytes, byte_count);
    *len += byte_count;
    *pos += byte_count;
    buf[*len] = 0;
}

void buf_backspace(char *buf, int *len, int *pos) {
    if (*pos <= 0 || *len <= 0) return;
    int prev_p = utf8_prev_grapheme(buf, *pos);
    int del_count = *pos - prev_p;
    if (del_count > 0) {
        if (*pos < *len) {
            memmove(buf + prev_p, buf + *pos, *len - *pos);
        }
        *len -= del_count;
        *pos = prev_p;
        buf[*len] = 0;
    }
}

void buf_delete(char *buf, int *len, int *pos) {
    if (*pos >= *len || *len <= 0) return;
    int next_p = utf8_next_grapheme(buf, *len, *pos);
    int del_count = next_p - *pos;
    if (del_count > 0) {
        if (next_p < *len) {
            memmove(buf + *pos, buf + next_p, *len - next_p);
        }
        *len -= del_count;
        buf[*len] = 0;
    }
}

static int wchars_to_utf8(const WCHAR *wbuf, int wlen, char *u8buf, int max_u8) {
    int u8pos = 0;
    for (int i = 0; i < wlen && u8pos < max_u8 - 4; i++) {
        WCHAR uc = wbuf[i];
        if (uc >= 0xD800 && uc <= 0xDBFF && i + 1 < wlen && wbuf[i+1] >= 0xDC00 && wbuf[i+1] <= 0xDFFF) {
            unsigned int cp = 0x10000 + (((unsigned int)(uc & 0x3FF)) << 10) + (wbuf[i+1] & 0x3FF);
            u8buf[u8pos++] = (char)(0xF0 | (cp >> 18));
            u8buf[u8pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            u8buf[u8pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            u8buf[u8pos++] = (char)(0x80 | (cp & 0x3F));
            i++;
        } else if (uc < 0x80) {
            u8buf[u8pos++] = (char)uc;
        } else if (uc < 0x800) {
            u8buf[u8pos++] = (char)(0xC0 | (uc >> 6));
            u8buf[u8pos++] = (char)(0x80 | (uc & 0x3F));
        } else {
            u8buf[u8pos++] = (char)(0xE0 | (uc >> 12));
            u8buf[u8pos++] = (char)(0x80 | ((uc >> 6) & 0x3F));
            u8buf[u8pos++] = (char)(0x80 | (uc & 0x3F));
        }
    }
    u8buf[u8pos] = 0;
    return u8pos;
}

static int wchar_idx_to_u8_idx(const WCHAR *wbuf, int wlen, int target_widx) {
    if (target_widx <= 0) return 0;
    int u8pos = 0;
    for (int i = 0; i < wlen && i < target_widx; i++) {
        WCHAR uc = wbuf[i];
        if (uc >= 0xD800 && uc <= 0xDBFF && i + 1 < wlen && wbuf[i+1] >= 0xDC00 && wbuf[i+1] <= 0xDFFF) {
            u8pos += 4;
            i++;
        } else if (uc < 0x80) {
            u8pos += 1;
        } else if (uc < 0x800) {
            u8pos += 2;
        } else {
            u8pos += 3;
        }
    }
    return u8pos;
}

static int u8_idx_to_wchar_idx(const WCHAR *wbuf, int wlen, int target_u8idx) {
    if (target_u8idx <= 0) return 0;
    int u8pos = 0;
    for (int i = 0; i < wlen; i++) {
        if (u8pos >= target_u8idx) return i;
        WCHAR uc = wbuf[i];
        if (uc >= 0xD800 && uc <= 0xDBFF && i + 1 < wlen && wbuf[i+1] >= 0xDC00 && wbuf[i+1] <= 0xDFFF) {
            u8pos += 4;
            i++;
        } else if (uc < 0x80) {
            u8pos += 1;
        } else if (uc < 0x800) {
            u8pos += 2;
        } else {
            u8pos += 3;
        }
    }
    return wlen;
}

int get_prev_grapheme_wchars(const WCHAR *wbuf, int wlen, int pos) {
    if (pos <= 0) return 1;
    char u8[1024];
    wchars_to_utf8(wbuf, wlen, u8, (int)sizeof(u8));
    int cur_u8 = wchar_idx_to_u8_idx(wbuf, wlen, pos);
    int prev_u8 = utf8_prev_grapheme(u8, cur_u8);
    int prev_widx = u8_idx_to_wchar_idx(wbuf, wlen, prev_u8);
    int diff = pos - prev_widx;
    return diff > 0 ? diff : 1;
}

int get_next_grapheme_wchars(const WCHAR *wbuf, int wlen, int pos) {
    if (pos >= wlen) return 1;
    char u8[1024];
    int u8len = wchars_to_utf8(wbuf, wlen, u8, (int)sizeof(u8));
    int cur_u8 = wchar_idx_to_u8_idx(wbuf, wlen, pos);
    int next_u8 = utf8_next_grapheme(u8, u8len, cur_u8);
    int next_widx = u8_idx_to_wchar_idx(wbuf, wlen, next_u8);
    int diff = next_widx - pos;
    return diff > 0 ? diff : 1;
}

void format_tab_title(char *dst, int dst_max, const char *src) {
    if (!src || !*src) {
        snprintf(dst, dst_max, "cmd");
        return;
    }
    int src_len = (int)strlen(src);
    int total_cols = utf8_cols(src, src_len);
    if (total_cols <= 15) {
        snprintf(dst, dst_max, "%s", src);
        return;
    }

    int cur_cols = 0;
    int i = 0;
    int out_pos = 0;
    while (i < src_len && out_pos < dst_max - 4) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(src + i, src_len - i, &adv);
        int w = is_zero_width_cp(cp) ? 0 : (is_wide_cp(cp) ? 2 : 1);
        if (cur_cols + w > 12) break;
        for (int k = 0; k < adv && out_pos < dst_max - 4; k++) {
            dst[out_pos++] = src[i + k];
        }
        cur_cols += w;
        i += adv;
    }
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos] = 0;
}

void format_cmd_display(char *dst, int dst_max, const char *src) {
    if (!src || !*src) {
        dst[0] = 0;
        return;
    }
    int src_len = (int)strlen(src);
    int total_cols = utf8_cols(src, src_len);
    if (total_cols <= 15) {
        snprintf(dst, dst_max, "%s", src);
        return;
    }

    int cur_cols = 0;
    int i = 0;
    int out_pos = 0;
    while (i < src_len && out_pos < dst_max - 4) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(src + i, src_len - i, &adv);
        int w = is_zero_width_cp(cp) ? 0 : (is_wide_cp(cp) ? 2 : 1);
        if (cur_cols + w > 12) break;
        for (int k = 0; k < adv && out_pos < dst_max - 4; k++) {
            dst[out_pos++] = src[i + k];
        }
        cur_cols += w;
        i += adv;
    }
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos] = 0;
}

void format_name_display(char *dst, int dst_max, const char *src) {
    if (!src || !*src) {
        dst[0] = 0;
        return;
    }
    int src_len = (int)strlen(src);
    int total_cols = utf8_cols(src, src_len);
    if (total_cols <= 10) {
        snprintf(dst, dst_max, "%s", src);
        return;
    }

    int cur_cols = 0;
    int i = 0;
    int out_pos = 0;
    while (i < src_len && out_pos < dst_max - 4) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(src + i, src_len - i, &adv);
        int w = is_zero_width_cp(cp) ? 0 : (is_wide_cp(cp) ? 2 : 1);
        if (cur_cols + w > 7) break;
        for (int k = 0; k < adv && out_pos < dst_max - 4; k++) {
            dst[out_pos++] = src[i + k];
        }
        cur_cols += w;
        i += adv;
    }
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos] = 0;
}

void format_name15_display(char *dst, int dst_max, const char *src) {
    if (!src || !*src) {
        dst[0] = 0;
        return;
    }
    int src_len = (int)strlen(src);
    int total_cols = utf8_cols(src, src_len);
    if (total_cols <= 15) {
        snprintf(dst, dst_max, "%s", src);
        return;
    }

    int cur_cols = 0;
    int i = 0;
    int out_pos = 0;
    while (i < src_len && out_pos < dst_max - 4) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(src + i, src_len - i, &adv);
        int w = is_zero_width_cp(cp) ? 0 : (is_wide_cp(cp) ? 2 : 1);
        if (cur_cols + w > 12) break;
        for (int k = 0; k < adv && out_pos < dst_max - 4; k++) {
            dst[out_pos++] = src[i + k];
        }
        cur_cols += w;
        i += adv;
    }
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos++] = '.';
    dst[out_pos] = 0;
}

int get_input_screen_offset(const char *buf, int len, int cursor_byte_pos, int vis_width) {
    if (vis_width < 1) vis_width = 1;
    int cursor_col = utf8_cols(buf, cursor_byte_pos);
    int total_cols = utf8_cols(buf, len);
    /* An exactly full field needs no scroll arrows.  Keep the insertion
     * caret inside the field as well: the terminal cursor occupies a cell,
     * so the end-of-text position is painted over the final visible cell
     * instead of leaking onto the separator/right border. */
    if (total_cols <= vis_width) {
        return cursor_col < vis_width ? cursor_col : vis_width - 1;
    }
    int scroll_col = 0;
    if (cursor_col >= vis_width - 2) {
        scroll_col = cursor_col - (vis_width - 3);
    }
    if (scroll_col > total_cols - vis_width + 1) {
        scroll_col = total_cols - vis_width + 1;
    }
    if (scroll_col < 0) scroll_col = 0;
    int has_left = (scroll_col > 0);
    int has_right = (scroll_col + vis_width - (has_left ? 1 : 0) < total_cols);
    int min_cx = has_left ? 1 : 0;
    int max_cx = vis_width - 1 - (has_right ? 1 : 0);
    if (max_cx < min_cx) max_cx = min_cx;
    int cx = cursor_col - scroll_col + (has_left ? 1 : 0);
    if (cx < min_cx) cx = min_cx;
    if (cx > max_cx) cx = max_cx;
    return cx;
}

void render_scrollable_input(char *out, int bs, int *posp,
                             const char *buf, int len, int cursor_byte_pos,
                             int vis_width, const char *bg_sgr, int *cursor_screen_offset) {
    int pos = *posp;
    if (vis_width < 1) vis_width = 1;
    int cursor_col = utf8_cols(buf, cursor_byte_pos);
    int total_cols = utf8_cols(buf, len);
    const char *bg = (bg_sgr && bg_sgr[0]) ? bg_sgr : "";

    if (total_cols <= vis_width) {
        pos += snprintf(out + pos, bs - pos, "%s\x1b[038;2;230;237;243m", bg);
        for (int p = 0; p < len && pos < bs - 8; p++) out[pos++] = buf[p];
        int used = total_cols;
        while (used < vis_width && pos < bs - 8) { out[pos++] = ' '; used++; }
        pos += snprintf(out + pos, bs - pos, "\x1b[0m");
        if (cursor_screen_offset) {
            *cursor_screen_offset = cursor_col < vis_width ? cursor_col : vis_width - 1;
        }
        *posp = pos;
        return;
    }

    int scroll_col = 0;
    if (cursor_col >= vis_width - 2) {
        scroll_col = cursor_col - (vis_width - 3);
    }
    if (scroll_col > total_cols - vis_width + 1) {
        scroll_col = total_cols - vis_width + 1;
    }
    if (scroll_col < 0) scroll_col = 0;

    int has_left = (scroll_col > 0);
    int has_right = (scroll_col + vis_width - (has_left ? 1 : 0) < total_cols);

    int text_slots = vis_width;
    if (has_left) {
        pos += snprintf(out + pos, bs - pos, "%s\x1b[038;2;210;153;034;1m<\x1b[22m", bg);
        text_slots--;
    }
    if (has_right) {
        text_slots--;
    }

    int cur_c = 0;
    int p = 0;
    int start_byte = 0;
    int end_byte = len;
    int found_start = 0;

    while (p < len) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(buf + p, len - p, &adv);
        int w = is_zero_width_cp(cp) ? 0 : (is_wide_cp(cp) ? 2 : 1);
        if (!found_start && cur_c >= scroll_col) {
            start_byte = p;
            found_start = 1;
        }
        if (found_start && cur_c + w > scroll_col + text_slots) {
            end_byte = p;
            break;
        }
        cur_c += w;
        p += adv;
    }

    int rendered_cols = 0;
    p = start_byte;
    pos += snprintf(out + pos, bs - pos, "%s\x1b[038;2;230;237;243m", bg);
    while (p < end_byte && pos < bs - 16) {
        int adv = 0;
        unsigned int cp = utf8_decode_cp(buf + p, end_byte - p, &adv);
        int w = is_zero_width_cp(cp) ? 0 : (is_wide_cp(cp) ? 2 : 1);
        for (int k = 0; k < adv && pos < bs - 8; k++) out[pos++] = buf[p + k];
        rendered_cols += w;
        p += adv;
    }

    while (rendered_cols < text_slots && pos < bs - 8) {
        out[pos++] = ' ';
        rendered_cols++;
    }

    if (has_right) {
        pos += snprintf(out + pos, bs - pos, "%s\x1b[038;2;210;153;034;1m>\x1b[22m", bg);
    }
    pos += snprintf(out + pos, bs - pos, "\x1b[0m");

    int min_cx = has_left ? 1 : 0;
    int max_cx = vis_width - 1 - (has_right ? 1 : 0);
    if (max_cx < min_cx) max_cx = min_cx;
    int cx_offset = cursor_col - scroll_col + (has_left ? 1 : 0);
    if (cx_offset < min_cx) cx_offset = min_cx;
    if (cx_offset > max_cx) cx_offset = max_cx;
    if (cursor_screen_offset) *cursor_screen_offset = cx_offset;

    *posp = pos;
}
