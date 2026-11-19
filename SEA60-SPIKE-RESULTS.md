# Real 60 fps at sea: measuring spike results (19 Nov 2026)

Branch `claude/sea60-spike` (worktree `K:\Code\pirategame\sea60`, own build, game copy
`sea60game`, runs in `sea60run`, scratch in `K:\Claude\sea60`). Every run was headless
(tools/test drive.ps1: off-screen, no focus, no real keyboard/pad, muted). Nothing was
merged, no PR, no release. The lifted C and analysis files are not committed.

## Short answer

The sea loop is **not** frame-locked the way it looked. The game already measures its
frame time (0x4C72F8, ms, copied from the engine delta 0x4D39F0 at 0x91DB6) and scales
ship motion, turning, AI ships and its "time acceleration" by it, **but clamps it to
33..100 ms** in three places. At 60 fps the frame time is 16 ms, the clamp turns it back
into 33, and everything runs 2x.

Lowering that floor (an experiment switch, see below) made sailing at 60 fps match 30 fps
for player speed (1.00), turn rate (0.95-0.97) and AI ship speeds (0.96). What is left
is a short, countable list of per-frame constants and frame-count timers.

**Firm estimate for real 60 at sea with game speed identical to 30 fps: 2-3 weeks**
(down from 4-8), most of it testing. Details at the end.

## Method

1. **Sea frame wait** (`RECOMP_PIRATES_SEA_WAIT_MS=N`, default 31 = unchanged; in
   `sub_001A958A` in `src/recomp_manual.c`): with frame interpolation off, at the two sea
   limiter tick reads (return addresses 0x91DE1, 0xA4AEE) the returned tick gets `31-N`
   added, so the game's `>= 31` comparison passes after N ms. Battles, duels and towns
   are untouched. With N = 1 the flip gate's one Swap per vblank paces sailing: measured
   **60.0 frames/s** (30 fps runs: 29.6-31.3).
2. **Logging** (`RECOMP_PIRATES_SEA_LOG=<dir>`), at each sea frame start (0x9190F,
   0xA43FA): `frames.txt` (host ms, game tick, 0x4D67D8, 36 watched dwords: the player
   struct 0x4564A0..0x45651C, 0x49C284, 0x49C28C, 0x49C298, 0x4D65E8); per-dword
   frame-to-frame |delta| sums (int and float) and change counts over all of `.data`
   (0x400400..0x5083C0, 270,320 dwords) every 10 s; a raw `.data` snapshot every second.
3. **Fixed place, fixed input**: the fixture `sea-near-margarita`, loaded headless; the
   ship is put back at open water (130,45) every 5 s (`RECOMP_PIRATES_SEA_PIN`), since a
   free ship reaches land within a minute (it sails ~1.6 map units/s). Two scripts: no
   input for ~150 s ("long"), and 20 s straight / 10 s left / 6 s right / idle ("turns").
   Pairs at 31 ms and 1 ms (60 fps), then the 60 fps run again with the clamp lowered.
4. **Write watch** (`RECOMP_PIRATES_SEA_WWATCH=a,b,..`): the host pages of up to 32
   dwords made read-only during sea frames, a vectored handler records which lifted
   function changed which dword and by how much (named from the PDB). It slows the game to
   ~21 fps, so it was used only to name writers, not for rates.
5. **Clamp experiment** (`RECOMP_PIRATES_SEA_DTMIN=n`): the floor of the three clamps
   made a variable in the lifted C of `sub_00085980` (local edit of `recomp_0033.c`,
   not committed) to see what one fix covers.
6. **Pictures and sound**: 48 in-game frames per run in bursts (16 frames ~125 ms apart)
   at 30, 60 fixed and 60 unfixed; HUD screenshots every 15-20 s; audio capture per
   voice slot (`RECOMP_AUDIO_CAPTURE`) and `RECOMP_DSOUND_PLAY_LOG`.

Runs: r30/r60 (turns), i30/i60 (no input, snapshots, audio), w30/w60 (write watch),
a30/d60 (no input, play log; d60 with the clamp floor at 1), d60t (turns, clamp floor
1), v30/v60/v60u (frame bursts). All in `K:\Claude\sea60\runs`.

## Results

