# FX Fighter Turbo – open port

## Goal
A rewrite of the original Windows game FX Fighter Turbo for modern systems. New C/SDL2 code runs on the data of the original `FIGHT.PAC`.
Documentation, code comments, commit messages and communication are in English.

## Status
- **Done:** asset loading (models, textures, arenas, animations), fight engine (punches, block, throw, jump, ring-out, rounds, timer), audio (CD music as Ogg Vorbis or MP3, ASF effects, banks, announcer), 3D camera with parameters recovered from the EXE, HUD with the original sprites, main menu, loading screen, character and arena select, VS screen, 3-round match, 1-player ladder (the original order table), computer opponent (the original AI, `0x425892`), controller support, OPTIONS menu with difficulty level, saved settings.
- **Not tried live:** on real Windows hardware with a real controller. Tests ran on Linux, in Xvfb and with the headless simulator.

## Layout
- `src/assets.[ch]` – PAC, CMP, pixmap, model, material, ANI loading
- `src/fight.[ch]` – fight engine (movement, hits, state machine)
- `src/audio.[ch]` – sound mixer, ASF decoder, music (`dr_mp3.h`, `stb_vorbis.c`; Ogg is tried before MP3, `tests/audiocheck.c` checks loading silently with `SDL_AUDIODRIVER=dummy`)
- `src/ui.[ch]` – the fight's 2D sprites (boutgfx), 640×480 virtual screen
- `src/sprfx.[ch]` – sprite effects (`fight/sfx/*.slb`): block sparks, blood, move effects, teleport
- `src/video.[ch]` – FMV (`.brp`) player
- `src/main.c` – rendering, camera, menus, match logic, input, options
- `src/fe3d.[ch]` – the 3D front end: scene assets and the script bytecode from `FIGHT.EXE` (see `docs/FRONTEND.md`)
- `src/fe3d_gl.[ch]` – renderer of the 3D front end (background, two parallel side views, main perspective view)
- `src/fe3d_vm.[ch]` – script VM of the 3D front end (runs the original bytecode; test: `tests/fevm.c`)
- `launcher/launcher.c` – Windows settings launcher (plain Win32, no SDL): edits `options.cfg` (all keys, unknown lines are kept), starts `fxfighter.exe` with the chosen `FIGHT.PAC`; centered on the primary work area, forces itself to the foreground. Auto start (launcher rule for all ports): Play after `autostart` seconds (launcher.cfg, default 10, Off/3..60) without input; 1 s grace, then cursor movement > 4 px or a held key/button cancels (GetCursorPos + GetAsyncKeyState, 100 ms timer; `FX_LAUNCHER_TEST=1` disables the input check). Target `fxfighter-launcher`.
- `tests/fecheck.c` – check of the front end assets
- `tests/fightsim.c` – headless CPU-vs-CPU simulation
- `tests/brpcheck.c` – video decoder check
- `src/tables.h` – tables extracted from the EXE (hit lines, reactions)
- `tools/` – Python helpers (unpac, cmp, brmodel, brmat, ani, moves, asf, pixlib, rip_cd, rip_video, pe, stratdis, menutab, brdump)
- `docs/FORMATS.md`, `docs/ROADMAP.md`, `docs/FRONTEND.md`, `README.md` – format description, milestones, plan of the 3D front end, usage
- The repository contains no binaries. Windows builds come from the GitHub Actions workflow `.github/workflows/build.yml` (`fxfighter.exe` + `SDL2.dll`).

## Reference material (not part of the repo)
These must be supplied locally; the `.gitignore` excludes `fight_decomp.c`, `*.PAC`, `*.mp3`, `*.ogg` and `video/`.
- The original `FIGHT.EXE` and `FIGHT.PAC` – from the original game folder. `FIGHT.PAC` is needed to run, `FIGHT.EXE` is needed for the 3D menus (and for `fecheck`).
- `fight_decomp.c` – Ghidra export (decompiled C) of `FIGHT.EXE`, for looking up functions. If you do not have it, `docs/FORMATS.md` and the code comments are the primary source.
- Unpacked data – `tools/unpac.py` produces it from `FIGHT.PAC`.
- The CD image (BIN + CUE) – source of the music (`tools/rip_cd.py`) and the videos (`tools/rip_video.py`).

The address table used for searching the EXE is at the bottom of `docs/ROADMAP.md`.

