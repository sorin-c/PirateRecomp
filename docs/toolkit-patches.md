# Toolkit patches

`patches/` holds changes to the
[DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp) toolkit
that this game needs and that are not in the pinned `xboxrecomp` commit yet.
They are applied to the submodule's working tree, never committed inside it,
so the submodule still points at an upstream commit. Apply them in number
order, after the submodule init and before the lift, from the repository
root (PowerShell):

```powershell
Get-ChildItem patches\*.patch | Sort-Object Name | ForEach-Object { git -C xboxrecomp apply $_.FullName }
```

Each one, with the reason it exists:

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

- **0017-hle-push-buffer-draws-ff-and-timing** (`src/hle/hle_d3d8_vertex.c`,
  `hle_d3d8.c`). Gamebryo draws each character's head and hands (one skin
  partition of 1,200+ triangles) through `D3DDevice_RunPushBuffer`, a
  prebuilt push buffer, and the rest of the body through
  `DrawIndexedVertices`. Two gaps lost them: the walk drew a push buffer
  only at the next replaced call, with the next object's transforms (the
  head came out huge in front of the camera), and it could not lay out a
  pushed draw under a fixed-function FVF at all (it needs a program
  object), so the head and hands were dropped. `RunPushBuffer` is now
  replaced and brings the walk up to date after its body, and pushed
  fixed-function draws are gathered in the FVF's layout from the registers
  the XDK feeds (v0 position, v1 weights, v2 normal, v3/v4 colours, v9-v12
  texture coordinates). `RECOMP_HLE_D3D8_PB_RUN_SYNC=0` restores the late
  draw. Needs a re-lift (`--from lift`): it adds an `HLE_ORIGINAL`.

- **0018-hle-interp-fixed-function** (`src/hle/hle_d3d8_interp.c`,
  `hle_d3d8_record.c`, `.h`). Frame interpolation blended only draws made
  by vertex programs, through their matrix constants. Pirates! draws its
  characters, the duels, the governor's rooms and the menus with the
  fixed-function pipeline, so those frames were held (no in-betweens) or
  the characters moved at 30 while the scene around them moved at 60. A
  fixed-function draw now keeps its view, projection and world matrices
  (one more world per blend weight), and an in-between frame draws it with
  them blended like a program's: rotation as a quaternion, scale and
  translation linearly, the projection term by term. Screen-space
  (XYZRHW) draws are left as they are.

- **0019-config-title-default-frame-interp** (`src/config/recomp_config.c`,
  `.h`, `src/hle/hle_d3d8_interp.c`). A title can set the frame
  interpolation it starts with (`RECOMP_TITLE_DEFAULT_FRAME_INTERP` on
  `xbox_config`); this game's CMakeLists sets 2. And with `frame_cap = 60`
  (a strict 60: every present waits for a vblank) an in-between frame is
  drawn only between title frames two vblanks or more apart: between two
  a vblank apart it cost the title a vblank, and the menus and sea dialogs
  ran at 40-50 instead of 60.

- **0020-hle-interp-mirror-quaternion** (`src/hle/hle_d3d8_interp.c`).
  The flicker at sea with frame interpolation: every in-between frame lost
  the towns, the ships' sails and the cloud shadows. The sea camera's view
  matrix is a mirror whose proper part turns half a turn, and the
  quaternion taken from it lost the relative sign of its axes at 180
  degrees, so even an unblended rebuild flipped two axes. The quaternion is
  now taken by Shepperd's method. Also: a fixed-function draw's matrices
  are read back from the device at the draw, a world matrix's jump is
  judged in camera space, and only draws of the same bytes are blended.
  `RECOMP_INTERP_FF=0` draws fixed-function draws as the frame has them.

