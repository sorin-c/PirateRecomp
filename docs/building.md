# Building it yourself

For development, or to build from the latest code. Done once, on Windows.

1. Install **Visual Studio 2022** with the *Desktop development with C++*
   workload (it includes CMake), **Git**, and **Python 3.10+**, then:

   ```
   py -3 -m pip install capstone
   ```

2. Get the code and the toolkit, and apply this project's toolkit patches:

   ```powershell
   git clone https://github.com/sorin-c/PirateRecomp.git
   cd PirateRecomp
   git submodule update --init
   git -C xboxrecomp submodule update --init
   Get-ChildItem patches\*.patch | Sort-Object Name | ForEach-Object { git -C xboxrecomp apply $_.FullName }
   ```

   Do not use `--recursive`: it also pulls xemu and its firmware trees, which
   the build does not need and which fail on Windows with "Filename too long".

3. Put **your own** disc in `game\`, so that `game\default.xbe` sits beside the
   rest of the disc. From an Xbox ISO (XISO or Redump):

   ```powershell
   cd xboxrecomp
   py -3 -m tools.xiso unpack "<your disc>.iso" -o ..\game\
   cd ..
   ```

   extract-xiso or xdvdfs work too. Only the retail disc listed under
   [The disc](../README.md#the-disc) is supported.

4. Name the XDK functions in your XBE (build the symbol tool once):

   ```powershell
   cmake -S xboxrecomp\third_party\XbSymbolDatabase -B xboxrecomp\third_party\XbSymbolDatabase\build
   cmake --build xboxrecomp\third_party\XbSymbolDatabase\build --config Release
   cd xboxrecomp
   py -3 -m tools.xdk_symbols ..\game\default.xbe
   cd ..
   ```

   This writes `game\default_xdk_symbols.json`. Without it the game runs with
   no picture and no sound.

5. Translate the game's code, then build:

   ```powershell
   scripts\recompile.ps1
   cmake -S . -B build
   cmake --build build --config Release --target pirates_recomp pirates_launcher
   ```

   `recompile.ps1` takes a minute or two and writes the generated C into
   `src\recomp\gen\` (never committed).

6. Start **`build\Release\pirates_launcher.exe`**, adjust the settings if you
   like, and press **Play**.

If a step fails, or the game misbehaves, open an
[issue](https://github.com/sorin-c/PirateRecomp/issues) with the error, your
Windows version and the end of the game's log.

## Making a release

`.github/workflows/release.yml` builds the Option A zip on a Windows runner
with the toolkit's `tools/release/make_release.py`, from
[`release/pirates.json`](../release/pirates.json) (the title, the launcher and
executable targets, and the SHA-1 of the supported `default.xbe`). Pushing a
version tag (`git tag v0.1.0 && git push origin v0.1.0`) attaches the zip to
a draft GitHub release, which is published by hand. A pull request that
touches the build makes the zip as a check. No game files are involved.

## Testing

The scripts in [`tools/test`](../tools/test/README.md) drive the game for
automated checks. Every run is **headless** (`RECOMP_HEADLESS=1`, or
`headless = 1`): the window opens off-screen, off the taskbar and never takes
focus, your keyboard and pads are not read (only the script presses
anything), and screenshots come from the game itself. Frames are still drawn
and paced exactly as in play. `drive.ps1 start -Visible` shows a run's window
behind the others, for watching one. A game you start yourself is not
affected.

## How it is built

```
PirateRecomp/
  CMakeLists.txt          the project; pulls in the xboxrecomp/ submodule
  xboxrecomp/             the DirectXbox-Recomp toolkit (git submodule)
  patches/                toolkit changes this game needs (toolkit-patches.md)
  src/main.c              entry point, game folder lookup
  src/recomp_manual.c     the game's own overrides, each with its reason
  src/settings_title.h    the game's lines in pirates.ini
  src/recomp/gen/         generated C (not committed)
  config/seeds/           functions the disassembler cannot find on its own
  config/sections/        sections the XBE marks executable that hold data
  release/pirates.json    the recipe for the self-building release (Option A)
  .github/workflows/      the release workflow
  scripts/recompile.*     runs the toolkit's translation pipeline for this game
  tools/test/             headless test scripts
  game/                   your game files (not committed)
```

The game needs 33 changes to the toolkit, kept as patches until they land
upstream: CPU translation fixes, file system and kernel timing fixes, fixed-
function lighting, skinning and shader fixes, frame interpolation for the
fixed-function pipeline, the in-game settings menu, display cropping and
controller handling. Each one is described, with the bug it fixes, in
[toolkit-patches.md](toolkit-patches.md).
