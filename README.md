# Sid Meier's Pirates! Recomp

An experimental Windows recompilation of the original Xbox release of
**Sid Meier's Pirates! (2005)**, built with the
[DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp) toolkit.
The game's code is translated into C and compiled into a native PC program,
with a Direct3D renderer, XInput and SDL controller support, keyboard
controls, a settings launcher, an in-game settings menu, widescreen, higher
internal resolutions and 60 fps through frame interpolation.

Neither this repository nor its downloads contain any game code or data. You
supply your own copy of the game, and the PC version is built from it on your
computer.

> **Status: partly playable.** A new career runs from crew sign-up through
> towns, sailing, ship battles, boarding duels and saving. Some parts are not
> tested yet; see [Status](#status).

## Start here

There are two ways to get the game running. Both need your own disc image of
the original Xbox game (see [The disc](#the-disc)).

### Option A: download and play (no compiler needed)

1. Download `PirateRecomp-<version>-windows-x86_64.zip` from the
   [Releases](https://github.com/sorin-c/PirateRecomp/releases) page and
   unpack it anywhere.
2. Start **`PirateRecomp.exe`**. The release is not signed, so the first time
   Windows may say it protected your PC: choose *More info*, then *Run anyway*.
3. The first time, it asks for your copy of the game: choose **Disc
   image...** for an `.iso` (XISO or Redump), or **Folder...** for a folder
   with the disc's files. It checks that it is the supported disc, then
   builds the game from it on your PC. This takes a few minutes and happens
   once.
4. The settings appear; press **Play**.

The download is the toolkit's *self-building release*: the launcher, a
compiler (zig), CMake, Ninja, Python and the translation pipeline, about
600 MB unpacked, so nothing else needs installing on 64-bit Windows 10 or 11.
What it builds goes in `%LOCALAPPDATA%\xboxrecomp\builds\5454008F`: the game,
its files (an image is extracted there once; a folder is linked, never
changed), your saves (`game\UDATA`), `pirates.ini` and `build.log`, the file
to attach to a bug report. A newer release finds the old build out of date and
offers **Build again**, with the same disc remembered.

### Option B: build it yourself

For development, or to build from the latest code. Done once, on Windows, in
a few commands and several minutes.

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
   [The disc](#the-disc) is supported.

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

## The launcher

`pirates_launcher.exe` is the toolkit's launcher, set up for this game. It
sits beside `pirates_recomp.exe` and starts it with your settings.

- **Video**: resolution, fullscreen, widescreen, frame rate cap, frame
  interpolation, texture sharpness, variable refresh rate and the frame-rate
  counter.
- **Input**: keyboard and mouse bindings and the controllers.
- **Play** starts the game. `pirates_recomp.exe` can also be started directly.

The launcher and the game share one settings file, `pirates.ini`, so a change
in either shows up in the other.

## In-game settings menu

Press **F2**, or **Start + Back** together on a controller, to open a settings
panel over the game. While it is open, the game receives no input. Start or
Back pressed alone still reaches the game as usual.

| Setting | Applies |
| --- | --- |
| Frame rate cap (adaptive, 60, 30, off) | at once |
| Frame interpolation (off, 2x, 3x, 4x) | at once |
| Frame-rate counter | at once |
| Fullscreen | at once |
| Internal resolution (1x to 8x) | next start |
| Widescreen (16:9) | next start |
| Texture filtering (off to 16x) | next start |
| Variable refresh | next start |
| Movies (skip all, skip intro, play all) | next start |
| Sound | next start |

Every change is written to `pirates.ini` straight away, keeping its comments.

## Display and frame rate

- **60 fps.** The game runs its world at 30 fps at sea, in battles, duels and
  the town sneak. Frame interpolation draws the frame in between, so you see
  60 while the game keeps its original speed. This is on by default
  (`frame_interp = 2`); menus, towns and the governor's rooms already run
  at 60. Characters, ships and the camera are all interpolated.
- **Widescreen.** `widescreen = 1` turns on the game's own 16:9 mode. The
  front end (legal text, title, menus and the new-career pages) stays at 4:3
  between bars, as it was drawn, and the game world fills the screen.
- **Internal resolution.** `resolution_scale` renders 2 to 8 times larger
  than the console.
- **Bottom and left bands.** The console leaves a grey-lavender band along
  the bottom of the sea view, and in widescreen the main menu's map stops
  short of the left edge. Both are always cropped off.

## Controllers

- Xbox controllers work through XInput; PlayStation and Switch pads through
  SDL's HIDAPI support. A pad plugged in after the game starts is picked up.
- **Controller 1 follows the pad you used last** (`pad_follow_input = 1`, the
  default): press anything on any pad and it becomes controller 1. Set it to
  `0` to keep each pad in its own slot, for several players.
- **DirectInput is off** (`pad_directinput = 0`). Scanning DirectInput devices
  delayed the first pad by 12 to 40 seconds on some PCs. Set it to `1` only
  for an older pad that only speaks DirectInput.
- The keyboard works throughout; bindings are in the launcher's Input tab.

## Settings: `pirates.ini`

`pirates.ini` lives beside the two executables (`build\Release\pirates.ini`).
Either program writes it with the defaults and a comment on every line the
first time it runs. Edit it in any text editor, or delete it to go back to the
defaults. Every line names the environment variable that overrides it, so a
`.bat` file or a command line still wins.

| Key | Default | What it does |
| --- | --- | --- |
| `widescreen` | `0` | The game's own 16:9 mode. |
| `resolution_scale` | `1` | Render 1 to 8 times the console's resolution. |
| `frame_cap` | `adaptive` | `adaptive` (as the console), `60`, `30` or `0` (off). |
| `frame_interp` | `2` | `1` off, `2` shows 60 where the game runs at 30. |
| `fps_overlay` | `0` | Frame-rate counter. |
| `fullscreen` | `0` | Fullscreen window. |
| `anisotropy` | `1` | Texture filtering, `1` to `16`. |
| `vrr` | `0` | Variable refresh rate. |
| `pad_follow_input` | `1` | Controller 1 is the pad used last. |
| `pad_directinput` | `0` | SDL's DirectInput back end. |
| `skip_intro` | `1` | `1` skips every movie, `intro` only the logos and title movie, `0` plays all. |
| `mute` | `0` | No sound. |
| `minigames` | `0` | Shows the developers' MINIGAMES (*DEBUG!*) menu entry. |
| `headless` | `0` | Test runs only (see [Testing](#testing)). |

<details>
<summary>Debugging and test switches</summary>

| Key, variable | Default | Effect |
| --- | --- | --- |
| `guest_lock`, `RECOMP_GUEST_LOCK` | `1` | One game thread runs at a time, as on the console's single CPU. Without it about half the runs through a new career's first governor visit froze or crashed. |
| `log_buffer`, `RECOMP_PIRATES_LOG_BUFFER` | `1` | Buffers the log. Unbuffered, a new game's first town took 47 s or more to load instead of 10 to 16 s; `0` keeps every line before a crash. |
| `cache_pause`, `RECOMP_PIRATES_CACHE_PAUSE` | `0` | The game copies its `.FPK` archives to the cache only while a movie or loading screen is up. With movies skipped the copy stopped part-way and a first load froze; `1` restores the console's behaviour. |
| `treasure_map`, `RECOMP_PIRATES_TREASURE_MAP` | `0` | Gives `n` pieces of a buried-treasure map when a town menu opens (`2` is a whole map), for testing treasure hunts. |
| `test_probe`, `RECOMP_PIRATES_TEST_PROBE` | (empty) | A folder where the game writes ship positions (`pos.txt`, `ships.txt`, `battle.txt`) and reads `teleport.txt`, for scripted test runs. |

The toolkit's own variables are listed in `xboxrecomp/CLAUDE.md`.

</details>

## Saves

The game keeps its saves on the Xbox's E: drive, which maps to the game
folder: in your build they are written to `game\UDATA`. The save folder also
holds the emulated console's identity (`console.ini`), which the game signs
settings and saves with. Deleting it or pointing `RECOMP_SAVE_DIR` somewhere
new makes existing saves read as damaged, so keep one save folder per player.

## Status

- **Works:** the logo movies, title screen and menu music; a new career from
  crew sign-up to the first town; the governor, tavern, merchant, shipwright
  and status screens; sailing the world map; a VS MODE ship battle; boarding with button matching and the sword duel, defeat, prison
  and release; dancing, land battles and the town sneak (from the debug
  minigames menu); saving at sea and loading after a restart.
- **Display:** widescreen, internal resolution up to 8x, 60 fps through
  interpolation at the original game speed.
- **Not tested yet:** treasure hunts end to end, a career ship battle to its
  end, and long play sessions.

## Testing

The scripts in [`tools/test`](tools/test/README.md) drive the game for
automated checks. Every run is **headless** (`RECOMP_HEADLESS=1`, or
`headless = 1`): the window opens off-screen, off the taskbar and never takes
focus, your keyboard and pads are not read (only the script presses
anything), and screenshots come from the game itself. Frames are still drawn
and paced exactly as in play. `drive.ps1 start -Visible` shows a run's window
behind the others, for watching one. A game you start yourself is not
affected.

## Making a release

`.github/workflows/release.yml` builds the Option A zip on a Windows runner
with the toolkit's `tools/release/make_release.py`, from
[`release/pirates.json`](release/pirates.json) (the title, the launcher and
executable targets, and the SHA-1 of the supported `default.xbe`). Pushing a
version tag (`git tag v0.1.0 && git push origin v0.1.0`) attaches the zip to
a draft GitHub release, which is published by hand. A pull request that
touches the build makes the zip as a check. No game files are involved.

## How it is built

```
PirateRecomp/
  CMakeLists.txt          the project; pulls in the xboxrecomp/ submodule
  xboxrecomp/             the DirectXbox-Recomp toolkit (git submodule)
  patches/                toolkit changes this game needs (docs/toolkit-patches.md)
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
[docs/toolkit-patches.md](docs/toolkit-patches.md).

## The disc

Supported: the retail disc **Sid Meier's Pirates! (USA, Europe)
(En,Fr,De,Es,It)**, title ID `0x5454008F`, XDK 5849, built 2005-06-20.
`default.xbe` SHA-1 `19191d2b0364c6ca7ee2a6fc063f3c8e3c843244`. Other
revisions and the PC version are not supported.

## Credits and licensing

Built on [DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp)
by phobos665, itself based on
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) by sp00nz, with
xemu-derived hardware components. Original Xbox behaviour was checked against
[xemu](https://xemu.app/) and community documentation such as
[XboxDevWiki](https://xboxdevwiki.net/). Development included assistance from
Claude Code.

| Submodule | Source | Used for |
| --- | --- | --- |
| `xboxrecomp` | [phobos665/DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp) | The translation pipeline and runtime (D3D, kernel, audio, input, launcher), with `patches/` applied |

The code in this repository is under the licence in [`LICENSE`](LICENSE). The
toolkit is GPL-3.0. *Sid Meier's Pirates!* belongs to Firaxis Games and 2K
Games; nothing of the game is included here, and this project is not
affiliated with them. Use only a copy of the game you own.
