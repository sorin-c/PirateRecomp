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
| Resolution (native, console, 720p to 8K) | next start |
| Widescreen (16:9) | next start |
| HUD layout (anchored to edges, centered 4:3) | at once |
| Texture sharpness (off, 1x, 2x, 4x, 8x, 16x) | next start |
| Variable refresh | next start |
| VSync | at once |
| Movies (skip all, skip intro, play all) | next start |
| Legal screen (skip, show) | next start |
| Sound | next start |
| Sailing frame rate (experimental: 30, 60, 90, 120, 144, 240, unlocked) | at once |
| Battle frame rate (experimental: 30, 60, 90, 120, 144, 240, unlocked) | at once |
| Land battle frame rate (experimental: 30, 60, 90, 120, 144, 240, unlocked) | at once |
| Sword duels frame rate (experimental: 30, 60, 90, 120, 144, 240, unlocked) | at once |
| Dancing frame rate (experimental: 60, 90, 120, 144, 240, unlocked) | at once |
| Town frame rate (experimental: 33, 60, 90, 120, 144, 240, unlocked) | at once |

Every change is written to `pirates.ini` straight away, keeping its comments.

## Display and frame rate

- **VSync.** `vsync` (F2 Display tab "VSync", applies at once, on by
  default): each frame waits for the monitor's next refresh before it is
  shown, so no frame is drawn torn across the one before. With variable
  refresh (`vrr`, G-SYNC or FreeSync) on, only a frame that comes faster
  than the monitor's top refresh rate waits, which is the usual way to run
  G-SYNC; that is where the tearing came from, in scenes set to 144 or
  unlocked running at about the 144 Hz refresh. VSync caps the frame rate at
  the refresh rate and never changes the game's speed. `vsync = 0` shows
  frames the moment they are ready, as before.
- **Frame interpolation.** The game runs its world at 30 fps at sea, in
  battles, duels and the town sneak. Frame interpolation (`frame_interp = 2`,
  off by default) draws the frame in between, so you see 60 while the game
  keeps its original speed; menus, towns and the governor's rooms already
  run at 60. Characters, ships and the camera are all interpolated.
