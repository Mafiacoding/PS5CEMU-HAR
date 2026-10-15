#!/usr/bin/env python3
"""Run cross-platform ZIP library smoke tests with a host C++20 compiler and libzip."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ps5cemu-zip-") as folder:
    p = Path(folder)
    for name in ("ds", "3ds", "wiiu"):
        (p/name).mkdir()
    with ZipFile(p/"ds"/"collection.zip", "w", ZIP_DEFLATED) as z:
        z.writestr("ds/Pokemon.nds", b"NDSROM" * 100)
        z.writestr("ds/Animal Crossing.srl", b"SRLROM" * 101)
        z.writestr("../escape.nds", b"BAD")
        z.writestr("bad/../../escape.nds", b"BAD")
        z.writestr("windows\\unsafe.nds", b"BAD")
    with ZipFile(p/"3ds"/"collection.zip", "w", ZIP_DEFLATED) as z:
        z.writestr("3ds/Super Mario.3DS", b"3DSROM" * 100)
        z.writestr("homebrew/Hello.3dsx", b"3DSXROM" * 30)
    with ZipFile(p/"wiiu"/"WiiUFolder.zip", "w", ZIP_DEFLATED) as z:
        z.writestr("Super Game/code/main.rpx", b"RPX" * 300)
        z.writestr("Super Game/meta/meta.xml", b"<xml></xml>")
        z.writestr("Super Game/content/music.pack", b"abcdef")
    with ZipFile(p/"wiiu"/"Single.zip", "w", ZIP_DEFLATED) as z:
        z.writestr("game/Single.wua", b"WUATEST" * 100)

    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "libzip"], text=True).strip())
    exe = p / "smoke"
    cmd = [os.environ.get("CXX", "g++"), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
           f"-DPS5CEMU_DATA=\"{p / 'user-data'}\"", "-I" + str(root / "tests" / "stubs"),
           "-I" + str(root / "port" / "app"), str(root / "port" / "app" / "zip_games.cpp"),
           str(root / "tests" / "zip_games_smoke.cpp"), "-o", str(exe)] + flags
    subprocess.run(cmd, check=True)
    for system in ("ds", "3ds", "wiiu"):
        subprocess.run([str(exe), str(p / system), system], check=True)
    assert not (p / "escape.nds").exists(), "ZIP path traversal"
    print("All three systems passed ZIP discovery, extraction, cache reuse and path safety smoke tests.")