## Build and run
- **Linux/macOS:** `cmake -S . -B build && cmake --build build` (needs the SDL2 dev package). Targets: `fxfighter`, `fightsim`, `fecheck`, `fevm`, `brpcheck`.
- **Windows (MSYS2 MinGW64):** `pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-make`, then in the MinGW64 shell:
  ```
  cmake -S . -B build -G "MinGW Makefiles" && cmake --build build
  ```
  Copy `SDL2.dll` (from `<MSYS2 root>/mingw64/bin`) next to `build/fxfighter.exe`. Without CMake, from Git Bash with `export PATH=<MSYS2 root>/mingw64/bin:$PATH`:
  ```
  gcc -O2 -std=c11 -Dmain=SDL_main -I<SDL2 dir>/include/SDL2 \
    src/main.c src/assets.c src/fight.c src/audio.c src/ui.c src/fe3d.c src/fe3d_gl.c src/fe3d_vm.c src/video.c src/sprfx.c \
    -L<SDL2 dir>/lib -lmingw32 -lSDL2main -lSDL2 -lopengl32 -mwindows -o build-win/fxfighter.exe
  ```
  On Windows `--screenshot` can be run directly, no Xvfb needed. `FIGHT.PAC` is searched in the working directory and one or two levels above it; pass its path as the first argument if it is elsewhere.
- **Cross-compiling from Linux:** the same `gcc` command with `x86_64-w64-mingw32-gcc` and the MinGW SDL2 (e.g. 2.30.9) as `<SDL2 dir>`.
- **Simulator:** `gcc -O2 -Isrc tests/fightsim.c src/fight.c src/assets.c -lm -o fs`, then `./fs <FIGHT.PAC> <p1> <p2> <ticks> <seed>`.
  Environment variables: `V=1` (moves), `SND=1` (sounds), `FLOOR=1` (ground distance), `FORCE_IN=<hex>` and `HOLD=<tick>` (hold a given input), `NOMESH=1` (no model), `AI=<0..7>` (difficulty, default 4). In the game and the simulator `FX_AITRACE=1` prints the AI's decisions.
- **Front end check:** `./fecheck <FIGHT.PAC> [FIGHT.EXE]` (default: the `FIGHT.EXE` next to `FIGHT.PAC`); `V=1` also prints the model types and missing textures. The 3D front end reads the bytecode from `FIGHT.EXE` at run time.
- **Screenshot:** `./build/fxfighter <FIGHT.PAC> --p1 4 --p2 6 --ticks 130 --screenshot out.ppm`. For menus use `--screen title|menu|select|arena|vs|options`, for the 3D front end `--screen fe3d`. For headless runs use `xvfb-run -a`.
- **Quick testing:** `FX_AUTOTEST=1` (state log), `FX_AUTOWIN=1` (player 2 is not a CPU), `FX_SPEED=8` (accelerated time).
- Command-line options that skip the menu: `--p1`, `--p2`, `--arena`, `--2p`, `--demo`, `--fight`, `--viewer`. `--size WxH` sets the window size (works with `--screenshot` too). Resolution: `window_w/h`, `fullscreen_w/h` in `options.cfg`, F9 / Shift+F9 cycle it (`res_cycle`, `res_apply` in `src/main.c`).

## Important technical facts
- Game frequency: 48 Hz (`FIGHT_HZ`).
- Fighter models: `fm%d.dat`; the model faces local −Y, Z is up.
- Move tables: `moves/f%d.hdr` (36-byte records) and `.ste` (12-byte state machine steps). Details: `docs/FORMATS.md`.
- Camera: `0x4231c0` (formula; center and D from the pelvis positions tracked by the fight engine, `Player.tx/ty`), `0x4a4be8` (presets), `0x4221b2` (director: move camera, ring edge). Byte 0x21 of the move selects the preset. `FX_CAMTRACE=1` prints the view changes used at the ring edge.
- Hit: the attacker's boxes merged with the boxes of the previous check (sweep), `0x4491f7`. Collisions (hit, push, ring boundary, ring-out) follow the original, including its bugs. Details: `docs/FORMATS.md`.
- Jump: the formula of `FUN_00440f42`, with +0.3 base.
- Ground alignment: the model's lowest point (`lowest_joint`).
- Sound banks: FXL files. The `frontend` bank is used in the menus, `generic` in fights.
- AI: it does not press buttons but picks a transition based on bytes 0x19–0x20 of the move header. Details: `docs/FORMATS.md`.

