# Sid Meier's Pirates! (Xbox) — static recompilation

Firaxis' *Sid Meier's Pirates!* (Xbox, 2005) recompiled into a native Windows
program, built against the
[DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp) toolkit.
You bring your own copy of the game, and it is built from that copy on your
computer. This repository contains no game code or data.

**Status: bring-up, partly playable.** It builds and runs on Windows, with
twelve toolkit patches (below).

- Works: the logo movies, title screen and main menu with music; a new game
  through crew sign-up to the first town; the governor, tavern, merchant,
  shipwright and status screens; sailing the world map; saving at sea and
  loading after a restart; a VS MODE ship battle; the game's own 16:9 mode
  (`RECOMP_WIDESCREEN=1`) and a higher internal resolution
  (`RECOMP_RES_SCALE`).
- Broken: one of the governor's guests is
  drawn without a head; the game holds itself to 30 fps at sea and in
  battle.
- Fixed since the first status: the governor's welcome freeze and a crash
  during the town load (game threads ran in parallel; `RECOMP_GUEST_LOCK`
  below), and slow loading screens (the log was unbuffered;
  `RECOMP_PIRATES_LOG_BUFFER`).
- Minigames (16 Nov): a career boarding (button matching, the sword duel,
  defeat, prison and release) works. The game's own debug menu
  (`RECOMP_PIRATES_MINIGAMES` below) starts dancing, dueling, land battles
  and town sneaking, and all of them load and play (the garden duel's
  green skin is fixed by patch 0011). Open: the town-sneak hero was drawn
  without head or hands (not re-checked since 0011), and opening the pause
  menu in town sneak crashed twice in two runs (a corrupt heap block in
  sub_00141190; logs in `NOTES-for-Sorin.md`).
- Fixed 16 Nov: the first load on a fresh save folder froze on the loading
  screen. The game copies its archives to the cache only while a movie or
  loading screen is up, and with the movies skipped the copy stopped
  part-way through a file (`RECOMP_PIRATES_CACHE_PAUSE` below).
- Untested: treasure hunts (there is no debug entry for them; the landing
  party they start from works), a career ship battle to its end, and play
  with a real pad or keyboard.

The goals, in order:

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
   git -C xboxrecomp apply ..\patches\0006-d3d-spot-falloff-nan.patch
   git -C xboxrecomp apply ..\patches\0007-d3d-light-indices.patch
   git -C xboxrecomp apply ..\patches\0008-d3d-vertex-blending.patch
   git -C xboxrecomp apply ..\patches\0009-d3d-lighting-without-normals.patch
   git -C xboxrecomp apply ..\patches\0010-hle-declaration-blend-weights.patch
   git -C xboxrecomp apply ..\patches\0011-d3d-normal-matrix.patch
   git -C xboxrecomp apply ..\patches\0012-config-title-settings-file.patch
   git -C xboxrecomp apply ..\patches\0013-kernel-contiguous-protect.patch
   git -C xboxrecomp apply ..\patches\0014-kernel-tick-count-1ms.patch
   git -C xboxrecomp apply ..\patches\0015-flip-gate-lend-any-divisor.patch
   git -C xboxrecomp apply ..\patches\0016-flip-gate-vblanks-per-swap.patch
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
git -C xboxrecomp apply ..\patches\0006-d3d-spot-falloff-nan.patch
git -C xboxrecomp apply ..\patches\0007-d3d-light-indices.patch
git -C xboxrecomp apply ..\patches\0008-d3d-vertex-blending.patch
git -C xboxrecomp apply ..\patches\0009-d3d-lighting-without-normals.patch
git -C xboxrecomp apply ..\patches\0010-hle-declaration-blend-weights.patch
git -C xboxrecomp apply ..\patches\0011-d3d-normal-matrix.patch
git -C xboxrecomp apply ..\patches\0012-config-title-settings-file.patch
git -C xboxrecomp apply ..\patches\0013-kernel-contiguous-protect.patch
git -C xboxrecomp apply ..\patches\0014-kernel-tick-count-1ms.patch
git -C xboxrecomp apply ..\patches\0015-flip-gate-lend-any-divisor.patch
git -C xboxrecomp apply ..\patches\0016-flip-gate-vblanks-per-swap.patch
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

- **0006-d3d-spot-falloff-nan** (`src/d3d/d3d8_shaders.c`). The
  fixed-function lighting shader raised a spot light's cone ratio to its
  falloff everywhere, so a light with falloff 0 gave pow(0, 0), NaN on the
  GPU, at every vertex outside the cone, and the NaN made the whole lit
  colour black. The main menu is lit by two such spots, and the sea chart
  behind it came out black. The spot term now follows D3D: 0 outside the
  outer cone, 1 inside the inner one, and the falloff curve only between.

