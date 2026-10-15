#!/usr/bin/env python3
"""Apply the PS5CEMU-HAR Cemu/Azahar/melonDS ZIP support overlay to a checkout.

Usage: python3 apply_zip_support.py /path/to/PS5CEMU-HAR
Targeted at premohq/PS5CEMU-HAR, branch PS5-Cemu, source tree 81c1496 (2026-10-15).
Refuses partial/ambiguous edits, and does not touch .deps, ROMs or saved games.
"""
from pathlib import Path
import argparse
import shutil
import sys


def apply(root, dry_run=False):
    package = Path(__file__).resolve().parent
    changes = {}

    def read(path):
        file = root / path
        if not file.is_file():
            raise RuntimeError(f"Expected source file missing: {path}")
        return changes.get(path, file.read_text(encoding="utf-8"))

    def change(path, old, new):
        text = read(path)
        count = text.count(old)
        if count != 1:
            raise RuntimeError(f"{path}: expected the target to appear once, found {count}. Wrong revision or already patched?")
        changes[path] = text.replace(old, new, 1)

    # Launch metadata is kept in library.json so a ZIP with more than one game can
    # launch the exact same internal ROM after the app is restarted.
    change("port/app/emulator.h", "\t\tstd::filesystem::path path;\t // what Cemu launches\n",
           "\t\tstd::filesystem::path path;\t // original ZIP or a regular game file\n"
           "\t\tstd::string archiveMember; // ZIP entry path, empty for a ZIP title at the archive root\n"
           "\t\tbool archiveFolder = false; // ZIP contains a full Wii U code/content/meta tree\n")
    change("port/app/catalog.cpp", '\t\t\t\tentry.game.path = String(item, "path");\n',
           '\t\t\t\tentry.game.path = String(item, "path");\n'
           '\t\t\t\tentry.game.archiveMember = String(item, "archiveMember");\n'
           '\t\t\t\tentry.game.archiveFolder = item.HasMember("archiveFolder") && item["archiveFolder"].IsBool() && item["archiveFolder"].GetBool();\n')
    change("port/app/catalog.cpp", '\t\t\t\twriter.String(entry.game.path.string().c_str());\n',
           '\t\t\t\twriter.String(entry.game.path.string().c_str());\n'
           '\t\t\t\tif (entry.game.archiveFolder || !entry.game.archiveMember.empty())\n'
           '\t\t\t\t{\n'
           '\t\t\t\t\twriter.Key("archiveMember");\n'
           '\t\t\t\t\twriter.String(entry.game.archiveMember.c_str());\n'
           '\t\t\t\t\twriter.Key("archiveFolder");\n'
           '\t\t\t\t\twriter.Bool(entry.game.archiveFolder);\n'
           '\t\t\t\t}\n')

    change("port/app/emulator.cpp", '#include "paths.h"\n', '#include "paths.h"\n#include "zip_games.h"\n')
    change("port/app/emulator.cpp",
           '\t\tstd::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return boost::ilexicographical_compare(a.name, b.name); });\n',
           '\t\t// ZIP entries have no Cemu title metadata until the player starts them.\n'
           '\t\t// The loader will unpack only the chosen game into the private cache.\n'
           '\t\tfor (const auto& folder : GetConfig().game_paths)\n'
           '\t\t\tps5zip::AddArchives(folder, ps5zip::System::WiiU, games);\n'
           '\t\tstd::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return boost::ilexicographical_compare(a.name, b.name); });\n')

    change("port/azahar/library.cpp", '#include "../app/paths.h"\n',
           '#include "../app/paths.h"\n#include "../app/zip_games.h"\n')
    change("port/azahar/library.cpp", '\t\t\tScan(folder, 0, games);\n\t\t\tScanInstalled(games);\n',
           '\t\t\tScan(folder, 0, games);\n'
           '\t\t\tps5zip::AddArchives(folder, ps5zip::System::N3ds, games);\n'
           '\t\t\tScanInstalled(games);\n')

    change("port/melonds/library.cpp", '#include "../app/paths.h"\n',
           '#include "../app/paths.h"\n#include "../app/zip_games.h"\n')
    change("port/melonds/library.cpp", '\t\t\tScan(folder, 0, games);\n\t\t\tstd::sort(games.begin()',
           '\t\t\tScan(folder, 0, games);\n'
           '\t\t\tps5zip::AddArchives(folder, ps5zip::System::Nds, games);\n'
           '\t\t\tstd::sort(games.begin()')

    # ZIP games can contain identical inner filenames in separate archives. Give DS saves,
    # save states and cheat filenames stable unique names instead of overwriting each other.
    change("port/melonds/core.cpp", '\t\ts_stem = game.path.stem().string();\n',
           '\t\ts_stem = game.path.stem().string();\n'
           '\t\tif (game.archiveFolder || !game.archiveMember.empty())\n'
           '\t\t\ts_stem += fmt::format("-{:016x}", game.titleId);\n')

    change("port/main_ps5.cpp", '#include "app/updates.h"\n', '#include "app/updates.h"\n#include "app/zip_games.h"\n')
    change("port/main_ps5.cpp",
           "return tile.rom.find('/') == std::string::npos ? game.path.filename().string() == tile.rom : game.path.string() == tile.rom;",
           "return tile.rom.find('/') == std::string::npos ? game.path.filename().string() == tile.rom : ps5zip::TileKey(game) == tile.rom;")
    change("port/main_ps5.cpp", '\t\tif (choice->system == ps5launcher::System::Nds)\n\t\t{\n',
           '\t\t// Extract on demand, not during library scans. The user can keep ZIPs on USB or\n'
           '\t\t// a network share; the checked output lives in /data/ps5cemu/cache/zip-games.\n'
           '\t\t// Keep game.titleId and the original path for settings, favourites and tiles.\n'
           '\t\tps5emu::Game preparedGame;\n'
           '\t\tif (!ps5zip::Prepare(game, preparedGame, error))\n'
           '\t\t{\n'
           '\t\t\tps5log::Line("[zip] {} could not be prepared: {}", game.name, error);\n'
           '\t\t\tif (choice->system == ps5launcher::System::Nds)\n'
           '\t\t\t{\n\t\t\t\tstatus.noticeDs = error;\n\t\t\t\tsettings.side = "ds";\n\t\t\t}\n'
           '\t\t\telse if (choice->system == ps5launcher::System::N3ds)\n'
           '\t\t\t{\n\t\t\t\tstatus.launchError3ds = error;\n\t\t\t\tsettings.side = "3ds";\n\t\t\t}\n'
           '\t\t\telse\n'
           '\t\t\t{\n\t\t\t\tstatus.launchError = error;\n\t\t\t\tsettings.side = "wiiu";\n\t\t\t}\n'
           '\t\t\tcontinue;\n'
           '\t\t}\n\n'
           '\t\tif (choice->system == ps5launcher::System::Nds)\n\t\t{\n')
    change("port/main_ps5.cpp", 'ps5melonds::LaunchGame(game, forGame.nds, error)',
           'ps5melonds::LaunchGame(preparedGame, forGame.nds, error)')
    change("port/main_ps5.cpp", 'ps5azahar::LaunchGame(game, forGame.n3ds, error)',
           'ps5azahar::LaunchGame(preparedGame, forGame.n3ds, error)')
    change("port/main_ps5.cpp", 'ps5emu::LaunchGame(game, error)',
           'ps5emu::LaunchGame(preparedGame, error)')

    # The home-screen tile must pick the actual member, not the first game in a ZIP.
    change("port/frontend/shell/shell.cpp", '#include "../../app/updates.h"\n',
           '#include "../../app/updates.h"\n#include "../../app/zip_games.h"\n')
    change("port/frontend/shell/shell.cpp", 'ps5forwarders::Start({game.entry.game.name, game.entry.game.path.string(), cover});',
           'ps5forwarders::Start({game.entry.game.name, ps5zip::TileKey(game.entry.game), cover});')
    change("port/frontend/shell/shell.cpp", 'ps5forwarders::StartRemove({game.entry.game.name, game.entry.game.path.string(), {}});',
           'ps5forwarders::StartRemove({game.entry.game.name, ps5zip::TileKey(game.entry.game), {}});')
    change("port/frontend/shell/shell.cpp", 'ps5forwarders::HasTile(g.entry.game.path.string())',
           'ps5forwarders::HasTile(ps5zip::TileKey(g.entry.game))')
    change("port/frontend/shell/shell.cpp", 'if (named ? path.filename().string() == rom : path.string() == rom)',
           'if (named ? path.filename().string() == rom : ps5zip::TileKey(m_games[i].entry.game) == rom)')

    change("port/CMakeLists.txt", '\tapp/emulator.h\n',
           '\tapp/emulator.h\n\tapp/zip_games.cpp\n\tapp/zip_games.h\n')
    change("port/CMakeLists.txt", '\tapp/catalog.cpp\n\tapp/compatibility.cpp\n',
           '\tapp/catalog.cpp\n\tapp/zip_games.cpp\n\tapp/compatibility.cpp\n')

    change("README.md",
           '| **Plays** | `.wua`, `.wud`, `.wux`, unpacked games | `.3ds`, `.cci`, `.cxi`, `.app`, `.3dsx`, `.elf`, compressed `.z*`, installed `.cia` | `.nds` |',
           '| **Plays** | `.wua`, `.wud`, `.wux`, unpacked games, ZIP archives | `.3ds`, `.cci`, `.cxi`, `.app`, `.3dsx`, `.elf`, compressed `.z*`, installed `.cia`, ZIP archives | `.nds`, `.srl`, ZIP archives |')
    change("README.md", '## Install\n',
           '### ZIP games (all three systems)\n\n'
           'Place a `.zip` containing a supported ROM in the relevant Wii U, 3DS or DS games folder. '
           'Nested folders and multiple ROMs per ZIP are supported. A Wii U ZIP of an unpacked game '
           'must contain `code/*.rpx` and `meta/meta.xml` (and normally `content/`); it appears as one title. '
           'The launcher reads only the ZIP directory when scanning; on first launch, the selected ROM '
           '(or the whole unpacked Wii U title) is validated and extracted to '
           '`/data/ps5cemu/cache/zip-games/`. Further launches reuse the cache until the ZIP size or '
           'timestamp changes. This requires enough free PS5 storage for the uncompressed game. '
           'Clear that cache folder to recover storage. ZIPs are **not** streamed or run in place. '
           'Encrypted/password-protected ZIPs are unsupported. Compression does not bypass game '
           'decryption or BIOS/key requirements; DSi and CIA installation remain subject to the '
           'emulators\' existing restrictions. ZIP entries use archive/entry filenames as library titles '
           'until the game is opened.\n\n'
           '## Install\n')

    # No writes happen until every substitution above is verified.
    for path, text in changes.items():
        if not dry_run:
            (root / path).write_text(text, encoding="utf-8")
    if not dry_run:
        for name in ("zip_games.cpp", "zip_games.h"):
            shutil.copyfile(package / "port/app" / name, root / "port/app" / name)
    return sorted(changes) + ["port/app/zip_games.cpp", "port/app/zip_games.h"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path)
    parser.add_argument("--check", action="store_true", help="verify substitutions; do not write")
    args = parser.parse_args()
    try:
        for path in apply(args.repository.resolve(), args.check):
            print(("[check] " if args.check else "[updated] ") + path)
    except Exception as error:
        print("ZIP patch not applied: " + str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
