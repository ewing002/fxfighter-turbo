# FX Fighter Turbo – open port (C11 + SDL2 + OpenGL)

A native, cross-platform rewrite of the 1996 Windows fighting game **FX Fighter Turbo**. It is new C code that reads the data of the
**original game** (`FIGHT.PAC`). It does not run or emulate the original executable.
It builds on Windows 10/11, Linux and macOS.

**The game data is not included.** The repository contains no original game content and no binaries. You need your own copy of
FX Fighter Turbo; the program reads its files at run time.

## Requirements
Your own copy of the original game is needed. Put the files below in the folder you run the game from (or next to the executable):

- `FIGHT.PAC` – **required**. The game's data archive, from your copy of the original game. The program looks for it in this order:
  1. the path given as the first command-line argument (`fxfighter path\to\FIGHT.PAC`);
  2. `FIGHT.PAC`, `../FIGHT.PAC`, `../../FIGHT.PAC`, `fight.pac` relative to the current working directory;
  3. `FIGHT.PAC`, `../FIGHT.PAC`, `../../FIGHT.PAC` relative to the folder of the executable.

  On Linux and macOS file names are case-sensitive, so use exactly these spellings.
- `FIGHT.EXE` – **required for the menus**, and it must be in the same folder as `FIGHT.PAC`. The original 3D menu scripts are read from its
  `.data` section at run time and executed by the port's script VM. Without it the program shows an error and exits.
  A fight without menus works without it: `fxfighter FIGHT.PAC --p1 N --p2 N`.
