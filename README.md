# Sid Meier's Pirates! Recomp

An experimental Windows port of the original Xbox **Sid Meier's Pirates!
(2005)**, made by static recompilation with
[DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp). It adds
widescreen, higher resolutions, 60 fps through frame interpolation,
controller and keyboard support, and a settings launcher.

No game code or data is included. You need your own copy of the game, and
the PC version is built from it on your computer.

> **Status: partly playable.** A new career runs from crew sign-up through
> towns, sailing, ship battles, boarding duels and saving. Treasure hunts,
> full career battles and long sessions are not tested yet.

## Install

You need 64-bit Windows 10 or 11, about 3 GB of free space, and an image of
your **original Xbox** disc (see [The disc](#the-disc)).

### Option A: download (recommended)

1. Download `PirateRecomp-<version>-windows-x86_64.zip` from
   [Releases](https://github.com/sorin-c/PirateRecomp/releases) and unpack it.
2. Start `PirateRecomp.exe`. If Windows warns about an unsigned app, choose
   *More info*, then *Run anyway*.
3. Pick your disc image (`.iso`) or a folder with the disc's files.
4. Wait a few minutes while it builds the game from your disc. This
   happens only once.
5. Press **Play**.

Nothing else needs installing: the zip carries its own compiler and tools.
The game, your saves and `pirates.ini` go in
`%LOCALAPPDATA%\xboxrecomp\builds\5454008F`. A newer release offers to
**Build again** and keeps your saves. If a build fails, attach `build.log`
from that folder to an [issue](https://github.com/sorin-c/PirateRecomp/issues).

### Option B: build it yourself

With Visual Studio, Git and Python. See [docs/building.md](docs/building.md).

## Playing

- **Launcher:** resolution, fullscreen, widescreen, frame rate, frame
  interpolation, texture sharpness, and keyboard and controller bindings.
- **In game:** **F2**, or **Start + Back** on a controller, opens a settings
  menu.
- **Controllers:** Xbox, PlayStation and Switch pads. Controller 1 is
  whichever pad you used last.
- **Display:** the game's own 30 fps at sea and in battles; turn on frame
  interpolation for 60 on screen at the same game speed. Widescreen uses
  the game's own 16:9 mode, and the menus stay 4:3.

### Controls

| Original Xbox pad | Modern pad | Keyboard | In the game |
| --- | --- | --- | --- |
| A | A | Z | Select, confirm |
| B | B | X | Back, cancel |
| X | X | A | |
| Y | Y | S | |
| Left / right trigger | LT / RT, or LB / RB (not while sailing) | E / R | Menu tabs |
| Black / White | LB / RB while sailing | W / Q | Map overlay / sails |
| Start | Start / Menu | Enter | Pause menu (saving is at sea) |
| Back | Back / View | Backspace | |
| D-pad | D-pad | Arrow keys | Move through menus |
| Left stick | Left stick | (none) | Steer the ship, walk |
| Right stick | Right stick | (none) | |
| Start + Back | Start + Back | F2 | Settings menu |

Keyboard only: **F9** shows the frame rate, **F10** changes the frame cap,
**F11** saves a screenshot (`frameNNN.bmp`, with a `frameNNN.d3dcap` frame
capture for bug reports) beside `pirates_recomp.exe`, and **Alt+Enter**
switches fullscreen.

Keys and buttons can be rebound in the launcher's Input tab. The pad
columns are by position, so a PlayStation pad's Cross is A.

Every setting, the controller options and how saves work are in
[docs/settings.md](docs/settings.md).

## The disc

Supported: **Sid Meier's Pirates! (USA, Europe) (En,Fr,De,Es,It)**, title ID
`0x5454008F`, `default.xbe` SHA-1 `19191d2b0364c6ca7ee2a6fc063f3c8e3c843244`.
Other revisions and the Xbox 360 and PC versions are not supported.

## Credits and licensing

Built on [DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp)
by phobos665, based on [xboxrecomp](https://github.com/sp00nznet/xboxrecomp)
by sp00nz, with xemu-derived hardware components. Checked against
[xemu](https://xemu.app/) and [XboxDevWiki](https://xboxdevwiki.net/).
Development included assistance from Claude Code. The game needs 49 toolkit
changes, kept in `patches/` and described in
[docs/toolkit-patches.md](docs/toolkit-patches.md).

The code here is under [`LICENSE`](LICENSE); the toolkit is GPL-3.0.
*Sid Meier's Pirates!* belongs to Firaxis Games and 2K Games. This project is
not affiliated with them and includes nothing from the game. Use only a copy
you own.
