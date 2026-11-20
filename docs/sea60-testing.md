# Sailing faster than 30: how it was checked, and how to check it again

`sea_fps` (docs/settings.md) runs the sea's game logic 60, 90, 120, 144, 240
times a second or unlocked, at the speed it has at 30 (`sea_60 = 1`, the
setting before it, is 60). How it works is in the comment above
`sea60_begin_frame` in `src/recomp_manual.c`; the hooked instructions are listed
in `config/hooks/5454008F.json` (toolkit patch 0042), and the build checks the
list against `recomp_guest_hook` (`tools/check_guest_hooks.py`).

Any change to it, or a new hook, should be checked the way it was first: a run
at 30 and a run at 60 from the same save, with the same input, compared per
second of real time. Everything below runs headless (`tools/test/drive.ps1`).

## What you need

- A build, and a copy of the game folder (as `tools/test`, `PIRATES_TEST_GAME`).
- A save made at sea: its `UDATA` folder, in `SEA60_FIXTURE`. The first save in
  the game's list is the one loaded.
- `SEA60_RUNS`: where the runs' folders go (else `%TEMP%\sea60`).
- Python 3 with Pillow for `bob.py`.

## The paired run

```
.\tools\sea60\sea.ps1 -Tag N30 -Interp 2 -Script accept -NoShips -Pin "130 45 5000 90 0" -Poke "4C72FC=150@60000" RECOMP_DSOUND_PLAY_LOG=1
.\tools\sea60\sea.ps1 -Tag N60 -Rate 60  -Script accept -NoShips -Pin "130 45 5000 90 0" -Poke "4C72FC=150@60000" RECOMP_DSOUND_PLAY_LOG=1
py -3 tools\sea60\accept.py $env:SEA60_RUNS\N30 $env:SEA60_RUNS\N60
```

- `-Rate N` is the rate under test: 60, 90, 120, 144, 240, or 0 for
  unlocked (run each against the same 30 fps baseline).
- `-Interp 2` holds the 30 fps baseline at exactly 30.0 frames a second (the
  recomp's 30 fps sea without it runs about 31).
- `-NoShips` empties the AI ship slots every frame and `-Pin` puts our ship
  back in open water (map 130,45, heading 90, speed 0) every 5 s. AI ships use
  random numbers every frame, so with them the two runs' worlds drift apart
  and the time acceleration (which drops near other ships) with them.
- `-Poke` starts a 150-frame countdown at 60 s: it must take 5.0 s in both.
- The script sails 30 s straight, turns left 25 s, right 25 s, then 30 s
  with no input. `accept.py` prints each quantity per real second and the
  ratio, and marks anything outside +-3%.

What to expect (20 Nov 2026): player speed, turning, acceleration, the
calendar, the time acceleration, the frame counter (30/s in both), sound
events and the countdown within about 3%. The phase after a turn and the idle
phase scatter as much between two 30 fps runs as between 30 and 60 (a
30-vs-30 pair showed 1.06-1.4 there), so judge those against a 30-vs-30 pair.

With AI ships (drop `-NoShips`), `accept.py` also gives their median speed;
compare it per ship kind, since each run spawns a different mix.

## The long run (date drift)

```
.\tools\sea60\sea.ps1 -Tag D30 -Interp 2 -Script drift -NoShips -Pin "130 45 5000 90 0"
.\tools\sea60\sea.ps1 -Tag D60 -Rate 60  -Script drift -NoShips -Pin "130 45 5000 90 0"
py -3 tools\sea60\drift.py $env:SEA60_RUNS\D30 $env:SEA60_RUNS\D60
```

Eleven minutes each, pressing A every 30 s for the crew's dialogs. The
calendar after each minute of sailing should agree (89608 against 89611 ticks
after ten minutes), and the dialogs ("months at sea") come at the same sailing
second in both.

## Other checks

- **The hull's bob**: `-Script burst` takes 3 x 32 in-game frames; `bob.py`
  fits the swing of the sails near the screen centre (period 2.0 s in both).
  Use `-NoShips` and a pin without heading, so nothing else is in the crop.
- **Saves both ways**: `-Script keys -SailFirst 40 -Keys "start,lt:200,dpad_down:200,a,a,wait:10,b,b" -Save <dir>`
  saves at sea and copies the save folder; load it at the other rate (keep
  only the new save in the folder, the menu lists by name) and compare with
  `savecheck.py` (calendar, position and speed as saved).
- **Changing the rate while sailing**: `-Flip "30000=120,70000=30"` sets
  `sea_fps` at those times as the F2 menu does; `timeline.py` shows the frames, the calendar
  and the time acceleration per 5 s (60 then 30, no jump in the date).

## The harness

`src/sea60_test.c`, nothing unless `RECOMP_PIRATES_SEA60_TEST` names a folder:
`frames.txt` (one line a sea frame: our ship, the calendar, the frame counter,
the countdowns, the time acceleration, sea_60's tick), `snaps.bin` (the ship
table once a second) and a clock-sync line in the log for the sound log's
times. The pin, poke, hold, no-ships and flip switches are described at the
top of that file.

## Ship battles (battle_fps)

`tools/battle60/battle.ps1` starts a VS MODE battle (no save needed),
headless, holds our ship in a turn from the start so it circles in open
water, presses each fire button in turn, and has the harness log every
battle frame (`RECOMP_PIRATES_BTL_TEST`: `btl_frames.bin`, both ships'
structs, the shot slots and the battle's counters; `btl_snaps.bin`, the
battle object once a second, or the game's whole data with
`RECOMP_PIRATES_BTL_WIDE=1`). `tools/battle60/accept.py` compares runs:

```
battle.ps1 -Tag b30 -Rate 30 RECOMP_FRAME_INTERP=2 "RECOMP_PIRATES_BTL_PIN_ENEMY=2304 -2296"
battle.ps1 -Tag b60 -Rate 60 "RECOMP_PIRATES_BTL_PIN_ENEMY=2304 -2296"
py -3 tools/battle60/accept.py $env:TEMPattle6030 $env:TEMPattle6060
```

With the enemy pinned, our ship's mean speed and turn rate, its path and
its turning per second agreed within 2% at 60, 120 and unlocked (two runs at
30 differ by about as much). With the enemy free, every cannonball flew for
its frame count at 30 fps (1.67 s for 51.5 frames at 30, 3.38 s for 102.8 at
60) at 240,000-244,000 units a second at every rate. Slow motion
(`RECOMP_PIRATES_BTL_OR=49C298=20` holds it on): our ship 35,900 units a
second at 30 (drawn at 9.9 fps) and 35,800 at 60. The enemy's AI is random:
two runs at 30 differ by more than 30 and 60 do, so compare it over several
runs. `RECOMP_PIRATES_BTL_PIN_OURS` holds our ship instead.