- **Sailing frame rate (experimental).** `sea_fps` (F2 "Sailing frame
  rate", applies at once): 30, the default, sails as the console does; 60,
  90, 120, 144 or 240 run the sea itself that many times a second, and 0 is
  unlocked (as fast as the PC draws it; `unlocked` may be written too). A
  value that is not a number, such as a typo, is read as 30. Ships, the wind's effect, turning,
  the calendar, the crew's timers and the hull's motion keep the speed they
  have at 30 (paired test runs agree within a few per cent). At 60 the sea
  is paced by the vblank as the rest of the game is; above 60 by the PC's
  clock, so it needs a monitor that refreshes at least that fast, or
  variable refresh (`vrr`), to show every frame: the picture is presented in
  step with the monitor, so on a 144 Hz monitor 240 and unlocked show about
  140 frames a second. The pause menu opened while sailing (Start: fleet,
  ship, log, the world map) runs at the same rate when it is above 60, and at
  60 otherwise; the world map's stick scrolls it as fast as it does at 60
  whatever the rate. Duels, towns, the town sneak, and the menus elsewhere
  keep their own pacing, and the frame cap (`frame_cap`) still applies to
  them. While sailing faster than 30,
  frame interpolation is not used at sea (battles and duels still use it
  when `frame_interp` is on). `sea_60 = 1`, the setting before this one,
  still sails at 60 while `sea_fps` is 30. Experimental: measured against
  30 fps in scripted test runs ([sea60-testing.md](sea60-testing.md)), not
  yet played through many battles and towns.
- **Battle frame rate (experimental).** `battle_fps` (F2 "Battle frame
  rate", applies at once): the same for ship battles. 30, the default, is
  the console's; 60, 90, 120, 144 or 240 (0 unlocked) run the battle that
  many times a second with the speed it has at 30: the ships' sailing,
  turning and drifting, the cannonballs' flight, the wind, the AI's
  decisions, reloads, fires and other countdowns, and the slow motion
  (10 a second on the console, still 10 a second of game time). The camera
  takes the console's step once every 30th of a second and is drawn a
  share of the way into it each frame, so it glides rather than jolting
  (it trails the console's by at most that 30th of a second). Paced as
  sailing is. Measured against 30 fps in scripted battles (ships within 2%,
  every cannonball's flight time the console's); not yet played through
  many battles. Frame interpolation is not used in a battle faster than 30.
- **Land battle frame rate (experimental).** `land_fps` (F2 Display tab
  "Land battles", applies at once): the same for land battles. 30, the
  default, is the console's; 60, 90, 120, 144 or 240 (0 unlocked) draw the
  battle that many times a second at the speed they have at 30: the men's
  march step, their fighting clock (aim, shot, blow, reload), the camera's
  pan and zoom and a message's time, which the console moves once a frame,
  go by each frame's length (kept to the microsecond); the flags' wave, the
  target highlight's blink, the men's idle fidgets and the pause after a
  battle is won or lost keep the console's 30 a second.
  Paced as sailing is. Measured against 30 fps in scripted battles; not yet
  played through many battles. Frame interpolation is not used in a land
  battle faster than 30.
- **Sword duel frame rate (experimental).** `duel_fps` (F2 "Sword duels",
  applies at once): the same for sword duels. 30, the default, is the
  console's; 60, 90, 120, 144 or 240 (0 unlocked) run the duel that many
  times a second with the pace it has at 30: the duel times its moves by the
  game clock, which faster frames now share out instead of rounding each
  frame up, and what it decides once a frame -- the opponent's next move,
  the crew fight behind the duellists, the effects around them -- it
  decides once every 30th of a second. The camera eases its speed once a
  30th and moves a share of it each frame. Paced as sailing is. Measured
  against 30 fps in scripted duels (the fighters' animation within 1%, the
  duel ending at the same moment); not yet played through many duels.
- **Dancing frame rate (experimental).** `dance_fps` (F2 "Dancing", applies
  at once): the dancing, which the console draws 60 times a second. Its
  steps, prompts and camera follow the music and the game's clock, so 90,
  120, 144, 240 or 0 (unlocked) only let the frames come faster, paced as
  sailing is; the default, 60, is the console's.
- **Town frame rate (experimental).** `town_fps` (F2 "Town frame rate"):
  the tavern, the governor's room and the town's other rooms, which the
  console draws 33 times a second (a 30 ms wait, frame cap or not). Their
  characters and camera already run on the game's real-time clock, so 60,
  90, 120, 144, 240 or 0 (unlocked) only lets the frames come faster, paced
  as sailing and battles are; the default, 30, keeps the console's 33.
  The governor's room also skips the 10 ms pause its dialog loop takes
  every frame, which otherwise held it to about 60 (90 on and off), as do
  the town's dialogs without a room (dividing the plunder). Above 60 the
  town menu and the screens it opens (merchant, shipwright, plunder,
  status) follow it too, instead of the 60 of the vblank, and so does the
  world map tab, its stick scrolling as fast as it does at 60 (as at sea).
- **Menu frame rate (experimental).** `menu_fps` (F2 Display tab "Menus",
  applies at once): the front end -- the title, the main menu, the new
  career pages, loading and saving -- which the console draws 60 times a
  second (a 16 ms pause a frame, then the vblank). 60, the default, keeps
  that; 90, 120, 144, 240 or 0 (unlocked) let the frames come faster, paced
  by the PC's clock as sailing is. Its page changes and animations run on
  the game's real-time clock, so they keep their speed. With `sea_fps`
  above 60 the dialogs that come up at sea (the crew's reports, a town's
  approach, the landing party) follow `sea_fps` too, as the pause menu
  does, instead of 60; their timeouts are the same real time.
- **Widescreen.** `widescreen = 1`, the default, turns on the game's own
  16:9 mode (`0` plays at 4:3). The front end (legal text, title, menus and the new-career pages) stays at 4:3
  between bars, as it was drawn, and the game world fills the screen.
- **HUD layout.** `hud_layout` (F2 Display tab "HUD layout", applies at
  once): `edges`, the default, moves the HUD out to the corners in
  widescreen (at sea the crew and food rings and the compass; in a ship
  battle the two bars with their flags, the ammo ring and the compass).
  `centered` keeps every piece where the console draws it, inside the
  middle 4:3 of the picture, as the console looks in widescreen. Menus,
  dialogs, the date banner and town names are the same either way, and
  at 4:3 the setting does nothing.
- **Resolution.** `resolution` is how tall the picture is rendered:
  `native`, the default, is your display's own (the tallest 16:9 picture
  it holds, so 1440 on a 2560x1440 screen), or `480` (the console's),
  `720`, `1080`, `1440`, `2160` (4K), `2880` or `4320`. The launcher and
  the F2 menu show the sizes at the picture's shape: 1920x1080 in
  widescreen, 1440x1080 at 4:3. A resolution taller than the window is
  supersampled (smoother edges, more work for the graphics card). The
  older `resolution_scale` (2 to 8 times 480 rows) is still read from a
  file without a `resolution` line; its old default, 1, reads as native.
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
| `widescreen` | `1` | The game's own 16:9 mode. |
| `resolution` | `native` | Picture height: `native` (the display's), `480`, `720`, `1080`, `1440`, `2160`, `2880` or `4320`. |
| `hud_layout` | `edges` | Widescreen HUD: `edges` (corners) or `centered` (the console's middle 4:3). |
| `frame_cap` | `adaptive` | `adaptive` (as the console), `60`, `30` or `0` (off). |
| `frame_interp` | `1` | `1` off, `2` shows 60 where the game runs at 30. |
| `sea_fps` | `30` | Experimental: frames a second while sailing (`0` unlocked). |
| `battle_fps` | `30` | Experimental: frames a second in ship battles (`0` unlocked). |
| `land_fps` | `30` | Experimental: frames a second in land battles (`0` unlocked). |
| `duel_fps` | `30` | Experimental: frames a second in sword duels (`0` unlocked). |
| `dance_fps` | `60` | Experimental: frames a second while dancing (`0` unlocked). |
| `town_fps` | `30` | Experimental: frames a second in a town: the menu, its rooms (the tavern, the governor's) and its other screens (`0` unlocked). |
| `menu_fps` | `60` | Experimental: frames a second in the front end: title, main menu, new career, load and save (`0` unlocked). |
| `fps_overlay` | `0` | Frame-rate counter. |
| `fullscreen` | `0` | Fullscreen window. |
| `anisotropy` | `0` | Texture sharpness: `0` off (the game's own filtering), `1`, `2`, `4`, `8` or `16`. |
| `water_reflection` | `1` | The ship's reflection in the sea: `1` as the console (a faint rippled smear), `clear` (F2: Improved) four times larger with gentler ripples. `mirror`, `2` and `4` from older files read as `clear`. Applies at the next start. |
| `vrr` | `0` | Variable refresh rate. |
| `vsync` | `1` | Wait for the monitor's refresh: no tearing, frame rate at most the refresh rate. `0` off. |
| `pad_follow_input` | `1` | Controller 1 is the pad used last. |
| `pad_directinput` | `0` | SDL's DirectInput back end. |
| `skip_intro` | `1` | `1` skips every movie, `intro` only the logos and title movie, `0` plays all. |
| `skip_legal` | `1` | `1` skips the legal screen at boot (the copyright lines, the ESRB notice, the Bink and Miles logos), `0` shows it as the disc does. |
| `sea_map_snap` | `0` | `1` keeps the console's sea map (BLACK at sea): it moves in whole map cells, a jump about twice a second when sailing fast. `0` scrolls it smoothly with the ship, by part of a pixel each frame if needed. |
| `mute` | `0` | No sound. |
| `minigames` | `0` | Shows the developers' MINIGAMES (*DEBUG!*) menu entry. |
| `priority` | `1` | Windows: runs the game above normal priority, so other work on the PC (a build, a download) does not slow it; test runs stay at normal (only `RECOMP_PRIORITY=1` raises one). `0` for normal. |
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
| `RECOMP_PIRATES_SRAND` | (unset) | A number the game's random seed is set to instead of the clock (at start-up and when a land battle begins), so a scripted run repeats: the same start location and the same draws. |
| `RECOMP_PIRATES_LAND_LOG` | (unset) | A file a land battle writes a line a frame to (time, tick, frame length, counters, the first man of each side's first four units), for comparing frame rates. |

The toolkit's own variables are listed in `xboxrecomp/CLAUDE.md`.

</details>

## Saves

The game keeps its saves on the Xbox's E: drive, which maps to the game
folder: in your build they are written to `game\UDATA`. The save folder also
holds the emulated console's identity (`console.ini`), which the game signs
settings and saves with. Deleting it or pointing `RECOMP_SAVE_DIR` somewhere
new makes existing saves read as damaged, so keep one save folder per player.