- `music/trackNN.mp3` – optional. CD audio tracks `track02.mp3` … `track14.mp3`. Without them the game runs silently (sound effects still play).
  Make them from your CD image (BIN + CUE) with `tools/rip_cd.py`, which needs Python 3 and `ffmpeg` (with `libmp3lame`) on the `PATH`
  (see [Music](#music)). The program uses the first of these folders that contains `track03.mp3`:
  `music/` next to the executable, `../music/` relative to the executable, `port/music/` and `music/` next to `FIGHT.PAC`,
  `music/` and `../music/` relative to the working directory.
  Track meaning: 2 = credits, 3–13 = the arenas (arena number + 2), 14 = attract (demo) mode.
- `video/*` – optional full-motion video (intro, logos, the videos at the end of the ladder and on game over, the credits clips).
  Copy them from the CD with `tools/rip_video.py` (Python 3 only; it writes `*.brp`, `*.clt` and `*.pal` files, see [Videos](#videos)).
  The program looks for each video in: `video/` next to the executable, `../video/` relative to the executable, `port/video/` and `video/` next to
  `FIGHT.PAC`, and `FIGHT/VIDEO/` next to `FIGHT.PAC` (the installed game's own folder). A video that is not found is skipped and the game goes on.
- Settings and high scores are written to the per-user folder returned by SDL: on Windows `%APPDATA%\FXFighter\Turbo\`
  (`options.cfg`, `hiscores.dat`); on Linux and macOS the SDL equivalent (normally `~/.local/share/FXFighter/Turbo/` and
  `~/Library/Application Support/FXFighter/Turbo/`). The environment variable `FX_PREFDIR=<folder>` redirects both files (used by the tests).

Example layout of a working folder (Windows):
```
FXFighter/
  fxfighter.exe
  SDL2.dll
  FIGHT.PAC         (from the original game)
  FIGHT.EXE         (from the original game)
  music/            (optional)
    track02.mp3 … track14.mp3
  video/            (optional)
    *.brp, *.clt, *.pal
```

**Windows builds.** The repository contains no binaries. The GitHub Actions workflow `.github/workflows/build.yml` builds the Windows version;
its artifact (or the release zip) contains `fxfighter.exe` and `SDL2.dll`. Unpack them next to your game files as shown above. You can also
[build it yourself](#building).

## Status
### Milestone 3 – complete gameplay with menus
- Reading of the `FIGHT.PAC` archive, RLE textures, palettes, BRender models, materials and pixmaps
- All 12 arenas (floor + 360° background) and the textured 3D fighter models
- **Bone animation**: the format and the bone matrix formula were reverse-engineered from the EXE; every clip can be played
- **Move system**: the original `moves/*.hdr` / `.ste` state machine, recovered from the EXE (punches, kicks, combos, block, duck, jump, roll)
- **Fighting**: hit detection by body part, reaction tables, damage, KO, rounds, timer, health bars
- **3D movement**: rotating fight line (sidestepping, swinging punches), jump, dash, real ring size, **ring-out** (a fall to the arena's lower level),
  feet do not sink into the ground
- **Camera from the EXE**: low view that depends on the distance between the fighters, 45° field of view, camera presets tied to moves
  (throws, victory); the background scrolls like the original
- **Music**: the CD audio tracks as MP3 (`music/trackNN.mp3`), one original track per arena (arena + 2)
- **Sound effects**: the original `.asf` samples (bit-exact DPCM decoder), fighter, arena and announcer banks (`.fxl`), sounds tied to moves
- **Menus and HUD with the original graphics**: loading screen, main menu, character select with portraits, arena select (2 players),
  VS screen with announcer, ROUND / FIGHT! / KO / RING OUT! / TIME OVER / WINNER / YOU WIN / YOU LOSE / GAME OVER captions,
  original health bar, timer and name tags
- **Matches**: first to win 2 of 3 rounds. In 1-player mode the other 10 fighters follow in order, each in his own arena. A defeat means GAME OVER.
- **Computer opponent**: the original AI, reverse-engineered, with 8 difficulty levels
- **High-score table**: at the end of the ladder the original name-entry screen and table, saved in `hiscores.dat`
- **Videos (FMV)**: the original intro and logos, the end-of-ladder video and the game-over video, from the CD (see below)
- **Credits**: after the WIN video at the end of the ladder, the clips of the crew with the announced names, then the scrolling name list

## Running
Start `fxfighter.exe` from the folder that contains `FIGHT.PAC` and `FIGHT.EXE` (see [Requirements](#requirements)), or pass the path of
`FIGHT.PAC` as the first argument.

**Menus:** the original 3D menus run: the port reads the menu scripts from your `FIGHT.EXE` (next to `FIGHT.PAC`) and executes them the
original way. Player 1 moves with W/A/S/D and selects with F (or Enter, Space); G = kick.
Player 2 moves with the arrow keys and selects with K; L = kick. Back and quit: Esc (or Backspace). In the OPTIONS menu the SAVE button
saves the settings and LOAD restores them. Round time, number of rounds, difficulty and volumes take effect even if you do not save them.
The keys can be changed under OPTIONS → CONTROLS → REDEFINE KEYS (ATTACK = forward, RETREAT = back, seen from the starting side);
they apply in the menus and in fights, and SAVE stores them. The table below shows the default layout.

After a lost 1-player match and after every 2-player match the original CONTINUE screen appears (10 s): punch or kick = continue
(1P: a new fighter, the ladder goes on where it was; 2P: fighter select), any other button speeds it up, on timeout you return to the main menu.
With `quick_continue=1` (OPTIONS → QUICK CONTINUE) a punch starts an immediate rematch.

Hidden fighter: in fighter select, stand on Magnon, hold the kick button and press punch: Jerzan appears, the unfinished 12th fighter of the
original game (the port fills in his missing body from Magnon's). Works in 1-player and 2-player mode.

Challenger: during a 1-player ladder, the punch button of player 2 (or the Start button of the second controller) interrupts the match
and opens the 2-player fighter select (a port extension; `challenger=0` turns it off).

ENDURANCE: two teams of up to 4 fighters; the loser of a round brings in his next fighter, the winner keeps his health, no time limit.

TOURNAMENT: 4 or 8 entrants (human or computer), every place is played off; computer matches can be skipped with any button (random winner).

The original sprite effects (`fight/sfx/*.slb`) are shown too: block sparks, blood, the flames and sparks of the special moves, splashes
in the water arena, the teleport at the start of a match. Network play was deliberately left out.

During a fight F1–F4 toggle the two fighters' textures and shading, F5 the floor texture (off / on / perspective), F6 the background.
The floor detail (`detail6` in `options.cfg`, 0–2) takes effect when an arena is loaded; at 0, as in the original, there is no floor model
and therefore no ring-out.

**Gamepad:** Xbox, PlayStation and other SDL-compatible controllers; they can be connected during a game. The first controller belongs to player 1,
the second to player 2.
In a fight: d-pad or left stick = move, X / Y / LB / LT = punch, A / B / RB / RT = kick, Start = pause, Back/Select = back to the main menu.
In the menus: d-pad or left stick = move, A / X / Start = select, Y = kick, B / Back = back. The controller rumbles when the fighter is hurt
(can be switched off).

**Settings:** round time, number of rounds, difficulty, game speed (48 / 60 / 66.7 frames/s; the round clock runs in real time), handicap,
volumes and keys are set in the OPTIONS part of the 3D menu.
Everything else is only in `options.cfg`:
- **Resolution:** `window_w` / `window_h` (size of the window, default 1280 x 960) and `fullscreen_w` / `fullscreen_h` (fullscreen resolution;
  `0` / `0` = borderless fullscreen at the desktop resolution, the default; any other pair switches the display to the closest supported mode).
  **F9** steps to the next resolution of the current mode (window sizes that fit the screen when windowed, the display's modes when in fullscreen),
  **Shift+F9** to the previous one; the new value is saved at once and shown in the window title and the console. A window size set by dragging the
  window edge is saved as well. `--size WxH` on the command line overrides the window size for one run.
- `fullscreen` (0/1; also F11 or Alt+Enter), `vsync`, `ringouts` (ring-out), `rumble` (controller vibration),
  `round_time=99` (a round time that the original does not have).
- `ring_guard` (default 1): a human player cannot step out of the ring by his own movement (step, attack, jump); only a punch, push or throw can
  take him out. In 2-player mode the opponent's body cannot push him out either. 0 = the original behavior, where you can walk past the edge.
- `input_buffer` (default 4, 0–12 ticks): a punch or kick press stays valid for this many ticks until a move uses it, so a press that falls
  at the very end of a move is not lost (in the original it was). 0 = the original behavior. It affects human players only.
- `challenger` (default 1): the challenger can join a running 1-player match (see above); 0 = off.

The settings are saved by the program (on Windows: `%APPDATA%\FXFighter\Turbo\options.cfg`); the 3D menus use the same file.
The high-score table is in the same folder (`hiscores.dat`).

**Fight** (default keys)

| | Player 1 | Player 2 |
|---|---|---|
| left / right | A / D | ← / → |
| jump / duck | W / S | ↑ / ↓ |
| punch / kick | F / G | K / L |

Player 2 is controlled by the computer by default. Some moves: hold back + punch = block, down + kick = roll, hold forward = run.

The fight reads the buttons once per tick (48/s); a short press that falls between two reads is remembered, so even a quick tap is not lost
(keyboard and controller). In 1-player mode player 2 is the computer. During a fight, as in the original: F1 / F2 = player 1's / player 2's
texture on/off, F3 / F4 = their shading (Gouraud / flat), F5 = floor detail, F6 = floor texture (these two are only stored).

During a fight: mouse drag = rotate the camera (the camera follows the fight line by itself), wheel = zoom, B = background,
F11 = fullscreen, **Esc = back to the main menu**. Any button interrupts the demo fight (it starts after idling).
Fixed camera views, as in the original: **Home** = on/off, **Insert / Delete** = next / previous of the 8 views, **PgUp / PgDn** = closer / farther,
arrow keys (when player 2 is the computer) = rotate and tilt; Shift gives finer steps.

For testing, the command-line fight (`--p1 N --p2 N`) has the older hotkeys:
**F3**: P2 human/computer, **F4**: P1 computer (demo), **F5**: new round, **P**: pause, **R**: ring-out on/off, **M**: music on/off,
**X**: sound effects on/off, **− / =**: volume, **1 / 2**: change P1 fighter, **9 / 0**: change P2 fighter, **N**: next arena
(F1 = viewer). These hotkeys are inactive when a human player's fight button uses the key.

**Viewer mode (F1)**: ← / → animation, ↑ / ↓ and PgUp / PgDn fighter, Space pause, , / . single step.

**Command-line options:** `--arena N --p1 N --p2 N --2p --demo --viewer --fight` (these skip the menu and start an endless match),
`--size WxH` (window size, also for `--screenshot`), and `--clip N --time S --yaw R --ticks N --screenshot out.ppm --screen fe3d`.

## Videos
The original videos (intro, logos, the end-of-ladder and game-over scenes, the credits clips) are on the CD. `tools/rip_video.py` copies them
out of the CD image (BIN, MODE1/2352; it reads the ISO 9660 data track and needs only Python 3):
```
python3 tools/rip_video.py "FX Fighter Turbo.bin" video
```
The files are written in lower case (`*.brp`, `*.clt`, `*.pal`). Put the `video` folder in one of the places listed under
[Requirements](#requirements). If a video is missing it is skipped. Esc skips the whole sequence, any other button only the current video.

## Music
The game originally played CD audio tracks. `tools/rip_cd.py` makes MP3 files (192 kbit/s) from the CD image (BIN + CUE). It needs Python 3 and
`ffmpeg` with `libmp3lame` on the `PATH`:
```
python3 tools/rip_cd.py "FX Fighter Turbo.cue" "FX Fighter Turbo.bin" music
```
It writes `trackNN.mp3` for every audio track. The folders searched are listed under [Requirements](#requirements). Without music the game runs silently.
Tracks: 2 = credits, 3–13 = the arenas (arena + 2), 14 = attract mode.

## Building
CMake builds the game (`fxfighter`) and the headless test tools (`fightsim`, `fecheck`, `fevm`, `brpcheck`).

**Linux/macOS:** install the SDL2 development package (`libsdl2-dev`, or `brew install sdl2`), then:
```
cmake -S . -B build && cmake --build build
```

**Windows (MSYS2 MinGW64):** in an MSYS2 MinGW64 shell:
```
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-make
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```
Copy `SDL2.dll` (from `mingw64/bin`) next to `build/fxfighter.exe`. MSVC with vcpkg (`vcpkg install sdl2`) and the same CMake commands also works.
The sources of `fxfighter` are `src/main.c`, `assets.c`, `fight.c`, `audio.c`, `ui.c`, `fe3d.c`, `fe3d_gl.c`, `fe3d_vm.c`, `video.c` and `sprfx.c`;
OpenGL and SDL2 are the only libraries.

The CI workflow `.github/workflows/build.yml` produces the Windows build (`fxfighter.exe` + `SDL2.dll`) as an artifact and release zip.

## Source layout
- `src/assets.[ch]`: loaders for all the formats
- `src/fight.[ch]`: the fight engine (move state machine, input, hits, rounds)
- `src/tables.h`: the EXE's static tables (body part groups, reaction tables)
- `src/main.c`: rendering, camera, input, HUD, hooking up the 3D menu, match logic, viewer
- `src/ui.[ch]`: the fight's 2D sprites (`boutgfx/*.pix`) on a 640×480 virtual screen
- `src/sprfx.[ch]`: the fight's sprite effects (`fight/sfx/*.slb`: sparks, blood, move effects, teleport)
- `src/fe3d*.[ch]`: the 3D menu (assets, script VM, renderer)
- `src/video.[ch]`: the FMV (`.brp`) player
- `src/audio.[ch]`, `src/dr_mp3.h`: music (MP3, dr_mp3 v0.7.2, public domain / MIT-0), the sound effect mixer and the `.asf` decoder
- `tests/fightsim.c`: headless fight simulation (two CPU players), for regression tests
- `tests/fecheck.c`, `tests/fevm.c`, `tests/brpcheck.c`: front end asset check, script VM test, video decoder check
- `tools/*.py`: Python tools (unpacking, dumps, format research, `rip_cd.py`, `rip_video.py`)
- `docs/FORMATS.md`: description of the reverse-engineered formats
- `docs/ROADMAP.md`: the plan for further work
