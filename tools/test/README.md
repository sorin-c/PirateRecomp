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

| Script | What it does |
| --- | --- |
| `drive.ps1` | A live session: `start [-Tag t] [-KeepData] [VAR=value ...]`, then `press a dpad_down:300 ...`, `shot [name]`, `wait <s>`, `log [pattern] [n]`, `alive`, `stop`. Presses go through `RECOMP_INPUT_LIVE` (toolkit patch 0003). |
| `run.ps1` | A bounded run with a fixed `RECOMP_INPUT_SEQ` script and timed screenshots. |
| `shot.ps1` | Captures the game window (PrintWindow) to PNG. |
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
