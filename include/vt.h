#ifndef WIN_TERMUX_VT_H
#define WIN_TERMUX_VT_H

#include "common.h"
#include "types.h"
#include "utf8.h"
#include "screen.h"

void screen_process_output(ScreenBuffer *s, const char *data, int len);
void process_sgr(ScreenBuffer *s, const int *params, int pc);
void execute_osc(ScreenBuffer *s);
void execute_esc(ScreenBuffer *s, char final, const char *inter, int inter_len);
void sanitize_title(const char *raw, int raw_len, char *out, int out_size);

#endif // WIN_TERMUX_VT_H