- **0021-d3d-pixel-offset-scaled** (`src/d3d/d3d8_device.c`). At a
  `resolution_scale` above 1, the toolkit's half-pixel screen-space offset
  (half a game pixel, so the console's pixel centres land on integers)
  left the first rows and columns of host pixels outside any full-screen
  quad the game draws from 0: a blue line of the clear colour along the
  top and left of the picture, two pixels wide at 4x. With
  `RECOMP_D3D8_PIXEL_OFFSET=scaled` (this game's CMakeLists sets it through
  `RECOMP_TITLE_PIXEL_OFFSET_SCALED`) the offset is half a host pixel: the
  same at 1x, and the quad covers the edge above it.

- **0022-headless-test-runs** (`src/config/recomp_config.c`,
  `src/hle/hle_d3d8.c`, `src/hle/input_host.c`, `src/input/input_bindings.c`).
  Test scripts ran the game on the same desk as the player: its window came
  up in front, took the focus, and the player's own keyboard and pad (one in
  slot 2 included) reached the game alongside the script. `RECOMP_HEADLESS=1`
  (or `headless = 1` in `pirates.ini`) opens the window off every monitor,
  off the taskbar and Alt+Tab, never active; no real keyboard, pad or mouse
  is read on any port, the game is shown one controller (the script's), and
  nothing rumbles. The window is still shown, not hidden or minimised, so
  frames are drawn, presented and paced exactly as in play (measured: 60 a
  second in the menus, 30 in a VS MODE battle with `frame_interp = 2`, as
  without it). `RECOMP_SHOT_REQUEST=<file>` lets a script ask the game for
  its next frame as a BMP (and a replayable capture, as F11), since nothing
  on the desktop can photograph a window that is off it. Off by default;
  a player's game is unchanged.

- **0023-d3d-clipplane-texture-mode** (`src/d3d/d3d8_combiners.c`, `.h`,
  `d3d8_xbox.h`, `src/hle/hle_d3d8_state.c`). The CLIPPLANE texture mode
  killed nothing, and PSCOMPAREMODE was not forwarded. The world map's
  forests use it: each tree's stage-0 coordinate is its map cell's tree
  density less a half, and the hardware kills the pixels where it is below
  zero, so a forest tile is thinned into natural clumps. Without it every
  tile was drawn whole, as a square block of trees (compared against xemu).
  PSCOMPAREMODE now goes to host render state 255, and a CLIPPLANE stage
  discards a pixel when any of s, t, r, q fails its test, as xemu does.

- **0024-in-game-settings-menu** (`src/hle/hle_settings_menu.c` new,
  `hle_d3d8.c`, `hle_d3d8_interp.c`, `hle_settings.h`, `input_host.c`,
  `src/d3d/d3d8_overlay.c`, `.h`, `src/config/recomp_config.c`, `.h`). F2,
  or Start+Back on a pad, opens a settings panel over the game: frame-rate
  cap, frame interpolation, the frame-rate counter and fullscreen apply at
  once; internal resolution, widescreen, texture filtering, variable
  refresh, movies and sound are written for the next start (marked *).
  Every change is written to `pirates.ini` in place (`recomp_config_set`,
  comments kept). While it is open the game reads no input; Start or Back
  pressed alone is held back up to 150 ms, so the pair never pauses the game
  first, and a short tap still reaches it. The overlay can draw a centred
  panel of several lines. Also: frame interpolation can be changed while
  running (`hle_set_frame_interp`; the title's 30 fps pacing follows it),
  a loose fixed-function match no longer holds a frame back, and
  `RECOMP_SHOT_REQUEST` takes `screen <file>` (the window as presented,
  overlay and menu included; `drive.ps1 screen`).

- **0025-display-crop-and-interval-ceiling** (`src/d3d/d3d8_display.c`,
  `d3d8_xbox.h`, `src/hle/hle_settings_menu.c`,
  `src/kernel/xbox_memory_layout.c`). The display resolve can leave the
  game's last rows off the screen and stretch the rest (`crop_bottom`,
  `RECOMP_CROP_BOTTOM`, guest rows; this game defaults to 8 through
  `RECOMP_TITLE_CROP_BOTTOM` on `xbox_d3d8`): the sea's grey-lavender band
  along the bottom, which the console shows too, is gone (always, since
  0031). And a title that sets
  `RECOMP_TITLE_INTERVAL_CEILING=1` has the player's frame cap read as a
  ceiling over its presentation interval, not an override: with the cap at
  60 or off, sailing presented every vblank or as fast as it could and
  frame interpolation never had a gap to fill (the sea stayed at 30). Now
  the sea presents at 30 and interpolation shows 60, at the same game speed.

- **0026-startup-window-black** (`src/hle/hle_d3d8.c`). The game window is
  black until its first frame (it had no background brush, so a fullscreen
  start showed a white box in the corner while the game loaded), and the
  log says when the window opened and when Swaps 1, 10 and 100 came
  (`[STARTUP] <seconds since the process started>`), to find where a slow
  start goes.

- **0027-pad-start-in-background** (`src/input/recomp_pad.c`). SDL's
  gamepad start-up runs on a thread of its own. It enumerates every input
  device, and on this PC it once took 20.8 s (instant on the next tries, a
  device waking up); the game's first pad poll comes before its first
  frame, so a start waited that long on a black screen. Until SDL is up
  there are no SDL pads, and the pad arrives as an insertion (the keyboard
  works throughout). The log says how long it took: `pads through SDL3
  ..., ready after N s`.

- **0028-crop-left-widescreen** (`src/d3d/d3d8_display.c`, `d3d8_xbox.h`,
  `src/hle/hle_settings_menu.c`). The display resolve can leave the game's
  first columns off too (`crop_left`, `RECOMP_CROP_LEFT`, guest columns).
  In widescreen the main menu's map stops three and a half columns short of
  the left edge, a light blue bar of clear colour; this game defaults to 4
  columns in its 16:9 mode only (`RECOMP_TITLE_CROP_LEFT_WIDE` on
  `xbox_d3d8`); always, since 0031.

- **0029-front-end-4x3-and-pad-hotplug** (`src/d3d/d3d8_display.c`,
  `src/input/recomp_pad.c`). A frame the title calls 4:3
  (`xbox_D3D8SetWideFrames(FALSE)`) from a buffer wider than 4:3 is shown
  as its centre at 4:3, between bars: this game's widescreen front end is
  its 4:3 menu with more background either side, and stretched to 16:9 it
  looked wide (`recomp_manual.c` calls the main menu's frames 4:3 while
  sub_00126BC0 runs). And SDL pads live on a thread of their own, which
  starts SDL, pumps the messages of the hidden windows SDL learns of new
  devices through, and updates and rescans the pads every 2 ms: polled
  from the title's threads, a pad plugged in after start-up was never
  seen.

- **0030-pads-no-directinput-follow-input** (`src/input/recomp_pad.c`).
  SDL's DirectInput back end is off on Windows unless `pad_directinput = 1`
  (`RECOMP_PAD_DIRECTINPUT`): it enumerates every DirectInput device and
  took 12-40 s on this PC every time (0.01 s without it), so a pad came
  long after the game. Xbox pads come through XInput, raw input and WGI,
  PlayStation and Switch pads through HIDAPI. And controller 1 follows the
  pad used last (`pad_follow_input`, `RECOMP_PAD_FOLLOW_INPUT`, on by
  default): a button, a trigger or a stick on any pad makes it controller
  1, the others follow in the order they were found. 0 keeps each pad in
  its own slot, for several players. The title now calls every front-end
  frame 4:3 (legal text, title, menus, the new-career pages) until the game
  world starts (`recomp_manual.c`).

- **0031-bands-always-cropped** (`src/d3d/d3d8_display.c`, `d3d8_xbox.h`,
  `src/hle/hle_settings_menu.c`). A title's crop defaults
  (`RECOMP_TITLE_CROP_BOTTOM`, `RECOMP_TITLE_CROP_LEFT_WIDE`) are fixed: no
  F2 menu item and no settings key turns them off. `crop_bottom` and
  `crop_left` remain for a title without a default.

- **0032-title-display-defaults-and-release-settings**
  (`src/config/recomp_config.c`, `recomp_config.h`, `src/d3d/d3d8_display.c`,
  `d3d8_device.c`, `src/launcher/launcher.c`). For the self-building release,
  where the runtime is built once, before any title, and only `xbox_config`
  is built with the title. The title's display defaults
  (`RECOMP_TITLE_CROP_BOTTOM`, `RECOMP_TITLE_CROP_LEFT_WIDE`,
  `RECOMP_TITLE_PIXEL_OFFSET_SCALED`) move from `xbox_d3d8` to
  `xbox_config`, which the renderer asks (`recomp_title_crop_bottom()` and
  the like); set on `xbox_d3d8`, a prebuilt engine never saw them and the
  title's CMakeLists failed to configure against it. And a release's
  launcher sits in the release folder while the game it starts is built in
  the player's folder: `recomp_settings_set_title_dir` points the title's
  settings file (`pirates.ini`) at the game's folder, so the launcher and
  the game read and write one file.

- **0033-mmio-shared-decoder-fallback** (`src/apu/apu_mmio_hook.c`,
  `src/nv2a/nv2a_mmio_hook.c`). The Windows APU and NV2A fault handlers
  decode the faulting instruction by hand, and knew only the forms MSVC
  emits. A release is built with zig's clang, which reads a volatile
  `if (reg & 0x18)` as `test dword [mem], 0x18` (F7 /0): the release build
  crashed as Miles Sound System started (`[APU] MMIO decode fail ... F7 84`).
  Both now fall back to the shared decoder (`platform/mmio_decode.h`,
  already used on Linux and for AC'97 stores), which covers TEST and group 1
  with an immediate, the ALU forms and the SSE scalar moves.

- **0034-builder-utf8-tool-output** (`tools/builder/src/main.rs`). The
  release's builder read each tool's output with `lines()`, which stops at
  the first line that is not UTF-8. Python writes to a pipe in the system's
  code page on Windows, and the parser prints the disc's title, "Sid Meier's
  Pirates! ™", so the ™ came out as one cp1252 byte: the builder stopped
  reading, the pipe closed, and the parser's next `print` failed with
  `OSError: [Errno 22]`. The first build of the v0.1.0 download failed this
  way on a player's PC. The builder now decodes each line leniently, and
  runs every Python tool with `PYTHONUTF8=1` and `PYTHONIOENCODING=utf-8`.

- **0035-texture-sharpness-off-and-levels** (`src/launcher/launcher.c`,
  `src/config/recomp_config.c`, `src/d3d/d3d8_display.c`, `d3d8_states.c`,
  `src/hle/hle_settings_menu.c`). Texture sharpness (`anisotropy`) offers
  off, 1, 2, 4, 8 and 16 in the launcher and the F2 menu. `0` (the new
  default) leaves the game's own filtering; `1` and up force anisotropic
  filtering at that level where the game filters linearly.

- **0036-texcoord-index-not-above-stage** (`src/d3d/d3d8_shaders.c`). A
  fixed-function stage reads a texture coordinate set no higher than its own
  number; one whose `D3DTSS_TEXCOORDINDEX` names a later set gets its own.
  The game leaves stage 0 at set 1 while it draws the tavern tables (the
  new-career nation choice and town taverns); xemu draws them with set 0
  for both textures, and taken literally the lightmap was read with the
  wrong coordinates and the tables came out as a black-patched mosaic.

- **0038-hud-edge-pinning** (`src/d3d/d3d8_device.c`, `d3d8_xbox.h`). Edge
  pinning (`xbox_D3D8SetEdgeTest`) for a title with its own 16:9 mode whose
  HUD keeps its 4:3 places: in widescreen the title's test (given the draw's
  vertex shader handle, stage 0's texture, the vertices and the matrices)
  moves a draw sideways by the 4:3 layout's margin, to the left or right
  edge. The game's sea HUD uses it (`recomp_manual.c`): the crew count goes
  to the left edge, the food and the compass to the right; the date banner,
  the town labels and the world stay where they are.

- **0039-edge-pinning-offset** (`src/d3d/d3d8_device.c`, `d3d8_xbox.h`). The
  edge test answers how far to move a draw, in the title's pixels, rather
  than which edge: the game moves each HUD group so its gap to the side edge
  equals its gap to the top (the compass: the bottom) on screen.

- **0040-bumpers-as-triggers** (`src/config/recomp_config.c`, `.h`,
  `src/input/input_bindings.c`). A title can set
  `RECOMP_TITLE_BUMPERS_AS_TRIGGERS=1` on `xbox_config`: the default pad
  bindings then give LB the left trigger and RB the right, as LT and RT
  still do, and the Xbox's Black and White keep only their keys (W, Q). This
  game sets it. A bindings file from the launcher overrides the defaults.

- **0041-bumpers-black-white-by-scene** (`src/input/input_bindings.c`,
  `.h`). With the bumpers-as-triggers defaults (0040), LB and RB are bound
  twice, to the triggers and to Black and White, and
  `recomp_bindings_set_bumper_test` lets the title say, at each sample, which
  job they do now. This game makes them Black and White while sailing (the
  map overlay and the sails) and the triggers everywhere else.

- **0042-lifter-guest-hooks** (`tools/recomp/lifter.py`, `__main__.py`,
  `scripts/recompile.py`, `templates/runtime/recomp_types.h`). Guest hooks:
  `config/hooks/<TITLEID>.json` lists guest instruction addresses
  (`{"hooks": [{"address": "0x...", "note": "..."}]}`), the pipeline passes
  it to the lift (`--guest-hooks`), and each listed instruction gets
  `RECOMP_GUEST_HOOK(address)` just before it: a call to the title's
  `recomp_guest_hook()`, which may read and change the guest's registers and
  memory (`ebp` goes through `g_ebp` both ways). A hook that cannot be placed
  (no lifted instruction starts there, or it is the jump of a fused
  `cmp`/`jcc`) stops the lift. This changes what one instruction inside a
  large function sees without replacing the function, and a clean lift
  reproduces it. The game uses it for sailing at 60 (`sea_60`).

- **0043-settings-menu-title-live-row** (`src/hle/hle_settings_menu.c`). A
  title row that applies at once (`LIVE_TITLE`: the title reads the key with
  `recomp_config_lookup` while it runs), and the game's "Sailing at 60 fps
  (experimental)" row (`sea_60`). The label column is as wide as the longest
  label.

- **0044-flip-gate-title-rate** (`src/kernel/xbox_memory_layout.c`, `.h`).
  `xbox_Nv2aFlipGateSetTitleRate(hz)`: a title's own rate for one scene whose
  logic keeps time, over the vblank and over the player's frame cap -- hz > 0
  Swaps a second by the host clock, < 0 no wait (unlocked), 0 back to
  normal. The title renews it every frame; the gate lets it lapse 100 ms
  after the last renewal, so a dialog or another scene is paced as before.
  The game uses it for sailing faster than 60 (`sea_fps`).

- **0045-settings-menu-sailing-rate** (`src/hle/hle_settings_menu.c`). The
  F2 row for the game's sailing rate: "Sailing frame rate (experimental)",
  30 (off), 60, 90, 120, 144, 240, unlocked (`sea_fps`), in place of 0043's
  on/off "Sailing at 60 fps" row.

- **0046-lifter-guest-hook-ebp** (`tools/recomp/translator.py`). A function
  with a guest hook (0042) declares its `ebp` local even when its own code
  never uses ebp: the hook passes ebp through `g_ebp` both ways, and a leaf
  function without it failed to compile ("'ebp': undeclared identifier";
  the first was sub_000C6320, a ship's body written back to the ship).

- **0047-settings-menu-battle-rate** (`src/hle/hle_settings_menu.c`). The F2
  row for the game's battle rate: "Battle frame rate (experimental)", 30
  (off), 60, 90, 120, 144, 240, unlocked (`battle_fps`).

- **0048-settings-menu-town-rate** (`src/hle/hle_settings_menu.c`). The F2
  row "Town frame rate (experimental)": 33 (off), 60, 90, 120, 144, 240,
  unlocked (`town_fps`).

- **0049-texcoord-traded-sets** (`src/d3d/d3d8_shaders.c`). Replaces 0036's
  rule. When a fixed-function stage names a later coordinate set than its
  own, the stages are trading sets, and every stage then reads its own set;
  a stage that names an earlier set still gets it. The game draws its rooms
  (taverns, the governor's, the tables behind the nation choice) with the
  lightmap on stage 0 at set 1 and the stone or wood on stage 1 at set 0,
  and xemu shows each read with its own set. Under 0036 both read set 0: the
  lightmap was read with the stone and wood coordinates, and rooms came out
  darker and blotchier than in xemu (the governor's walls white instead of
  blue panels). Characters keep set 0 shared by stages 1 and 2 behind a
  reflection on stage 0.

- **0050-ack-wake-and-vertex-runs** (`src/kernel/xbox_memory_layout.c`,
  `src/hle/hle_d3d8_vertex.c`, `src/d3d/d3d8_device.c`). Frame time, no
  change in what is drawn or how the game runs. (1) The XDK's GPU waits
  (`CDevice_KickOff`, `D3D_KickOffAndWaitForIdle`, `D3D_BlockOnTime`,
  `D3D_BlockOnResource`, `D3DResource_BlockUntilNotBusy`) spin on something
  the NV2A ack thread answers (the PFB flush bit, DMA_GET, the fence mirror).
  Idle, that thread slept a timer period between passes (about 1 ms, against
  the 100 us asked), and the game thread spun all that time: 4-7% of it at
  sea, in battles and in the tavern. The replaced waits now wake it
  (`xbox_Nv2aAckWake`), and it polls for 200 us; otherwise it behaves as
  before, at the same CPU (`RECOMP_NV2A_ACK_WAKE=0` for the old timing).
  Acknowledging the bit from the game thread itself was tried and stalled
  the game: the ack thread's busy spell after a change is what keeps the
  waits that follow fast. (2) The push-buffer walk converts a draw's vertex
  arrays a whole array at a time for float and D3D-colour attributes
  (`w_convert_run`), instead of a switch per vertex per attribute. (3)
  `RECOMP_FRAME_WORK=1` prints, every five seconds, each frame's own work
  apart from the present (mean, 99th percentile, worst): the measure when the
  display caps the frame rate. Tavern at town_fps unlocked, scale 4,
  widescreen: 8.3 to 7.0 ms a frame (119 to 135 fps); a VS battle 4.9 to
  4.3 ms.

- **0051-ff-vertex-copy-runs** (`src/hle/hle_d3d8.c`). A fixed-function
  draw described by a declaration (`ff_from_declaration`) rebuilds every
  vertex in FVF order (`shadow_expand_vertices`). Inputs already in the form
  they are drawn in (whole floats, a D3DCOLOR drawn as one) are now copied a
  whole input at a time with a fixed size, instead of through
  `ff_read_floats` a vertex at a time; the rest as before. The same bytes.
  The tavern at town_fps unlocked, scale 4: 7.0 to 6.6 ms a frame (about
  137 fps, against the 143 Hz display's ~140).

- **0052-settings-save-whole-or-not** (`src/config/recomp_config.c`,
  `src/config/recomp_config.h`, `src/hle/hle_settings_menu.c`). A change in
  the F2 settings menu rewrote the whole settings file in place (`wb`), and
  ignored what `fwrite` and `fclose` said: a full disk could leave an empty
  or cut file and still report success. The new file is now written to
  `<file>.tmp` beside it, and moved over the old one (`MoveFileExA`,
  `rename` off Windows) only when every byte was written and the file closed
  cleanly; otherwise the old file stays whole. The menu then shows "Could
  not save the settings file: this change lasts until you quit", and the log
  says `NOT SAVED`.

- **0053-texcoord-routed-arrays** (`src/hle/hle_d3d8_state.c`,
  `hle_d3d8_vertex.c`, `src/d3d/d3d8_shaders.c`). Replaces 0049's rule.
  `D3DTSS_TEXCOORDINDEX` is carried out by the XDK, not by the GPU:
  `SetTextureState_TexCoordIndex` points vertex attribute 9 + stage at the
  set the stage names, and the XDK writes the vertex arrays through that
  table. A draw from a vertex buffer is routed once, by the host, as the
  index says; a draw read back from arrays already written into the push
  buffer (a push buffer the title runs, or arrays it pushed itself) has each
  stage's set in its own attribute, so the HLE gives every stage its own
  set there. The game draws its rooms both ways with the lightmap on stage 0
  at set 1: the tavern floor from vertex buffers, the walls, hearth and the
  tables behind the nation choice from prebuilt push buffers. 0049 read the
  floor's lightmap with the tiled stone coordinates (a dark, striped floor,
  the carpet missing); now the tavern, the nation tables and the governor's
  room match xemu.

- **0054-settings-menu-legal-screen** (`src/hle/hle_settings_menu.c`). The
  F2 row "Legal screen": skip, show (`skip_legal`), after "Movies". Applies
  at the next start.

- **0055-game-above-normal-priority** (`src/hle/hle_d3d8.c`, Windows). A played game
  runs at above-normal priority (`RECOMP_PRIORITY`, `priority`, default 1);
  a headless or background test run stays at normal so it never takes the
  PC from the person at it. With a full game build compiling in the
  background, the sea at sea_fps unlocked ran 24-39 fps at normal priority
  and 56-120 fps above normal (140 with nothing else running): the game's
  threads hand work to each other many times a frame, and at normal
  priority each hand-off waited behind the compilers.

- **0056-texcoord-routed-past-fvf-sets** (`src/hle/hle_d3d8_state.c`). Fixes
  a regression from 0053. A draw read back from routed arrays under an FVF is
  laid out with only the FVF's coordinate sets (v9 .. v9 + ntex - 1), so a
  stage past them has no coordinates of its own in the vertex. 0053 sent such
  a stage to its own set anyway, and it read zeros. The game draws its
  characters' heads from prebuilt push buffers under a one-set FVF, with a
  reflection on stage 0 and stages 1 and 2 both naming set 0: hair, beards
  and the governor's wig came out as one smeared texel (the governor's face
  covered by his beard texture). Such a stage now reads the attribute of an
  earlier stage that names the same set; under a declaration nothing changes.

- **0057-third-party-dear-imgui** (`third_party/imgui`, new). Dear ImGui
  1.91.9b (MIT, its `LICENSE.txt` beside it), core files only: no backend,
  no demo. Vendored rather than a submodule because the toolkit's
  submodules are upstream's to choose and the release builds only what a
  patch puts in the tree. Used by 0058 for fonts, text and draw lists.

- **0058-settings-menu-panel** (`src/d3d/d3d8_menu.cpp` new,
  `d3d8_overlay.c/.h`, `src/d3d/CMakeLists.txt`, `src/hle/hle_settings_menu.c`,
  `hle_settings.h`, `hle_d3d8.c`, `src/input/input_bindings.c/.h`,
  `src/config/recomp_config.c/.h`). The F2 menu redrawn as a panel at the
  window's own resolution, through rhi.h (D3D11 and Vulkan alike): tabs
  (Display, Game, Controls, About), each row tagged "Applies now" or "After
  restart" (and "Experimental", "Changed"), dots for where the value sits
  with the default ringed, and the selected row described on the right.
  LB/RB (or the triggers, Black/White; Tab, PageUp/PageDown) switch tabs, Y
  (Delete) puts the default back, Resume and Quit game sit at the bottom,
  and the mouse works while it is open (it is given back from mouse look and
  shown). The game keeps running behind it. New setting `vibration`
  (`RECOMP_VIBRATION`, 0-150 %, default 100; 0-100 % in steps of 10 since
  0066) scales the pad's rumble, live.
  `recomp_config_default(key)` says what a key is when the file does not
  set it. If the panel's renderer cannot start, the old text panel is drawn.

- **0059-builder-log-path-first** (`tools/builder/src/main.rs`). When a step
  of the player's build fails, the launcher shows the builder's message in a
  box of a few lines. The message ended with the whole log's path after 25
  lines of output, so the box cut it off and players could not tell where the
  log was. The path now comes first: `The whole log is in
  %LOCALAPPDATA%\xboxrecomp\builds\<title id>\build.log`, then the last lines.

- **0060-vsync-setting** (`src/d3d/d3d8_device.c`, `d3d8_xbox.h`,
  `src/hle/hle_d3d8.c`, `hle_settings.h`, `hle_settings_menu.c`,
  `src/config/recomp_config.c/.h`, `src/launcher/launcher.c/.h`). New setting
  `vsync` (`RECOMP_VSYNC`, default on), a live row in the F2 Display tab and
  a launcher row; the launcher window is one row taller (862 high) so the
  ninth Video row's help no longer runs into the footer. The game's device always presented with interval 0 (the
  flip gate paces the game, not the present), and in fullscreen the D3D11
  swap chain presents those with ALLOW_TEARING for variable refresh. A scene
  set to 144 or unlocked runs at about the monitor's 144 Hz, so frames that
  came sooner than the monitor's fastest refresh were shown mid-scan: tearing
  even with G-SYNC on. With `vsync` on each present uses interval 1 (D3D11)
  or FIFO (Vulkan, whose swap chain is remade on the switch), so a frame
  waits for the refresh; with variable refresh only the frames faster than
  the display wait. `xbox_D3D8SetVsync` is called only by the HLE device,
  so `d3d8_replay` still never waits.

- **0061-settings-menu-restart-note** (`src/d3d/d3d8_menu.cpp`,
  `d3d8_overlay.h`, `src/hle/hle_settings_menu.c`). The F2 panel no longer
  shows "Applies now" / "After restart" under each row (Sorin's feedback on
  0058): a row with no other tag shows its label centred, and the
  description pane on the right says "Restart the game for this to take
  effect." only for a setting that needs a restart. The "Takes effect" line
  in the pane is gone. Numbered 0061 because the VSync patch (PR #66) takes
  0060; it applies with or without 0060.

- **0062-build-screen-progress** (`src/launcher/` launcher.c, launcher.h,
  launcher_theme*.c/.h, launcher_win32.c, launcher_sdl.c, CMakeLists.txt;
  `tools/builder/src/main.rs`, `tools/recomp/translator.py`,
  `tools/release/make_release.py`). The self-building release's first-run
  screens.
  - **Cancelling.** Neither a pad's B nor Escape cancels a build any more
    (both are the "back" a player presses without thinking, and a cancelled
    build starts over). The CANCEL button (a click) and closing the window
    still do.
  - **Progress.** The bar used to sit at one figure through the whole
    translation. tools.recomp prints its "[n/N] translated" lines when
    `RECOMP_PROGRESS=1` (the builder sets it), the builder reads its stderr
    as well as stdout, CMake's configure moves the bar a line at a time, and
    the stage weights are a measured Pirates build (133 s: files 3.5 s,
    translation 44 s, configure 24 s, compile 62 s).
  - **Steps and time left.** The six steps are shown with each one's time,
    with the time so far and an estimate of the time left (from how fast
    the bar moved since the game's files were ready, eased so it does not
    jump).
  - **The sea look.** A recipe's `"look": "sea"` (release/pirates.json) puts
    the disc, build and done screens in brass on dark sea blue, the F2
    menu's colours: the game's name in a serif (Georgia; the desktop's serif
    on Linux; macOS keeps its system face), a rope trim, a brass bar, the
    steps as ports along a sea route, "Set sail" when it is built, and a
    footer with the version and links (Show log, Open logs folder, and the
    recipe's `"home_url"`). The settings tabs keep the toolkit's look. New
    theme calls: theme_set_look, theme_set_serif, theme_fill(_vgrad),
    theme_disc, theme_diamond, theme_hline, theme_hdash, theme_rope; and
    lhost_open (ShellExecute, SDL_OpenURL).

- **0063-resolution-by-height-native-default** (`src/config/recomp_config.c`,
  `.h`, `src/d3d/d3d8_display.c`, `.h`, `src/launcher/launcher.c`,
  `launcher_sdl.c`, `src/hle/hle_settings_menu.c`, `src/host/host_sdl.c`,
  the config smoke test). The resolution was a whole multiple of the
  console's 640x480, so the launcher offered 1920x1440 and 2560x1920
  where a player looks for 1920x1080 (Sorin, 22 Nov). New setting
  `resolution` (`RECOMP_RESOLUTION`): the picture's height in rows, or
  `native`, the default, the tallest picture of its shape the primary
  display holds (`EnumDisplaySettingsW` on Windows, SDL's desktop mode
  elsewhere). The scene is rendered that tall and as wide as the guest's
  frame is at that height, so the scale need not be whole (1080 is 2.25x);
  every path from guest pixels to scene pixels was already a ratio, and the
  resolve already filtered any ratio. The launcher and the F2 menu offer
  native, 480 (console), 720, 1080, 1440, 2160, 2880 and 4320, named at the
  shape the picture will have (1920x1080 in widescreen, 1440x1080 at 4:3).
  `resolution_scale` is still read from a file without `resolution` (above
  1 it is 480 x it rows; 1, the old default, is native) and from
  `RECOMP_RES_SCALE`. Also `RECOMP_TITLE_DEFAULT_WIDESCREEN`: a title's
  widescreen default, which this game sets to on.

- **0064-settings-menu-no-changed-tag** (`src/hle/hle_settings_menu.c`,
  `src/d3d/d3d8_menu.cpp`, `d3d8_overlay.h`). Sorin's feedback on the F2
  panel: a row no longer gets a "Changed" tag when its value is not the
  default (the ringed pip still marks the default), and the footer no
  longer counts the changes that wait for a restart (nor does the text
  fallback panel); the description pane (0061) already says when a
  setting needs one. The unused `D3D8_MENU_TAG_CHANGED` and the restart
  counter are removed.

- **0065-settings-menu-card-style** (`src/d3d/d3d8_menu.cpp`,
  `d3d8_overlay.h`, `src/hle/hle_settings_menu.c`). Sorin asked for the F2
  panel to look like the menus other recomps use. Filled tabs (the current
  one in brass, no LB/RB pills); a card per setting with the value between
  arrows at the right and a small "default" under it when it is the
  default (no pips); section headings in brass at the label's size; the
  description pane shows only the setting's text, the restart note (0061)
  and any warning; the footer's pad hints are gone and the buttons are
  Continue and Quit game at the bottom right. Same colours, same fonts,
  same input; Resume is renamed Continue in the text fallback too.

- **0066-vibration-ten-percent-steps** (`src/hle/hle_settings_menu.c`,
  `src/input/input_bindings.c`, `input_bindings.h`). Sorin asked for the
  vibration strength to go from 0 to 100 % in steps of 10 (Off, 10%, ...,
  100%) instead of 0-150 % in steps of 25. The value is clamped to 0-100
  and rounded to the nearest 10 wherever it is read or set, so an older
  `pirates.ini` still loads (25 is 30 %, 75 is 80 %, 125 and 150 are
  100 %). A menu item can now hold up to 12 values.

- **0067-settings-menu-hud-layout** (`src/hle/hle_settings_menu.c`). A
  "HUD layout" row in the F2 Display tab, under Widescreen: Anchored to
  edges (`hud_layout = edges`, the default) or Centered 4:3 (`centered`).
  A title row (shown only when the title's settings know the key) that
  applies at once: the game's widescreen HUD test (`pirates_hud_edge` in
  `src/recomp_manual.c`) reads the key itself and, when centered, leaves
  the HUD where the game draws it. Title rows (also sea_fps, battle_fps,
  town_fps) now show the environment-variable tag and warning like every
  other row: the variable wins over the menu for them too, and the menu
  used to hide that. Applies on top of 0066.

- **0068-offscreen-target-scale** (`src/d3d/d3d8_device.c`,
  `d3d8_resources.c`, `d3d8_internal.h`, `d3d8_xbox.h`,
  `src/hle/hle_d3d8.c`, `src/hle/hle_settings_menu.c`). Sorin asked for
  sharper ship reflections at sea. The game draws them into small render
  target textures (256x256 and 512x512) that the sea samples through
  `PSDistortReflections_XB`; only screen-sized targets were drawn at the
  scene's resolution. `xbox_D3D8SetOffscreenScale(w, h, scale)` names a
  target size to draw at 2-8 times its size on the host; the texture keeps
  the title's size everywhere else, and the viewport, scissor and clears
  already scale by the target's host size. A pass into such a target gets
  a stand-in depth of the host size (the host refuses a depth of another
  size), never a title-sized depth texture. `RECOMP_D3D8_RT_SCALE` overrides
  it for tests (`2`, or `4@256x256,512x512`). The F2 Display tab gets a
  "Water reflections" row (Console, Sharp, Sharpest: the game's
  `water_reflection` = 1, 2 or 4, applied at the next start, set in
  `src/main.c`).
  The same patch fixes how a bump source in an unsigned format is
  filtered (`d3d8_combiners.c`): the NV2A makes each texel signed before
  it filters, and the translator filtered first, so neighbours either side
  of 0x7F/0x80 blended into an offset of nearly the whole range. At the
  console's size that hardly showed; drawn larger, the reflection's
  distortion pass broke into blocks (2x) and streaks (4x). The four texels
  are now read at their centres, made signed and blended in the shader.

- **0069-settings-menu-title-rows-by-default** (`src/hle/hle_settings_menu.c`).
  A title's F2 row was shown only when `pirates.ini` already held its key.
  A file last saved before the key existed (an older launcher's save) lacks
  it, so new rows such as Water reflections never appeared and could not be
  set. A row is now shown when the title's settings header gives the key a
  default as well.

- **0070-reflection-clearer-and-mirror** (`src/d3d/d3d8_device.c`,
  `d3d8_combiners.c`, `.h`, `d3d8_internal.h`, `d3d8_xbox.h`,
  `src/hle/hle_settings_menu.c`). Sorin wanted a better reflection, not
  only a sharper one. The game's sea (`PSOceanWaterEMBM`) lerps the
  reflection in at 0.4 under an alpha of 0.6, about 24%, over a target
  cleared to black, so dark parts such as the hull add nothing; the ripple
  pass moves each texel up to 0.06 of the target (BUMPENVMAT -0.06, noise
  tiled 4x). `xbox_D3D8SetOffscreenEffect(w, h, bump_scale, mirror)`: a
  bump-environment stage sampling a render target texture of that size has
  its bump matrix scaled, and, while the draw goes anywhere but such a
  target, the texel any stage samples from it (the sea's is a plain
  projected stage) is laid over the combiners' result with weight
  `mirror` times its green/blue brightness x2 (the black clear adds
  nothing, and neither does the red clear of the ripple target's strip
  that its quad leaves uncovered; x6 drew a dark fringe at the edges;
  `RECOMP_D3D8_MIRROR_GAIN` overrides the 2 for tests), solved for the
  draw's SRCALPHA/INVSRCALPHA blend. The F2 row's values
  become Console, Clearer and Mirror (`water_reflection` 1, `clear`,
  `mirror`; 2 and 4 read as `clear`): both draw the 256x256 targets 4x
  and ripple them a third as much, Mirror at weight 0.6. The 512x512 dialog
  backdrop is no longer scaled.

- **0071-settings-menu-menu-rate** (`src/hle/hle_settings_menu.c`). The F2
  Display tab's "Frame rate by scene" group gets a "Menus" row for the
  game's `menu_fps` (60 (off), 90, 120, 144, 240, Unlocked; applies at
  once): the front end's frame rate, paced in `src/recomp_manual.c`.

- **0072-water-reflection-console-or-improved** (`src/hle/hle_settings_menu.c`).
  Sorin did not like Mirror and asked for two values: the F2 row is now
  Console (`1`) and Improved (`clear`). `mirror`, `2` and `4` from an older
  file show as Improved, which is how the game reads them. The mirror
  composite (0070) stays in the toolkit; the game uses it only when
  `RECOMP_PIRATES_MIRROR_WEIGHT` is set, for tests.

- **0073-settings-menu-duel-rate** (`src/hle/hle_settings_menu.c`). The F2
  Display tab's "Frame rate by scene" group gets a "Sword duels" row for
  the game's `duel_fps` (30 (off), 60, 90, 120, 144, 240, Unlocked; applies
  at once), after "Sea battles": the duel's frame rate, paced in
  `src/recomp_manual.c`.
