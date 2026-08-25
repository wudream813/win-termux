import subprocess
import os

test_c = """
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef unsigned short WCHAR;
"""

with open("/home/user/win-termux/termux.cpp", "r", encoding="utf-8") as f:
    src = f.read()

# Extract functions from termux.cpp
start_idx = src.find("static inline unsigned int utf8_decode_cp")
end_idx = src.find("static WCHAR g_high_surrogate = 0;")
extracted = src[start_idx:end_idx]

driver = """
int main() {
    struct {
        const char *input;
        int expected_backspace_steps;
    } tests[] = {
        {"😀", 1},
        {"👍🏽", 1},
        {"🇨🇳", 1},
        {"👨‍💻", 1},
        {"👨‍👩‍👧‍👦", 1},
        {"1️⃣", 1},
        {"⚡️", 1},
        {"❤️", 1},
        {"✍️", 1},
        {"🏴󠁧󠁢󠁥󠁮󠁧󠁿", 1},
        {"hello😀world", 11},
        {"abc👍🏽def", 7},
        {"test1️⃣end", 8},
        {"😀😁😂", 3},
        {NULL, 0}
    };

    for (int i = 0; tests[i].input; i++) {
        char buf[128];
        strcpy(buf, tests[i].input);
        int len = strlen(buf);
        int pos = len;
        int steps = 0;
        while (len > 0) {
            buf_backspace(buf, &len, &pos);
            steps++;
        }
        if (steps != tests[i].expected_backspace_steps) {
            fprintf(stderr, "FAIL backspace on %s: got %d steps, expected %d\\n", tests[i].input, steps, tests[i].expected_backspace_steps);
            return 1;
        }

        // Test forward delete
        strcpy(buf, tests[i].input);
        len = strlen(buf);
        pos = 0;
        steps = 0;
        while (len > 0) {
            buf_delete(buf, &len, &pos);
            steps++;
        }
        if (steps != tests[i].expected_backspace_steps) {
            fprintf(stderr, "FAIL delete on %s: got %d steps, expected %d\\n", tests[i].input, steps, tests[i].expected_backspace_steps);
            return 1;
        }
    }
    printf("All emoji grapheme tests passed successfully!\\n");
    return 0;
}
"""

full_c = test_c + extracted + driver
with open("/tmp/test_emoji_runner.c", "w", encoding="utf-8") as f:
    f.write(full_c)

ret = subprocess.run(["gcc", "-O2", "/tmp/test_emoji_runner.c", "-o", "/tmp/test_emoji_runner"])
assert ret.returncode == 0
ret = subprocess.run(["/tmp/test_emoji_runner"])
assert ret.returncode == 0
print("verify_emoji.py: SUCCESS")