- **0007-d3d-light-indices** (`src/d3d/d3d8_device.c`). The host device
  kept 8 light slots and dropped `SetLight` and `LightEnable` for any index
  above 7. D3D8 allows any index and limits only how many lights are
  enabled at once. The game lights its rooms with lights 39-43, so
  characters in the governor's mansion, the shipwright's and the merchant's
  were lit by nothing and drew as black silhouettes. 256 slots now; the
  shader still packs the first 8 enabled.

- **0008-d3d-vertex-blending** (`src/d3d/d3d8_shaders.c`,
  `src/hle/hle_d3d8_state.c`). `D3DRS_VERTEXBLEND` was not forwarded, and
  the fixed-function vertex shader skipped the blend weights in XYZB1-5
  vertices. Every skinned vertex was drawn with world matrix 0 alone, so
  the game's characters came apart: torsos torn, limbs and coats out of
  place. The Xbox values (n WEIGHTS = 2n-1, n WEIGHTS n MATRICES = 2n)
  are forwarded, and the shader blends position and normal over up to
  four world matrices.

- **0009-d3d-lighting-without-normals** (`src/d3d/d3d8_shaders.c`). With
  lighting on, a vertex without a normal was left unlit, with a white
  diffuse colour. D3D lights it with a zero normal, so only the emissive
  and ambient terms count. The game darkens its stencil shadows with a lit,
  position-only quad whose colour comes from the material (black,
  translucent), and every character's shadow came out white.

- **0010-hle-declaration-blend-weights** (`src/hle/hle_d3d8.c`). A
  fixed-function draw described by a vertex declaration (no program) is
  drawn through an equivalent FVF, and the blend weights (v1) were
  dropped on the way, so 0008's blending never saw them: the dancers'
  dresses in the governor's ball came apart into strips. The weights now
  follow the position as XYZB1-4.

- **0011-d3d-normal-matrix** (`src/d3d/d3d8_shaders.c`). The fixed-function
  vertex shader's normal matrices (WorldInvTranspose, WorldViewInvTranspose)
  were uploaded transposed once too often, so the shader got inverse(world)
  instead of its transpose and turned every lit normal by the inverse of the
  world rotation. Objects whose world matrix rotates were lit from the wrong
  side: in the sword duel, the duellists' skin and shirts were lit only by
  the garden's teal fill lights behind them and came out green.

- **0012-config-title-settings-file** (`src/config/recomp_config.c`). A
  title can keep its settings in one file beside its executables
  (`RECOMP_TITLE_SETTINGS_FILE`), which the game and the launcher both
  read and write, instead of the per-user file under `%APPDATA%`. It can
  also add its own commented lines to that file
  (`RECOMP_TITLE_SETTINGS_HEADER`), which a save from the launcher keeps.
  Pirates! uses both: `pirates.ini`, with `src/settings_title.h`.

- **0013-kernel-contiguous-protect** (`src/kernel/kernel_memory.c`,
  `kernel_bridge.c`). `MmQueryAddressProtect` reports the protection a
  contiguous allocation was made with (write-combine and no-cache
  included), not the host's, which has no such bits. Pirates! builds its two
  contiguous heaps with write-combine and checks for it when one is full:
  without it, the overflow went to malloc, and those low addresses, freed
  through the D3D free hook (which sets bit 31), were freed into the other
  heap and corrupted it. That was the crash on quitting the town sneak.

- **0014-kernel-tick-count-1ms** (`src/kernel/kernel_hal.c`,
  `xbox_memory_layout.c`). `KeTickCount` counts milliseconds from the
  performance counter instead of copying `GetTickCount`, which moves in
  15.6 ms steps. Pirates! ends each frame at sea and in battle by sleeping
  until the tick is 31 ms past the frame's start; with 15.6 ms steps every
  frame took exactly 31.25 ms (32 fps).

- **0015-flip-gate-lend-any-divisor** (`src/kernel/xbox_memory_layout.c`).
  The flip gate lends its wait to frame interpolation at any vblank divisor,
  not only at one Swap a vblank, and tells it which vblank will release the
  frame. A title held to 30 can then show the frames between. Pirates! uses
  it with `frame_interp = 2` in `pirates.ini` (`src/recomp_manual.c`,
  `sub_001A958A`): at sea and in battles the game keeps its 30 and the
  screen shows 60.

