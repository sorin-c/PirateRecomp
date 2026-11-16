# Sid Meier's Pirates! (Xbox) — static recompilation

Firaxis' *Sid Meier's Pirates!* (Xbox, 2005) recompiled into a native Windows
program, built against the
[DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp) toolkit.
You bring your own copy of the game, and it is built from that copy on your
computer. This repository contains no game code or data.

**Status: bring-up.** The XBE lifts cleanly (15,947 functions); nothing has
been built or run yet. The goals, in order:

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
   git clone https://github.com/sorin-c/PirateRecomp.git
   cd PirateRecomp
   git submodule update --init
   git -C xboxrecomp submodule update --init
   ```

   That is the toolkit and its direct dependencies only. A fully recursive
   checkout also pulls xemu and its firmware trees, which the build does not
   use and which fail on Windows with "Filename too long".

   Then apply the toolkit patches (see [Toolkit patches](#toolkit-patches)),
   before the lift:

   ```
   git -C xboxrecomp apply ..\patches\0001-lifter-xadd-flags-snapshot.patch
   git -C xboxrecomp apply ..\patches\0002-kernel-empty-directory-pattern.patch
   git -C xboxrecomp apply ..\patches\0003-input-live-file.patch
   git -C xboxrecomp apply ..\patches\0004-d3d-bump-env-map.patch
   git -C xboxrecomp apply ..\patches\0005-hle-walk-draw-simple-states.patch
   ```

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

## Toolkit patches

`patches/` holds changes to the toolkit that this game needs and that are not
in the pinned `xboxrecomp` commit yet. They are applied to the submodule's
working tree, never committed inside it, so the submodule still points at an
upstream commit. Apply them in number order, after the submodule init and
before the lift, from the repository root:

```
git -C xboxrecomp apply ..\patches\0001-lifter-xadd-flags-snapshot.patch
git -C xboxrecomp apply ..\patches\0002-kernel-empty-directory-pattern.patch
git -C xboxrecomp apply ..\patches\0003-input-live-file.patch
git -C xboxrecomp apply ..\patches\0004-d3d-bump-env-map.patch
git -C xboxrecomp apply ..\patches\0005-hle-walk-draw-simple-states.patch
```

- **0001-lifter-xadd-flags-snapshot** (`tools/recomp/lifter.py`,
  `translator.py`, and a regression test in `test_lifter_lock_flags.py`).
  The jcc after `xadd` / `lock xadd` was lifted as a re-read of the
  destination (`MEM32(edi) != 0`) at the branch instead of the sum the
  instruction produced. This game releases a reference with
  `lock xadd [edi], eax / pop edi / jne` (0x0020AD80), so the branch tested
  whatever the popped edi pointed at, and every child added to a UI node was
  destroyed on the spot. The game then made virtual calls into freed memory
  and crashed about a second after `CreateDevice`. The re-read was also racy
  across guest threads. The lift now snapshots the sum into `_fa`/`_fas`,
  as it already did for `cmpxchg`. Without it the build boots and crashes;
  with it the game reaches its main menu.

- **0002-kernel-empty-directory-pattern** (`src/kernel/kernel_file.c`).
  On Windows, `NtQueryDirectoryFile` with a zero-length file name built
  the search path `<dir>\` and `FindFirstFileW` failed. XAPI's
  `FindFirstFile` passes exactly that for `*.*`, and the game indexes its
  loose files by scanning `d:\Assets\*.*` recursively, so it found none of
  `assets\Bink\*.bik` or `assets\sounds\*.wav`: no logo movies and no
  music, with no error anywhere. An empty name now means every entry, as the
  POSIX backend already read it. With it the 2K and Firaxis movies play
  with sound and the title and menu music plays.

- **0003-input-live-file** (`src/hle/input_host.c`). Test tooling, not a
  game fix: `RECOMP_INPUT_LIVE=<file>` presses each line appended to the
  file (`a`, `dpad_down:300`, `lstick_left+a:800`), one after another.
  Fixed `RECOMP_INPUT_SEQ` timelines drift by tens of seconds over this
  game's long loading screens; this lets a test look at a screenshot first
  and press second (`tools/test/drive.ps1`). Does nothing unless the
  variable is set.

- **0004-d3d-bump-env-map** (`src/d3d/d3d8_combiners.c`, `.h`). The pixel
  shader translator sampled `BUMPENVMAP` and `BUMPENVMAP_LUM` stages as
  plain 2D textures and ignored the bump matrix, which the HLE layer
  already forwards. The stage's coordinates are now offset by the source
  stage's (du, dv) through `BUMPENVMAT00..11`, reading an unsigned colour's
  bytes as two's complement as the NV2A does. The game's
  `PSDistortReflections_XB` pass uses it to ripple the reflection the sea
  samples. `_LUM`'s luminance scale is still not applied (`BUMPENVLSCALE`
  and `LOFFSET` are not forwarded).

- **0005-hle-walk-draw-simple-states** (`src/hle/hle_d3d8_state.c`,
  `hle_d3d8_vertex.c`). The game pushes its terrain draws into the push
  buffer itself, and the toolkit's push-buffer walk only draws them at the
  next replaced call. By then the game's SetRenderState has already stored
  the states for what comes next in the state array: blending, alpha test
  and depth write put back after the third terrain pass. Drawn with those,
  that pass painted its faded-out deep-water vertices over the sea as opaque
  black tiles, in seven frames out of eight. The walk now keeps the simple
  render states (depth, blend, alpha test, colour mask, stencil) in buffer
  order, and the draws it makes use them. The array itself is unchanged.
  `RECOMP_HLE_D3D8_WALK_RS=0` turns it off. Cull mode is not covered.

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
The save folder also holds the emulated console's identity (`console.ini`,
its MAC address, part of what XAPI signs settings and saves with), so
deleting it or pointing `RECOMP_SAVE_DIR` somewhere new makes existing
settings and console-bound saves read as damaged: keep one folder per player.

**Settings.** Environment variables this game reads on top of the toolkit's
own (`xboxrecomp/CLAUDE.md` lists those):

| Variable | Default | Effect |
| --- | --- | --- |
| `RECOMP_PIRATES_SKIP_INTRO` | `1` | Skip every Bink movie through the movie player's own "not played" exit (`src/recomp_manual.c`): the 2K and Firaxis logos at boot, and the opening story (`PiratesOpening1.bik`) and mutiny (`PiratesOpening2.bik`) after NEW GAME, which are pre-rendered movies, not in-engine scenes. `intro` skips only the 2K, Firaxis and Pirates! movies; `0` plays everything as the disc does. On during bring-up. |

## The disc

The retail disc (USA, Europe, En/Fr/De/Es/It): title ID `0x5454008F`, XDK
5849, built 2005-06-20, entry point `0x001AC88A`. `default.xbe` SHA-1
`19191d2b0364c6ca7ee2a6fc063f3c8e3c843244`. Beside the game's code it links
D3D8, D3DX8, DirectSound, Xbox Live (XONLINE/XNET), Bink video, Miles'
MSSMIXER and the Dolby encoder; XbSymbolDatabase names 399 of the XDK
functions in it.

## What is specific to this game

Nothing yet. As the bring-up finds things, they go in `src/recomp_manual.c`,
`config/` and `CMakeLists.txt`, each with its reason, as the toolkit's other
title projects do.

## Licence

The code in this repository is under the licence in `LICENSE`. The toolkit is
GPL-3.0. The game is Firaxis' and 2K's; nothing of it is here.
