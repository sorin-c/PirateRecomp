# Settings, display and controllers

## The launcher

`PirateRecomp.exe` in the download (`pirates_launcher.exe` in a build of
your own) is the toolkit's launcher, set up for this game.

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
| Texture sharpness (off, 1x, 2x, 4x, 8x, 16x) | next start |
| Variable refresh | next start |
| Movies (skip all, skip intro, play all) | next start |
| Sound | next start |

Every change is written to `pirates.ini` straight away, keeping its comments.

## Display and frame rate

- **Frame interpolation.** The game runs its world at 30 fps at sea, in
  battles, duels and the town sneak. Frame interpolation (`frame_interp = 2`,
  off by default) draws the frame in between, so you see 60 while the game
  keeps its original speed; menus, towns and the governor's rooms already
  run at 60. Characters, ships and the camera are all interpolated.
- **Widescreen.** `widescreen = 1` turns on the game's own 16:9 mode. The
  legal text, title and new-career pages stay at 4:3 between bars, as they
  were drawn. The main menu fills the screen with its map zoomed, the menu
  and the swordfighter where the 4:3 menu has them; the Load Game screen,
  the loading screens and the game world are 16:9.
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

## `pirates.ini`

`pirates.ini` lives beside the game: in `%LOCALAPPDATA%\xboxrecomp\builds\5454008F` for the download, `build\Release` for a build of your own.
Either program writes it with the defaults and a comment on every line the
first time it runs. Edit it in any text editor, or delete it to go back to the
defaults. Every line names the environment variable that overrides it, so a
`.bat` file or a command line still wins.

| Key | Default | What it does |
| --- | --- | --- |
| `widescreen` | `0` | The game's own 16:9 mode. |
| `resolution_scale` | `1` | Render 1 to 8 times the console's resolution. |
| `frame_cap` | `adaptive` | `adaptive` (as the console), `60`, `30` or `0` (off). |
| `frame_interp` | `1` | `1` off, `2` shows 60 where the game runs at 30. |
| `fps_overlay` | `0` | Frame-rate counter. |
| `fullscreen` | `0` | Fullscreen window. |
| `anisotropy` | `0` | Texture sharpness: `0` off (the game's own filtering), `1`, `2`, `4`, `8` or `16`. |
| `vrr` | `0` | Variable refresh rate. |
| `pad_follow_input` | `1` | Controller 1 is the pad used last. |
| `pad_directinput` | `0` | SDL's DirectInput back end. |
| `skip_intro` | `1` | `1` skips every movie, `intro` only the logos and title movie, `0` plays all. |
| `mute` | `0` | No sound. |
| `minigames` | `0` | Shows the developers' MINIGAMES (*DEBUG!*) menu entry. |
| `headless` | `0` | Test runs only (see [building.md](building.md#testing)). |

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