- **0016-flip-gate-vblanks-per-swap** (`src/kernel/xbox_memory_layout.c`).
  `xbox_Nv2aFlipGateVblanksPerSwap()` says how many vblanks each Swap waits
  for now. The player's frame cap outranks the title's presentation
  interval, so Pirates! checks it before handing its 30 fps wait to the
  gate: with `frame_cap = 60` the gate ran those scenes at 60 and the game
  at twice its speed. Now the game then keeps its own wait.

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

**Settings: `pirates.ini`.** The settings live in `pirates.ini`, beside
`pirates_launcher.exe` and `pirates_recomp.exe` (`build\Release\pirates.ini`
in a build of your own). It is written with the defaults and a comment on
every line the first time either program runs, and both read it; edit it in
any text editor, or change the common settings in the launcher. Delete it to
go back to the defaults. Every line names the environment variable that
overrides it, so a `.bat` file or a command line still wins.

The lines a player would touch: `widescreen` (the game's own 16:9 mode),
`resolution_scale` (render 2-8 times larger), `frame_cap`, `fps_overlay`,
`fullscreen`, `skip_intro` (`1`, `intro` or `0`), `minigames` (the debug
MINIGAMES menu) and `mute`. A section marked for debugging holds
`guest_lock`, `log_buffer` and `cache_pause`.

The game's own switches, by environment variable (the `pirates.ini` key is
given first in each row; the toolkit's are listed in `xboxrecomp/CLAUDE.md`):

| Variable | Default | Effect |
| --- | --- | --- |
| `skip_intro`, `RECOMP_PIRATES_SKIP_INTRO` | `1` | Skip every Bink movie through the movie player's own "not played" exit (`src/recomp_manual.c`): the 2K and Firaxis logos at boot, and the opening story (`PiratesOpening1.bik`) and mutiny (`PiratesOpening2.bik`) after NEW GAME, which are pre-rendered movies, not in-engine scenes. `intro` skips only the 2K, Firaxis and Pirates! movies; `0` plays everything as the disc does. On during bring-up. |
| `log_buffer`, `RECOMP_PIRATES_LOG_BUFFER` | `1` | The log (stderr) is buffered (64 KB). The toolkit logs every file read, path lookup and event, and unbuffered that took about half the game's main-thread time: a new game's first town loaded in 47 s or more instead of 10-16 s. `0` goes back to unbuffered, for a crash that leaves no report. |
| `minigames`, `RECOMP_PIRATES_MINIGAMES` | `0` | `1` shows the developers' own "MINIGAMES (*DEBUG!*)" entry on the main menu: dancing, dueling (any stage and difficulty), button matching, a ship battle, town sneaking and a land battle, each straight from the menu. The menu draws it while byte 0x004D69C0 is set (`src/recomp_manual.c` sets it); the game toggles it on a debug input no pad produces. For testing; a player never needs it. |
| `treasure_map`, `RECOMP_PIRATES_TREASURE_MAP` | `0` | `n` buys up to `n` pieces of a buried-treasure map, free, the way the mysterious traveller sells one (`sub_000AA3C0(0, x, y, 1, ...)` from `src/recomp_manual.c`), whenever a town menu opens with no treasure hunt under way; `2` makes a whole map on the default difficulty. For testing treasure hunts without waiting for the traveller or a beaten pirate's map; a player never needs it. |
| `cache_pause`, `RECOMP_PIRATES_CACHE_PAUSE` | `0` | The game copies its `.FPK` archives from the disc into the cache (`<save folder>\Cache`) on first use, on a job thread that copies only while its "go" event is set: the game sets it for a movie or a loading screen and resets it afterwards, so the copy never competes with gameplay for the DVD. With the movies skipped those windows were microseconds long, the copy stopped part-way through a file, and loading a save then waited for that file: three of three loads on a fresh save folder froze. `src/recomp_manual.c` leaves that one event set, so the copy finishes in the background (about a minute, once per save folder). `1` passes the resets on as the disc does. |
| `guest_lock`, `RECOMP_GUEST_LOCK` | `1` (set by `src/main.c`) | One game thread runs lifted code at a time, as on the console's single CPU (the toolkit's default is on only for ARM). Without it, about half the runs through a new career's first governor visit froze or crashed: the main thread stopped after NEW GAME, the governor's welcome hung on its last line, or two threads crashed in one memmove. With it, 7 of 7 runs went through, at the same load time and frame rate. `0` turns it off. |

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