Ratio = per real second at 60 fps / at 30 fps. 1.0 = fine, 2.0 = stepped per frame.
"60+fix" = 60 fps with the clamp floor lowered (`RECOMP_PIRATES_SEA_DTMIN=1`).

| Quantity | Address | 60 | 60+fix | Code site | Per-frame constant | Patch |
| --- | --- | --- | --- | --- | --- | --- |
| Sea frame time used by the game | 0x4C72F8 (ms) | 31 -> 16 | - | written 0x91DBC from engine delta 0x4D39F0 | - | - |
| Time scale ("time acceleration", 100 = normal, up to ~550 in open sea) | 0x40D4C4 | 2.30 | ~1 | 0x8605D-0x860B0: `clamp(dt,33,100)*f/33` | clamp floor 0x21 | trivial |
| Player speed (no input) | 0x4564BC/C0 | 1.64 -> 3.61 u/s, **2.20** | 1.614 vs 1.618, **1.00** | x += sin(hdg)·speed[+0xC8]·[0x40D4C4]·[0x40D4B8]/k at 0x868C9-0x86A04; clamps 0x864D9, 0x8656D | clamp floor 0x21 (x2 sites) | trivial |
| Player turn rate | 0x4564C4 (2^32 = 360°) | 4.17°/frame both: **~2.0** (130 -> 266 °/s) | 1.99°/frame, **0.95-0.97** | same clamps | - | covered |
| AI ship speeds (median over ~70 ships) | ship table 0x4564BC + i·0x45C | 0.68 -> 1.23, **1.81** (slow class 0.39 -> 0.75) | 0.660 vs 0.632, **0.96** | sub_00084E60, uses [0x40D4C4] | - | covered |
| Calendar (date, 400 ticks = event day) | 0x49C284 (+ 0x49C94C per nation) | 135 -> 259 ticks/s, **1.92**; HUD Apr 10 -> Jun 13 vs May 3 -> Sep 11 in 60 s | 115 vs 133, **0.86** | loop at 0x85AAD: `[0x40D4C4]/100` ticks per frame (integer) | truncation | easy: fractional accumulator (1 site) |
| Ship speed approach (acceleration) | 0x4564C8 | ~2 | 1.6-2.0 | 0x86843-0x86891: ±1 + (target-speed)·k per frame | per-frame step | easy (1 player + ~1 AI site) |
| Hull bob / pitch animation | (render side, not located) | cycle 1.9 s -> 1.0 s, **2.0** | **2.0** | not in sub_00085980; candidates: per-frame counters 0x4D671C/0x4D6720 (sub_000C1290), 0x4D3754 (sub_000BA9C0, wraps at 250) | per-frame phase | medium (find + 1 site) |
| Wind direction | 0x40D43C | constant 90° in every run | - | set once 0x7D90C; read 0x86478 as wind - heading | - | none |
| "Wind/sea speed" factors | 0x40D440/444/448 | 2.73 (derived from 0x40D4C4) | ~1 | sub_00089170 0x89262-0x892AC | - | covered |
| Engine clock (animations, waves) | 0x4D3A10 (double, s) | real time (+= delta, capped 0.2 s, 0xC130D) | - | sub_000C1290 | - | none |
| Ocean / wave look, wake, clouds | - | same in frame bursts | same | engine clock | - | none seen |
| Frame counter 0x49C288 (+1/frame) | 0x49C288 | 2.0 | 2.0 | read at 0x780C5-0x78376 (stamps in table 0x4C70F0), 0x7997F, 0x7E7E2, 0x7ED09, 0x84ADA, 0x851F6, 0x8752C; "every 8th frame" at 0x86702 | per frame | medium: ~10 readers to review |
| Frame-count timers | 0x4C72FC (60/20 frames, 0x86CB3/0x86D0F, dec 0x85BC0); ship words +0x4564D6 (0x79433), +0x45650E (0x87240), +0x4564D0 (0x86387); 0x4D6530; 0x4D2AB8-C4 (0xB958D); 0x4AA474/476/51A (0x7E7EB-0x7E8CE) | 2.0 | 2.0 | 17 inc/dec on memory in the 32 sea-logic functions | per frame | medium: each needs a look; maybe half matter |
| Alternate-frame world pass | table 0x4B6446 (sub_000785C0) | 2.0 (by code) | 2.0 | 0x785E7 lifetime in frames, 0x7860E step 100 units per call | per call | easy-medium (1 function) |
| Camera floats | 0x506E18..0x506E58 (sub_000FD2B0) | ~2 changes/s | ~2 | sub_000FD780/FDE20 read engine delta 0x4D39F8 | probably dt-aware | verify only |
| Music, ambience (stream slots) | - | same bytes per second (32.56 vs 32.54 MB) | - | - | - | none |
| One-shot sounds (Play/Stop events) | - | 351 vs 331 in the same 140 s | - | - | - | none |
| HUD (date banner, icons) | - | follows the calendar | - | - | - | covered by calendar |

