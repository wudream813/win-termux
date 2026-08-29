#ifndef WIN_TERMUX_COMMON_H
#define WIN_TERMUX_COMMON_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006   // Win10 1809 (RS5) - ConPTY requirement
#endif
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <process.h>
#include <wctype.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#endif

#ifndef TERMUX_VERSION
#define TERMUX_VERSION "1.8.23"
#endif

#define MAX_PANES         16
#define SCROLL_BUF_LINES  10000
#define READ_BUF_SIZE     32768
#define MAX_CHOOSER_ITEMS 9
#define MAX_SEARCH_MATCHES 2048
#define PALETTE_STACK_MAX 8

#define RGB565_WHITE 0xFFFF
#define RGB565_BLACK 0x0000

static inline WORD rgb565(int r, int g, int b) {
    return (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline void rgb565_split(WORD v, int *r, int *g, int *b) {
    *r = (v >> 11) & 0x1F; *r = (*r << 3) | (*r >> 2);
    *g = (v >> 5) & 0x3F;  *g = (*g << 2) | (*g >> 4);
    *b = v & 0x1F;         *b = (*b << 3) | (*b >> 2);
}

#endif // WIN_TERMUX_COMMON_H
