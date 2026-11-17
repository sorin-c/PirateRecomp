# Test scripts

Windows PowerShell helpers used to bring the game up. They run the built
`build\Release\pirates_recomp.exe` muted against a test copy of the game
folder, so a run never touches `game\UDATA` or your real saves.

- **Work folder:** `..\testrun` beside the repository (override with
  `PIRATES_TEST_OUT`). Logs, screenshots and per-run save folders go there.
- **Game folder:** `..\testgame` beside the repository (override with
  `PIRATES_TEST_GAME`): a copy of `game\` (hard links are fine) without
  `UDATA`, `TDATA` or `CACHE`. Those three are cleared before each run
  unless `-KeepData` is given, because the runtime maps the Xbox's E:, T:
  and Z: drives into the game folder.
- **Headless.** Runs never appear on the desk or take input from it:
  `drive.ps1` and `run.ps1` set `RECOMP_HEADLESS=1` (window off every
  monitor, off the taskbar, never active; no real keyboard, pad or mouse read
  on any port, one controller shown to the game) and
  `RECOMP_WINDOW_BACKGROUND=1`. Presses come only from `RECOMP_INPUT_LIVE` /
  `RECOMP_INPUT_SEQ`. `-Visible` puts the window back on the desktop, behind
  everything and still never active, and lets real devices in again on the
  ports the script does not own.
- **Pictures** come from the game (`RECOMP_SHOT_REQUEST`): `shot.ps1 -Request
  <file>` asks for the next frame and converts it to PNG, and falls back to
  PrintWindow when the game does not answer (an older build, a suspended
  process). `drive.ps1 capture` also writes a replayable `.d3dcap`, as F11.
- **Another build:** `PIRATES_TEST_EXE` (both scripts), so a test never
  rebuilds or runs the player's own `build\Release`.

| Script | What it does |
| --- | --- |
| `drive.ps1` | A live session: `start [-Tag t] [-KeepData] [-Visible] [VAR=value ...]`, then `press a dpad_down:300 ...`, `shot [name]`, `capture [name]`, `wait <s>`, `log [pattern] [n]`, `alive`, `stop`. Presses go through `RECOMP_INPUT_LIVE` (toolkit patch 0003). |
| `run.ps1` | A bounded run (headless unless `-Visible`) with a fixed `RECOMP_INPUT_SEQ` script and timed screenshots; log to `runs\<tag>.log`. |
| `shot.ps1` | The game's next frame to PNG: from the game with `-Request` (headless runs), else the window through PrintWindow. |
| `montage.ps1`, `tile.ps1` | Contact sheets of a run's screenshots. |
| `pcmstat.py` | Loudness per 5 s of `RECOMP_AUDIO_CAPTURE` output, to check sound in a muted run. |
| `capdraw.py`, `capstate.py` | One draw of a `.d3dcap` frame capture: its vertices decoded by the declaration and the vertex-program constants, or every render and stage state in force. |
| `capbetween.py` | Every chunk a `.d3dcap` capture recorded between two draws, states named: what changed state between them. |
| `xpu.py` | Decode the game's compiled pixel shaders (`.xpu`): texture modes, combiner count. |
| `governor.ps1` | Drives a new career (English, default name) into the start town's governor's welcome, for the governor-scene freeze. |
| `duel.ps1`, `townsneak.ps1` | Start the garden sword duel or the town sneak from the debug MINIGAMES menu (`RECOMP_PIRATES_MINIGAMES=1`). |
| `vs-battle.ps1` | Drives a fresh start into a VS MODE ship battle. |
| `xdis.py`, `findimm.py`, `secxrefs.py` | Disassemble a guest address, find code using a constant, count code references into a section (capstone). |

A typical session:

```
.\drive.ps1 start -Tag ng
.\drive.ps1 wait 36; .\drive.ps1 press start; .\drive.ps1 wait 4; .\drive.ps1 press a
.\drive.ps1 shot signup
.\drive.ps1 stop
```