## Open tasks (by priority)
1. **AI:** the original AI is done, including the ring-edge check at `0x424d8f`. Fighter 13 is unfinished data (no `fi13.cmp` and `f13.cmp`, cannot be selected in the game); the simulator exits with an error message for it.
2. **Jerzan (fighter 12):** unfinished content in the original: his rows in the ladder table (`0x496150`) are empty, the body parts of `fm12.dat`/`fm12b.dat` are shrunk to points (only head, hands and feet show), his animation is Magnon's (`f12.ani` = `f1.ani`), his move set is his own. The port makes him playable as a hidden fighter (extension): the point-sized body parts get Magnon's geometry with Jerzan's material (`jerzan_body`); in the select screen he is chosen by holding kick on Magnon and pressing punch (1P ladder, 2P); his ladder is Magnon's, and he is entered in the high-score table as Magnon. Test: the `match: p1 .. p2 ..` line (`FX_AUTOTEST`).
3. **Dizzy (daze):** the mechanic is done (daze meter, DAZE moves, combo scaling, breaking free by mashing). There is no visual effect: the original only writes the variable `0x4f1a48` (`= 0x17f`, `0x449148`) and never reads it; the visible part of dizziness is the DAZE move (`0x401785(i, 0x68)`), which the port has as well.
4. **Camera:** the ring-edge view (`0x4221b2`), the 8 fixed views (`0x421728`) and the shake are done. SAT/CAR/SIDE CAM are only dead BRender data (`0x4a1e38`), nothing to port. The ring-out camera is the port's own.
5. **Menus (3D front end):** the original menus are a BRender 3D scene driven by a script VM. Investigation, plan and status: `docs/FRONTEND.md` (tools: `tools/stratdis.py`, `tools/menutab.py`). Step 5 is done: the 3D menu is the default (input, sounds, music, settings, starting a match and returning). Step 6 is done too: the old 2D menus are gone, the menu is only the 3D front end (without `FIGHT.EXE` an error message; fight without menus: `--p1/--p2/--arena`). `--screen fe3d` draws the VM's scene after `FX_FE_TICKS=<n>` ticks and `FX_FE_INPUT="30:P1DOWN 75:P1PUNCH 130:P2PUNCH"`; the script also takes effect in a real run (`FX_AUTOTEST=1 FX_SPEED=10` prints the modes; the window is then hidden, there is no fullscreen, and both fighters are computer-controlled – run it with `SDL_AUDIODRIVER=dummy` and `FX_PREFDIR=<separate folder>` so that it makes no sound and does not write the user's `options.cfg` and `hiscores.dat`; changing `APPDATA` is not enough because SDL queries the real folder). `FX_FE_ENTRY=<mode> FX_FE_G="59=60000,63=80000"` shows an entry mode in screenshot mode (e.g. 1 = tournament VS, 2 = high-score name entry with `62=50000,61=00000105`, 6 = results). The challenger joining is dead code in the original; the port implements it as an extension (`challenger`, test: `FX_CHALLENGE=<s>`).
6. **Continue, credits, FMV:** the CONTINUE screen, the high-score table, the videos (`src/video.c`, from the CD with `tools/rip_video.py`) and the credits are done. Videos do not run in tests (`FX_AUTOTEST`), only with `FX_VIDEOTEST=1`; `FX_NOVIDEO=1` turns them off.
7. **Shading:** the original BRender index lighting computed on the CPU (`shade_of` / `shade_l` in `src/main.c`; description: `docs/FORMATS.md`, "Shading"). Lit textures are approximated with two texture levels: the brightness of the `colmap` row, and above 48 a blend toward palette color 137 (highlight, full at row 60; `FX_NOHIGHLIGHT=1` turns it off). The original's affine texturing is not implemented.
8. **Background and floor:** the background position, the three floor detail settings and the floor animation (`0x428f0a`, arenas 0xa and 0xb) are done as in the original.
9. **Sprite effects (`.slb`):** done (`src/sprfx.c`): block sparks and blood on a hit, the effects of move event `0xe1` (charging, muzzle flash, nozzles, water splash), the teleport at the start of a match (the fighters and their shadows are invisible for 1.75 s, until the beam is at its widest, `0x444ea6` / `0x444f54`). The fight engine queues the requests in `Fight.fx` (4 per frame); `main.c` starts and steps them after every tick. `FX_SPRTRACE=1` prints the requests. The projectiles (`Sfxproj.c`) are dead code: no move table starts them. Open: the rotation (Euler angles) of sprite kinds 2–4 comes only from the original's formula, with no live comparison.

## Decided questions (with the user)
- There will be no network play in the port (deliberately left out).
- Ring-out is forbidden during a jump by the port's own decision, not from the original; it stays that way.
- At the end of a jump the fighter lands on the floor in one step (`land_snap`, like the original's `cb4e0` alignment), which removed the pelvis jump at the start of JUMP LAND 3; the small remaining difference is the pose difference of the animations.

## Publishing
The public repository history is English: commit messages are in English, and so are the documentation and code comments. Do not commit original game content (`FIGHT.PAC`, `FIGHT.EXE`, `fight_decomp.c`, MP3 or video files) or binaries.

## Working mode
- Work in small steps. After every change, build, look at a screenshot or a simulation log, and update the documentation.
- Check both the Linux and the Windows build before a release.
- Move on to the next milestone based on the user's feedback, following `docs/ROADMAP.md`.
- Documentation, code comments, commit messages and the repo are in English.
