#ifndef WIN_TERMUX_UTF8_H
#define WIN_TERMUX_UTF8_H

#include "common.h"

unsigned int utf8_decode_cp(const char *s, int max_len, int *adv);
int is_wide_cp(unsigned int cp);
int is_zero_width_cp(unsigned int cp);
int is_combining_cp(unsigned int cp);
int is_ri(unsigned int cp);
int is_emoji_modifier(unsigned int cp);
int utf8_cols(const char *s, int len);

int utf8_prev_grapheme(const char *buf, int pos);
int utf8_next_grapheme(const char *buf, int len, int pos);

int get_prev_grapheme_wchars(const WCHAR *buf, int len, int pos);
int get_next_grapheme_wchars(const WCHAR *buf, int len, int pos);

void buf_backspace(char *buf, int *len, int *pos);
void buf_delete(char *buf, int *len, int *pos);
void buf_insert_utf8(char *buf, int *len, int *pos, int max_len, const char *u8, int u8_count);

void format_tab_title(char *dst, int dst_max, const char *src);
void format_name_display(char *dst, int dst_max, const char *src);
void format_cmd_display(char *dst, int dst_max, const char *src);
void format_name15_display(char *dst, int dst_max, const char *src);

int get_input_screen_offset(const char *buf, int len, int cursor_byte_pos, int vis_width);
void render_scrollable_input(char *out, int bs, int *posp,
                             const char *buf, int len, int cursor_byte_pos,
                             int vis_width, const char *bg_sgr, int *cursor_screen_offset);

void append_padded_utf8(char *out, int bs, int *posp, int *colsp, const char *s, int target_cols);
void pad_to_right_border(char *out, int bs, int *posp, int *colsp, int target_w);

#endif // WIN_TERMUX_UTF8_H
