#!/usr/bin/env python3
"""Run every lightweight regression check and propagate failures."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
CHECKS = (
    "verify_picker.py",
    "verify_flow.py",
    "verify_mouse53.py",
    "verify_color8.py",
    "verify_emoji.py",
    "verify_v131.py",
)


def main() -> int:
    failed: list[str] = []
    for check in CHECKS:
        print(f"\n===== {check} =====", flush=True)
        result = subprocess.run([sys.executable, str(ROOT / check)], cwd=ROOT)
        if result.returncode:
            failed.append(check)

    if failed:
        print("\n失败: " + ", ".join(failed), file=sys.stderr)
        return 1
    print("\n全部回归验证通过。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
