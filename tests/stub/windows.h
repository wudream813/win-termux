/* 仅供 Linux 侧单元测试使用的 windows.h 最小替身。
 * 目标是让不依赖 Win32 API 的纯逻辑模块（theme.c / keymap.c）能在
 * CI 的 ubuntu runner 上原生编译并直接跑断言，而不是靠 Python 复刻一遍逻辑。 */
#ifndef WIN_TERMUX_TEST_STUB_WINDOWS_H
#define WIN_TERMUX_TEST_STUB_WINDOWS_H

#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>

typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned long long DWORD64;
typedef unsigned char BYTE;
typedef int BOOL;
typedef wchar_t WCHAR;
typedef void *HANDLE;
typedef void *HPCON;
typedef unsigned int UINT;
typedef long LONG;

#define MAX_PATH 260
#define TRUE 1
#define FALSE 0

#define _stricmp  strcasecmp
#define _strnicmp strncasecmp

/* Win32 调用约定在原生 Linux 编译下置空（部分头里有 __stdcall 函数声明）。 */
#ifndef __stdcall
#define __stdcall
#endif
#ifndef WINAPI
#define WINAPI
#endif

/* 控制台单元（仅 loghist/screen 等纯逻辑模块需要；真实定义见 Win32 wincon.h）。 */
typedef struct _CHAR_INFO {
    union {
        WCHAR UnicodeChar;
        char  AsciiChar;
    } Char;
    WORD Attributes;
} CHAR_INFO;

typedef struct _COORD {
    short X;
    short Y;
} COORD;

typedef struct _SMALL_RECT {
    short Left, Top, Right, Bottom;
} SMALL_RECT;

/* CRITICAL_SECTION 替身：纯逻辑测试不真正用临界区（Enter/Leave 为空宏）。 */
typedef struct { int dummy; } CRITICAL_SECTION;
#define InitializeCriticalSection(p) (void)(p)
#define DeleteCriticalSection(p)     (void)(p)
#define EnterCriticalSection(p)      (void)(p)
#define LeaveCriticalSection(p)      (void)(p)

/* 鼠标事件记录替身（只列访问到的字段）。dwMousePosition 用 COORD。 */
typedef struct _MOUSE_EVENT_RECORD {
    COORD dwMousePosition;
    DWORD dwButtonState;
    DWORD dwControlKeyState;
    DWORD dwEventFlags;
} MOUSE_EVENT_RECORD;

#define FROM_LEFT_1ST_BUTTON_PRESSED  0x0001
#define FROM_LEFT_2ND_BUTTON_PRESSED  0x0004
#define RIGHTMOST_BUTTON_PRESSED      0x0002
#define DOUBLE_CLICK                  0x0002
#define MOUSE_WHEELED                 0x0004
#define MOUSE_HWHEELED                0x0008

#define COMMON_LVB_UNDERSCORE  0x8000
#define COMMON_LVB_REVERSE_VIDEO 0x4000
#define FOREGROUND_INTENSITY   0x0008
#define BACKGROUND_INTENSITY   0x0080
#define FOREGROUND_RED 0x0004
#define FOREGROUND_GREEN 0x0002
#define FOREGROUND_BLUE 0x0001
#define BACKGROUND_RED 0x0040
#define BACKGROUND_GREEN 0x0020
#define BACKGROUND_BLUE 0x0010

/* 控制键状态位 */
#define RIGHT_ALT_PRESSED   0x0001
#define LEFT_ALT_PRESSED    0x0002
#define RIGHT_CTRL_PRESSED  0x0004
#define LEFT_CTRL_PRESSED   0x0008
#define SHIFT_PRESSED       0x0010

/* 虚拟键码（只列出 keymap 用到的） */
#define VK_BACK    0x08
#define VK_TAB     0x09
#define VK_RETURN  0x0D
#define VK_SHIFT   0x10
#define VK_CONTROL 0x11
#define VK_MENU    0x12
#define VK_ESCAPE  0x1B
#define VK_CAPITAL 0x14
#define VK_SPACE   0x20
#define VK_NUMLOCK 0x90
#define VK_SCROLL  0x91
#define VK_PRIOR   0x21
#define VK_NEXT    0x22
#define VK_END     0x23
#define VK_HOME    0x24
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28
#define VK_INSERT  0x2D
#define VK_DELETE  0x2E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_ADD     0x6B
#define VK_F1      0x70
#define VK_F2      0x71
#define VK_F5      0x74
#define VK_F24     0x87
#define VK_OEM_1   0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2   0xBF
#define VK_OEM_3   0xC0
#define VK_OEM_4   0xDB
#define VK_OEM_5   0xDC
#define VK_OEM_6   0xDD
#define VK_OEM_7   0xDE

#endif /* WIN_TERMUX_TEST_STUB_WINDOWS_H */
