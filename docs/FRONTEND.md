# The 3D front end (winfront.c) – reverse-engineering notes and plan

The original menus are not 2D images but a BRender 3D scene driven by a bytecode script VM
(called a "strat block" in the decompiled code). The C code only provides the interpreter, the object system, the triggers,
the rendering and a few system calls. All menu logic, layout, animation and option handling
lives as bytecode in the data area of `FIGHT.EXE`.

Helper tools (run from the port's directory; `FIGHT.EXE` defaults to `../FIGHT.EXE`, or set `FIGHT_EXE=<path>`):
- `PYTHONIOENCODING=utf-8 PYTHONPATH=tools py -3 tools/stratdis.py [n ...]` – disassembles the scripts
- `PYTHONIOENCODING=utf-8 PYTHONPATH=tools py -3 tools/menutab.py` – the menu tables (items, positions, cursor positions)

## 1. Data in the EXE

| Address | Contents |
|---|---|
| `0x49fb88` | script pointer table, 93 entries; the bytecode spans `0x499ba8`–`0x49fb88` (about 24 KB) |
| `0x4a0048` | opcode table, 238 entries (0x00–0xED), handlers `0x404d8f`–`0x40c25a` |
| `0x4a0408` | trigger types, 16 entries |
| `0x4a2948` | model type → model name in `crshipl1.dat`, 212 entries |
| `0x49ae53` | fighter → portrait model; `0x49d5a9` barrel face → fighter |

Files in the PAC (`fight/frontend/`): `crshipl1.dat` (210 models), `demo.mat` (246 materials), `demo.pix`, `demo.pal`,
`align.pix` (textures loaded by name, `0x4129c0`), `back1.pix` (background with the frames), `text.pix` (font,
only for the credits and messages), `shade.tab` (8-bit shading table).

## 2. Execution

- **Main loop** (`0x4135f1`): `back1.pix` to the full screen → input (`0x42784c`) → N script ticks
  (`0x404902`, N = elapsed time / 66.67 ms, at least 1, so about 15 Hz) → the right and left worlds into the two side
  panels → the main world to the full screen. If `DAT_004a1df0` (mode) is set, it exits and starts the game.
- **Object** (`0x404a96`, 0x108 bytes): position (1000 = 1.0 unit), rotation (0.1 degree), scale (256 = 1.0),
  velocity, target, parent/child chain, instruction pointer (`+0xf8`), stack, trigger list, local variables
  (`+0x74`…`+0xd0`), world (0 main, 1 right, 2 left). Matrix: `RotY · RotX · RotZ`, translation, then *pre*-scaling
  (`0x46ea60` = S · M, so the scale affects only the model, not the object's own position); a child's matrix is
  multiplied by its parent's, so the parent's scale also scales the children's positions.
- **Globals:** `G[n]` = `DAT_004f3e20 + 4n` (16.16). Important ones: G69 / G72 / G75 are the state of the upper / lower / middle
  box, G73/G74 the selected / cursor item, G11/G12 the two players' fighters, G20–G57 the options,
  G58 the entry mode (return from the game).
- **Interpreter** (`0x40c28c`): `ip = op[*ip](object)` until it yields. Jumps are relative to the operation's own
  address, signed 16-bit. Then physics (`0x40c39e`), triggers (`0x40c9ba`).
- **Triggers:** 0 never, 1 always, 2–8 every 2nd…128th frame, 9 collision, 10 arg ≤ 0, 11 far in z,
  12/13 system bits, 14 timer, **15 key**. The keys are the players' input slots (P1 right/left/down/up/
  punch/kick, P2 likewise, Esc), so the menu works with the configured buttons and with the controller too.
- **Starting the game:** opcode 0xD7 writes the mode (`DAT_004a1df0`: 0 demo, 1 single player, 2 two player,
  3 endurance, 4 tournament, 5 network, 6 quit, 7/8/9 back to the menu) and the fighters, arena and handicap.
- **Options:** `0x433c23` copies the settings into the globals G20–G57, `0x434001` copies them back.

## 3. Scene

- **Main camera:** perspective, 45° field of view, hither 0.1, yon 50, aspect ratio 1.46, at (0; 0; 0.2), looking toward −Z.
- **Side cameras:** parallel projection, hither 0.2, yon 20, width 0.40, height 0.378, at (±0.19; 0; 0) in their own
  world. The panels: left x 30–143, right x 496–609, y 17–458.
- **Lights:** one directional light per world (45° around Y, then another 45°).
- **Drawing:** the original draws with Z-sorting and no depth buffer; in the port a depth buffer is enough. Palette index 0
  of the textures is transparent (for the front and back faces of the boxes about 82% of the texels are, for the portraits' `EMPTY` frame 91%),
  and faces are single-sided (BRender does not draw back faces). Both are needed: without them the black interior of the frame covers
  a portrait sliding into the box, and the back portraits show through on the cylinders.
- **Boxes:** the upper box is at (0; 63; −200), the lower at (0; −81; −200), size 167. On a state change they
  turn 2 × 90° ("card flip"), and their contents are swapped in the meantime. The menu items are children of the box.

The detailed layout of the screens is given by the output of `tools/menutab.py` (main menu, options, controls, detail,
round, sound, button redefinition, arena, VS panel, letter grid, statistics).

## 4. Plan (in small steps)

1. ✅ Reconnaissance, tools (`tools/stratdis.py`, `tools/menutab.py`, `tools/pe.py`).
2. ✅ **Tools on the C side** (`src/fe3d.c`, check: `tests/fecheck.c`; 210 models, 246 materials, of which 235 textured – the missing 11 do not exist in the original either –, 93 scripts, 208 of the 212 model types matched): `crshipl1.dat` models, `demo.mat` materials, `demo.pal` + `demo.pix` / `align.pix`
   textures, and reading the bytecode at run time from the user's `FIGHT.EXE` (no original code goes into the repo).
3. ✅ **Static scene** (`src/fe3d_gl.c`, `--screen fe3d`; the height of the parallel side camera gives the 442 px, with square pixels – the portrait cylinder exactly fills the panel): the three views (main perspective, two parallel side views), `back1.pix` background, lights;
   as a trial, the main menu placed by hand based on the tables. Verified with a screenshot.
4. ✅ **Script VM** (`src/fe3d_vm.c`, headless test: `tests/fevm.c`; `--screen fe3d` now
   draws the VM's scene, after `FX_FE_TICKS` ticks and an `FX_FE_INPUT` input script): 512 objects with hierarchy, stack,
   triggers, 15 Hz tick. The 93 scripts actually use 112 opcodes, all of which are implemented; another
   ~40 (turning, following, velocity physics) were derived from the reverse engineering but no script uses them, so
   they are unverified. Verified: the main menu builds up when idle; the cursor moves; OPTIONS opens and
   closes; starting 1P gives `START_GAME mode 1`; after about 61 s of idling the demo starts (mode 0; the root script
   counts 30×30 ticks, not 30 s); with random input, modes 1–4 and 6–9 all
   occur within 50,000 ticks, without a script error. Stubs and deviations:
   - network: E1 = 0, E2 = −1, E3/E8 empty ("no network", so mode 5 is unreachable);
   - E4 (raw key): the original blocks, here the operation retries on the next tick
     (`fe_vm_key_raw`, DirectInput codes);
   - cameras (0x96, 0x2A): no effect, the renderer's cameras are fixed;
   - face swap (0x97, `FUN_00409d15`): the FRONTA–D material (the faces of the upper and lower boxes) receives the source material's
     color map: 0 = FRONTBACKUP (OPTI1), n = ARENAn (a 256×256 arena image in `demo.pix`); the map transform
     in mode 0 is a 180° rotation, otherwise a mirror in u (BrMatrix23Scale(−1, 1)). At start-up the root script sets all
     four faces to FRONTBACKUP; the 2P arena selector (script 15) alternately replaces the 2nd and 3rd faces of the lower card with
     the selected arena's image while the card flips (G72 = 8/9). The port passes this in the scene (`FeScene.face_*`),
     and the renderer transforms the face's UVs;
   - system operations (D8–E0, E9/EA, EC/ED): the cfg ↔ G rewriting is done by the VM, while file, volume and
     music handling is left to the host (`sys` callback);
   - trigger types 12/13 never fire, and there is no collision (no script sets a mask);
   - the world-position function faithfully returns 1/10 script unit, as in the original;
   - scale is per axis (the middle box is 256/128/128), hence `FeNode.scale[3]`.
5. ✅ **Integration** (`src/main.c`, `fe_*` functions; the 3D menu is the only menu):
   - input at 15 Hz: P1 W/A/S/D + F/G (Enter, Space too), P2 arrows + K/L, controllers, Esc/Backspace;
     the port remembers short presses between ticks; raw keys (E4) with a DirectInput code (`di_scan`);
   - sounds from the `frontend` bank (bank 0; the announcements marked `#` play in sequence, one after another, see `docs/FORMATS.md`),
     music: E9 → track 14, EA/START_GAME → stops;
   - the scripts run at the original 15 Hz, but the port draws every frame: the VM keeps the previous tick's positions,
     and the drawing interpolates between the two ticks (`fe_vm_scene_lerp`; a turn of more than 120° per tick or a jump of more than 0.4
     units is not smoothed). The drawing thus lags by at most one tick (67 ms);
   - settings: the port's `options.cfg` ↔ the configuration block (difficulty = `0x4a587c`, round time 30/60/90/infinite,
     wins 1–3 ↔ 1/3/5 rounds, volume 1–8 ↔ 0–10, without slippage). As in the original, SAVE (D9/DD) saves,
     LOAD (D8) loads, the volume sliders (DF/E0) take effect live; when a match starts, the G → cfg rewrite takes effect even without saving;
   - START_GAME: 0 demo (machine vs. machine, one round, any button interrupts), 1 ladder (with the port's ladder table),
     2 two player (with the VM's fighters and arena), 3 endurance, 4 tournament (below), 6 quit, 7/8/9 restart of the menu,
     5 (network) is unavailable;
   - **endurance** (`FUN_00419219`): p1 / p2 are each a team of at most 4, from the top byte downward; per byte
     `& 0xf` is the fighter, `0x20` = human (`DAT_004f3264` human mask: 1P = 1, 2P = 3, demo = 0). The arena is the first fighter
     of team 2's, the round time is infinite (`0x4a587e = -1`). After each round only the loser brings the next fighter,
     and only his life is restored to 120 (the match function does not refill it when `0x4f3268 == 0x16`). In place of the win markers,
     the fighters waiting in the team are shown; if a team has one fighter left, the announcer says "last fighter"
     (generic 11). The team that runs out loses;
   - **tournament** (`FUN_00431676`, `Wraptour.c`; the VS screen is script 29, the results table script 88): the order of the starter bytes
     is p1>>24, p2>>24, p1>>16, p2>>16, …, `& 0x1f` is the fighter, `0x20` = human; 4 or 8 starters (1–4 or 5–8
     humans, the rest computer-controlled). Placement bracket with 20 static nodes (`0x4a6500`): with 8 starters 4 quarterfinals, 2 consolation matches,
     2 semifinals, the 7th, 5th and 3rd place matches and the final (12 matches), with 4 starters 2 semifinals, 3rd place, final. Pairing is the
     order of the places; a human always goes to side 1 against the computer; the arena is that of the side-2 fighter; rounds and time
     are the usual ones. Any button skips a computer-vs-computer match with a random winner. During a fight the name panel is `pan_pl<n>` or
     `pan_cpu`. From the 2nd match on, the front end's VS screen appears (entry mode 1, G59/G60 the two fighters in 16.16, G63 the two
     labels: starter 0–7, 8 = cpu), at the end the standings (entry mode 6, G59/G60 the placed fighters,
     G64/G65 the starters, as raw bytes, the better place on top); a punch on the table leads to the main menu. There is no bracket screen.
     In the tournament the sound bank 0 is `tour.fxl` (`0x413a6d`: the `generic` list + 20–30 the fighter names + 31 "wins"), and
     at the end of each round the announcer says the winner's name (`0x42263c`: fighter + 0x13), followed by
     "perfect" (7) if his life was full; instead of "you win" / "player n wins";
   - handicap (2P): parameters 5–6 of START_GAME (1–8) go into bytes 0–1 of the configuration block (`0x40bce7`), and
     between two humans they multiply the attacker's damage by 0.7…1.4 (4 is neutral; see `docs/FORMATS.md`, damage). The port
     keeps it in `options.cfg` (`handicap1`, `handicap2`, default 4; the original's factory value is 3);
   - game speed (OPTIONS, G51 → `0x4a5899` = 1–3, op EC → `FUN_00433f83`): the table at `0x4a1cf0` gives the length of one fight frame
     in PIT ticks (24850, 19880, 17890), i.e. 48, 60 and 66.7 Hz; the round clock, however, runs down in real time
     (`0x445652`: closing time − timer), and the ladder's total time is real time too. The port sets the pace of the fight and the round clock
     (`Fight.tick_pit`) accordingly; the setting goes into `options.cfg` (`speed`). EC also plays a feedback sound
     (`0x4ab058[speed]`), which is not wired up;
   - detail (DETAIL, G27–33 → `0x4a5883–89`): P1 texture, P1 shading, P2 texture, P2 shading, floor texture
     (fltxper, 0–2), floor detail (lomedhi, 0–2) and the background (on/off). The fighter's drawing mode (`0x402004`, the
     debug text "TYPE: FLAT / GOURARD [TEXTURED]") comes from the texture and shading switches; the port follows this
     (material color instead of texture, Gouraud shading with vertex normals instead of flat). During a fight (`0x4414c7`, DI
     0x3B–0x40) F1–F4 toggle the fighters' switches, F5 steps the floor texture 0→1→2, F6 toggles the background; the
     floor detail has no key, it takes effect when the arena is loaded. The port follows all three (`options.cfg`:
     `detail5..7`):
     - floor texture (`0x4a5887`, material update `0x41c6c0`): 0 = untextured, the TXGFLOOR n / TXFFLOOR n material gets its color from the
       10-element ramp of the palette starting at 16(n−1)+10 (TXG smoothed and lit, TXF flat); 1 and 2 =
       textured, the TXF without lighting (full brightness), the TXG lit. 1 and 2 differ only in the affine and the
       perspective mapping, under OpenGL they are the same;
     - floor detail (`0x4a5888`, loader `0x41bfd0`): 0 = no floor model, below the horizon the floor color
       (palette 0x20); the gameplay is then different too: no ring-out, no ring boundary (`0x4408f9`), the ring-edge
       camera does not start either (`0x4221b2`), and the distance between the two fighters is at most 8.0 (`0x43fbca`). 1 = `floor_m%x.dat`
       (the same plain ring in every arena, the outer plane FLOOR_COLOUR = unlit palette 0x20), 2 =
       `floor_h%x.dat`, the full scenery;
     - background (`0x4a5889`): 0 = no panorama, black above the horizon (`0x41d420`);
   - return after the match (`ProcessWrapperReturns`, `0x413d3d`, status → G58): end of demo (0), Esc (0), 1P game
     over (10 → 0), end of ladder (11 → 2, name entry), end of endurance (5/6 → 7: WINNER / LOSER screen; G59/G60 the winner's
     and loser's fighters, G61/G62 their sides, `0x41970d`), tournament (13 → 6), 2P match (22 → 4, the 2P selector). Not in the
     port: the network (23 → 5). At the end of the ladder the WIN video, the credits and the name entry follow, on game over the LOSE video;
   - credits (`Credits.c`, `0x429800`, `fight/credits.txt`): empty lines are skipped; `#` is a comment; the `List` / `Fmv` /
     `Sound` prefix (case-insensitive) switches mode. Mode 1: `name;title;x;y` = the team screen (small green name at
     (x, y−50), large orange title at (x, y), relative to the center; 1.28 s fade-in, 1.5 s hold, fades to 72%,
     black); the `Scroll,Dn` and `Code:` fields do nothing when absent. Mode 4: the nth announcement of `credits.fxl`, mode 3:
     the `fight/video/<name>.brp` clip (silent, the music continues), after both of which it returns to mode 1. Mode 2: a scrolling
     line on a 640×480 paletted canvas (demo.pal): the letters of `text.pix` (small green 16×16, `>` = heading in the large orange
     letters; letter table `0x4a5994`, width `0x42a20e`), 3 pixels per step / 60 ms, a line is 6 steps (heading 7).
     Music: CD track 2. Esc or Space between lines exits, Esc during a clip too; any other button skips the clip. The port's
     `src/main.c` (`credits_*`) follows this, with two fixes: in the original the width measurement (`0x42a20e`) uses different values
     from the drawing routine's advance, and the advance of the large digits and the punctuation comes from garbage values in the table.
     As a result the lines were shifted to the right and the `>S 3   Code` heading slid out of view. In the port a single shared advance
     (`cr_adv`) measures and draws the line: a large digit is 24, punctuation is as wide as a space. This way the lines
     are centered and every heading is visible;
   - continue screen (`continue.c`, `0x4368c0`; details: the notes in `continue_notes.md`): after a lost 1P match
     (`0x41aec7`) and after every 2P match (`0x40413a`), on the losing side; endurance, tournament and demo do not call it. Two
     steel doors (doorl/doorr.cmp, 160×240) close (x += v, v += 1.2 per frame, 17 frames), then "PLAYER n
     CONTINUE" (p1cont/p2cont.cmp, 192×50, at (64, 51) on the 320×240 image), in 2P the champion's trophy (champ.cmp,
     80×32, below the winner's half, the streak number with timer.pix digits) and a countdown from 10 to 0 (rndnumbr.pix,
     (134, 151), real time, 10.98 s). Sounds from the `continue` bank: 1 door starts, 2 door closes + 5 "Continue?",
     3/4 ticking (even / odd); the music is quieter meanwhile. Only the loser's input counts: punch → 1 (with QUICK CONTINUE
     2), kick → 1, any other button −0.25 s, expiry → 0. 1P: 0 game over (main menu), 1 → status 20 → entry mode 3
     (G67 = the ladder position): choosing a new fighter, and the ladder continues at the same place (op ED / `0x41a7e0`: if the new fighter is still
     ahead, the old one takes his place; the total time is kept), 2 → the same match again. 2P: 0 → main menu, 1 → 2P
     selector (the champion and his streak are kept), 2 → rematch. The trophy's streak (`0x40179e`: 0x4a1d68 / 0x4a1d6c) is reset on a new 2P
     round. Not in the port: the network versions (pncont, pnwcont);
   - challenger joining in: in the original this is unfinished, dead code. The ladder converts match result 12 into status 12
     (`0x41afe4`), to which `ProcessWrapperReturns` responds only by logging ("OTHER Player joining in on 1P game : GOTO 2P
     selection", `0x413f69`), without setting the entry mode (it would be the main menu). The match function (`0x44506d`), however, never
     returns 12: its result is 2/3, −1, 0, 1 or −2. As an extension the port provides the intended behavior: during a single-player ladder,
     the 2nd player's punch button (or the second controller's Start button) interrupts the match, the ladder ends,
     and entry mode 4 (the 2P selector) follows, with a new 2P round (`challenger_join`, `src/main.c`; `options.cfg`:
     `challenger`, default 1). Test: `FX_CHALLENGE=<s>` makes the challenger join after this many seconds of ladder fight time;
   - high-score table (`0x4a7b40`, `fight/hiscores.dat`): 10 × 8 bytes, 3 letters, the fighter and the time (DOS ticks, 18.2 Hz,
     16.16). On every start and return, the front end puts it into OPT[0..19] (`FUN_0043b513`: OPT[i] = the
     letters − 'A' in the upper three bytes, the fighter in the lowest; OPT[10+i] = the time's digits as P P M M bytes), and
     OPT[20..30] = 2 (`0x4a1d48`). If the player has beaten the whole ladder ("1 Player HAS BEATEN AI", `0x413ea7`), the menu
     starts with entry mode 2: G62 = the fighter (16.16), G61 = the digits of the total time spent fighting (`0x4c7320`,
     per round the round's time). The script decides from this whether he makes it onto the table; if so, it asks for a name on the
     letter grid, the DE (`FUN_0043b5f0`) writes the table back, and the port saves it to `hiscores.dat` (next to the settings). Factory
     table: SIM 10:20, CLV 11:55, DIG 12:17, DAN 12:19, PHL 12:43, HUN 13:02, OWN 13:21, SUE 13:57, ZHR 14:26,
     JAS 14:34;
   - key bindings: the fight input (`0x428036`) reads 6 buttons per player from `0x4a588a + 6p`: ATTACK
     (forward from the starting side), RETREAT, JUMP, DUCK, PUNCH, KICK (DirectInput codes; bits 0x01/0x02, 0x08, 0x04, 0x10,
     0x20). The roles of ATTACK and RETREAT swap when the fighters change sides (`0x4a8300`, set by the camera function),
     so the buttons are screen directions: for player 1 ATTACK is the right button, for player 2 the left. The port keeps the
     12 codes in `options.cfg` (`key1`…`key12`, default D A W S F G and ← → ↑ ↓ K L), the menu's CONTROLS →
     REDEFINE KEYS screen sets them (SAVE saves), and they apply in fights as well as in the menu. The fight
     hotkeys (B, N, R, M, X, 1, 2…) are inactive if a human player's button is the same key. When waiting for a raw key (E4) starts,
     the VM forgets earlier presses, otherwise it would pick up the punch button that selected the slot.
   Verified with accelerated, scripted runs (`FX_AUTOTEST=1 FX_SPEED=10 FX_FE_INPUT=…`): 1P ladder, 2P (with the handicap and
   LOADING screens), demo, quit, SAVE, key redefinition and saving, endurance, tournament (VS and results screens
   with screenshots, `FX_FE_ENTRY`/`FX_FE_G`), the challenger joining in (`FX_CHALLENGE`). Step 5 is complete.
6. ✅ **Retiring the old 2D menus:** the title image, the 2D main menu, OPTIONS, the character and arena selectors and the VS screen, the
   `--menu2d` and `--screen title|menu|…` were removed (`src/main.c`), and only the fight's sprites remain in `src/ui.c`.
   Without `FIGHT.EXE` the program exits with an error message; the command-line fight (`--p1/--p2/--arena`, `--fight`) runs without
   a menu and exits on Esc. The settings that cannot be set from the 3D menu (`fullscreen`, `vsync`, `ringouts`, `rumble`,
   the 99 s round time) remain in `options.cfg`.

Risks: the 8-bit palette and the `shade.tab` shading can only be approximated; the meaning of the 0xBD/0xBE and the D8–E0, E8–ED
system operations is only partly verified; the 15 Hz pace assumes a millisecond timer.
