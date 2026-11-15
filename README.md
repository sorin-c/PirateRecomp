# Sid Meier's Pirates! (Xbox) — static recompilation

Firaxis' *Sid Meier's Pirates!* (Xbox, 2005) recompiled into a native Windows
program, built against the
[DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp) toolkit.
You bring your own copy of the game, and it is built from that copy on your
computer. This repository contains no game code or data.

**Status: bring-up.** Nothing runs yet. The goals, in order:

1. Playable on Windows.
2. Widescreen, 4K internal resolution and an uncapped frame rate.

## Layout

```
PirateRecomp/
  CMakeLists.txt          the project; pulls in the xboxrecomp/ submodule
  xboxrecomp/             the DirectXbox-Recomp toolkit (git submodule)
  src/main.c              entry point, game folder lookup (from the toolkit's template)
  src/recomp_manual.c     the title's own overrides, each with its reason
  src/recomp/gen/         lifted code (generated, not committed)
  config/seeds/           functions the disassembler cannot find on its own
  config/sections/        sections the XBE marks executable that are data
  release/pirates.json    the recipe a self-building release is made from
  scripts/recompile.ps1   runs the toolkit's lift pipeline for this game (Windows)
  scripts/recompile.sh    the same, for macOS and Linux
  game/                   your game files (not committed)
```

## Building

The toolkit's own requirements apply (its `docs/GETTING_STARTED.md`): on
Windows, Visual Studio 2022 with the C/C++ desktop workload (which brings
CMake), and Python 3.10+ with `capstone` (`py -3 -m pip install capstone`).

1. **Clone with the submodule.**

   ```
   git clone --recurse-submodules https://github.com/sorin-c/PirateRecomp.git
   cd PirateRecomp
   ```

   An existing clone: `git submodule update --init --recursive`.

2. **Game files.** Extract your disc image into `game/`, so that
   `game/default.xbe` exists beside the rest of the disc. The toolkit can do
   it:

   ```
   py -3 -m tools.xiso unpack "<your disc>.iso" -o ..\game\
   ```

   (run from `xboxrecomp/`), or use extract-xiso / xdvdfs.

3. **XDK symbols.** Build the toolkit's `third_party/XbSymbolDatabase` CLI
   once, then name the XDK functions in your XBE:

   ```
   cmake -S xboxrecomp\third_party\XbSymbolDatabase -B xboxrecomp\third_party\XbSymbolDatabase\build
   cmake --build xboxrecomp\third_party\XbSymbolDatabase\build --config Release
   cd xboxrecomp
   py -3 -m tools.xdk_symbols ..\game\default.xbe
   ```

   That writes `game/default_xdk_symbols.json`. Without it nothing is
   replaced by name, and the game runs with no picture and no sound.

4. **Lift.** `scripts\recompile.ps1` (a minute or two). It writes the lifted C
   into `src/recomp/gen/`.

5. **Build.**

   ```
   cmake -S . -B build
   cmake --build build --config Release --target pirates_recomp pirates_launcher
   ```

## Running a build of your own

```
build\Release\pirates_launcher.exe
```

The launcher starts `pirates_recomp` beside it with the chosen settings;
`build\Release\pirates_recomp.exe` starts the game directly. The game is
looked up at `game/` relative to the repository, or wherever `RECOMP_GAME_DIR`
points.

**Saves.** The game keeps its saves on the Xbox's E: drive, which the runtime
maps to the game folder: in a build of your own they are written into
`game/UDATA`. Test runs belong on a copy of `game/` without `UDATA`
(`RECOMP_GAME_DIR`) and with `RECOMP_SAVE_DIR` set to a fresh folder.

## What is specific to this game

Nothing yet. As the bring-up finds things, they go in `src/recomp_manual.c`,
`config/` and `CMakeLists.txt`, each with its reason, as the toolkit's other
title projects do.

## Licence

The code in this repository is under the licence in `LICENSE`. The toolkit is
GPL-3.0. The game is Firaxis' and 2K's; nothing of it is here.