The broad pass (`.data`, 270k dwords): without the fix ~2,000 dwords changed at about
twice the rate; with the fix the 2x list outside buffers shrinks to the frame counters,
timers and render-side values in the last rows. The large 2x regions that remain
(0x4F5714-0x4F7E28, 0x4B1FA8/0x4B3FA8 x512, 0x4BBE3C stride 0x124) are rebuilt every
frame (render/particle lists) or follow game state; nothing in them showed in pictures.

## Things that looked or sounded wrong at 60 (unfixed)

- Everything sails and turns twice as fast; AI ships too.
- The calendar runs twice as fast: in ~90 s of real time the crew complained "we've
  been sailing over 9 months" (the 30 fps run did not reach it in the same time).
- The ship's bob/pitch cycle halves (also with the clamp fix).
- Sound: nothing measurably wrong; music and ambience streams and the rate of sound
  events are the same.

## Patch sites, counted

| Group | Sites | Difficulty |
| --- | --- | --- |
| Clamp floor 33 ms -> ~8 ms (0x86062, 0x864DE, 0x86572) | 3 (same-length operand patches, `config/xbe_patches`) | trivial; check integer precision at 16 ms |
| Calendar accumulator (0x85AAD loop) | 1 | easy (hook) |
| Speed approach / acceleration (player 0x86843-0x86891, AI equivalent in sub_00084E60) | 2 | easy |
| Hull bob phase | 1 (to locate) | medium |
| Alternate-frame pass sub_000785C0 | 1 function | easy-medium |
| Frame-count timers (17 candidates) | ~8-12 that matter | medium each, low risk |
| Frame-counter 0x49C288 readers (stamps/ages, every-8th-frame) | ~10 | medium; or one global change: count 0x49C288 at 30 Hz |
| Camera (0x506E..), HUD animations | verify | probably none |

About 25-30 sites, most of them small. A shortcut worth trying first for the last two
groups: keep the frame-count logic at 30 Hz (advance 0x49C288 and the countdowns only on
every other 60 fps frame) instead of rescaling each one.

## Effort estimate (sea only, game speed identical to 30 fps)

- Clamp patches + calendar accumulator + acceleration: 2-3 days, with the measuring
  harness from this spike as the test (ratios must come back 1.0).
- Hull bob, sub_000785C0, timers and frame-counter readers: 5-8 days (finding each
  consumer, deciding scale vs 30 Hz gating, re-measuring).
- Transitions and edge cases (ship sighted / attack prompts, storms, entering port,
  fleet ships, the map, the second sea loop copy sub_000A1E10, switching interpolation
  back on for battles): 3-5 days.

**Total: 2-3 weeks.** Battles, duels and towns stay at 30 (unchanged). Risk that would
grow it: the hull-bob/animation side turning out to be many separate Gamebryo controllers
stepped per frame (the frame bursts showed only the hull bob).

## Switches added on the branch (debug, default off)

`RECOMP_PIRATES_SEA_WAIT_MS`, `RECOMP_PIRATES_SEA_LOG`, `RECOMP_PIRATES_SEA_WATCH`,
`RECOMP_PIRATES_SEA_SNAP_MS`, `RECOMP_PIRATES_SEA_PIN`, `RECOMP_PIRATES_SEA_WWATCH`,
`RECOMP_PIRATES_SEA_DTMIN` (only does something with the local edit of the lifted C).
Scripts: `tools/sea60/` (sea.ps1 run driver, stats.py, frames.py, snaps.py, wmap.py).
