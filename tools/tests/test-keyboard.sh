#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The on-screen keyboards' keys (port/app/keyboard.cpp) on the PC: every language's pages and what
# their keys type (tools/tests/test-keyboard.cpp). Needs clang++ (or g++).
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/../env.sh"
cd "$PS5CEMU_ROOT"
out=$PS5CEMU_BUILD/test-keyboard
mkdir -p "$out"
compiler=$(command -v clang++ || command -v g++)
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -DPS5KEYBOARD_CHECK -I port tools/tests/test-keyboard.cpp port/app/keyboard.cpp \
    -o "$out/test-keyboard"
"$out/test-keyboard"
