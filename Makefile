CC = x86_64-w64-mingw32-gcc
CXX = x86_64-w64-mingw32-g++
CFLAGS = -O2 -s -Wall -Wextra -Iinclude
CXXFLAGS = -O2 -s -Wall -Wextra -Iinclude
LDFLAGS = -luser32 -lshell32

SRC = src/config.c src/cliphtml.c src/framediff.c src/input.c src/keymap.c src/main.c src/pane.c src/render.c src/screen.c src/split.c src/theme.c src/utf8.c src/vt.c
TARGET = termux.exe
TARGET_CPP = termux_cpp.exe

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

cpp: $(TARGET_CPP)

$(TARGET_CPP): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET_CPP) $(LDFLAGS)

test:
	python3 verify_all.py

# 主题 / 键位模块不依赖 Win32 API，可用 tests/stub 的 windows.h 替身在本机跑
unittest:
	gcc -O1 -Wall -Wextra -Werror -Itests/stub -Iinclude src/theme.c src/keymap.c tests/test_config.c -o /tmp/termux_test_config -lm
	/tmp/termux_test_config

clean:
	rm -f $(TARGET) $(TARGET_CPP) *.o

.PHONY: all cpp test unittest clean
