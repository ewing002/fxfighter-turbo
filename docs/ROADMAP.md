# Roadmap – the full port of FX Fighter Turbo

## Structure of the original (from the EXE's assert messages)
`C:\Projects\Turbo\Source\`: `Fight.c` (fight engine), `Fsupport.c` (fighter loading), `FXInit.c`, `winfront.c` (menus),
`Sfxroot.c`/`Sfxproj.c`/`Fsound.c`/`Windsoun.c` (sound), `Effects.c`, `Sprlib.c`/`Sprplay.c` (2D sprites, HUD),
`Pixmap.c`, `Fastfile.c` (PAC), `Video.c` (FMV), `continue.c`, `Credits.c`, `Poollist.c`, `Dump.c`.
Compiler: Watcom C/386 (register calling convention: eax, edx, ebx, ecx).
BRender is statically linked, a fixed-point (16.16) build.

## Useful addresses in FIGHT.EXE
| Address | What it does |
|---|---|
| `0x41c300–0x41c500` | fighter loading (`fm%d.dat`, `shadow%d.dat`, `f%d.ani`, `f%d.cmp`) |
| `0x41f0a0` | `.ani` loader |
| `0x445dc8` | building the body-part actors (flat list) |
| `0x445f16` | **applying a frame to the bones** (`ani_bone_matrix` in the port) |
| `0x445430` | part of the fight's main loop (update and pose for both fighters) |
| `0x466c90` | BrModelLoadMany |
| `0x46d490` / `0x46e5c0` / `0x46e700` / `0x46e960` | BrMatrix34RotateZ / PostRotateX / PostRotateY / PostTranslate |
| `0x4a8780 + i*4` | clip count per fighter |
| `0x4c71e0 + i*4` | animation table pointer per fighter |
| `0x4231c0` | **camera**: distance 0.9·D + 1.6, angle from the vertical 95 − D/0.3 degrees, height +0.5, following the fight line with a 30-degree dead zone. D = √(dx² + dy² + h²) from the tracked pelvis positions (`a8320`/`a8330`: 0.1 dead zone, ¼ speed), h = the higher pelvis (updated only at distances below 3); the center is the average of the tracked positions, with a 0.01 dead zone |
| `0x42106c` / `0x4a4be8` | camera presets (128 of them: angle, tilt, distance, speed), selected by byte 0x21 of the moves |
| `0x41d5b0` | background: the `bground.dat` strip fixed to the camera, a camera turn = 320 units of scrolling |
| `0x4221b2` | camera director: move camera; ring edge (fighter within 1.8 of the edge, distance < 4: angle 90 ± 12·(3.5 − dist), tilt 83 − 8·(3.5 − dist)), if the detail level (`0x4a5888`) ≥ 1 |
| `0x421728` | fixed views: Home on/off (`4a4be0`), Ins/Del the 8th–15th preset, PgUp/PgDn distance (2–8), arrows angle/tilt (30–80) |
| `0x44c666` / `0x44cba0` / `0x44d868` | requesting a sprite effect (queue, 4 per frame), starting it, the `.slb` script interpreter (`src/sprfx.c`) |
| `0x433350` / `0x433630` | loading the match's `.slb` libraries, looking up an effect by ID and player |
| `0x4491f7` | hit detection: the boxes of the attacker's limbs merged (`0x448a69`, in z the smaller upper bound) + the previous check's box (sweep); zone from the center of the hit cube |
| `0x43fbca` / `0x4408f9` / `0x43f36e` | repulsion, ring boundary (measured along the fight line), ring-out (only the fighter farther from the center) |
| `0x440f42` | jump and landing (a8290: vertical offset, the lowest foot aligned to the ground) |
| `0x496150` | 1 player ladder: per fighter 4 orders × 15 bytes (7 fighters, then the bosses: 10, 11, 9), the rows of 12 are empty |

## Milestones
1. ✅ **Asset viewer**: models, textures, arenas, animations
2. ✅ **Move system** (done: `src/fight.c`): the 16-byte parameters of `moves/*.hdr` (clip index, frames, damage, hitbox?), the `.ste` state machine, the input → move mapping.
   This required reverse-engineering the `Fight.c` functions that read the `moves` data (starting from the xrefs of the `fight/moves/f%d.hdr` string).
3. ✅ **Fight**: joint hit cubes, continuous hit detection, damage, block, reactions, throws (foreign table), KO, rounds, timer, rotating fight line, sidestepping, jump and breakout, ring boundary, ring-out, daze (`cb3f8`), combo scaling (`0x4aa470`), breaking free by mashing (0xCC–0xCE) · ⏳ aligning the feet to the ground (`a8290`)
4. ✅ **HUD and menus**: `boutgfx/*.pix` sprites (with the arena's palette), `frontend/align.pix` (own palette, images stored upside down), `back1.pix`,
   loading screen, main menu, character and arena selector, VS, 3-round match, 1 player ladder · options menu (BESTO1..7, time, difficulty, controls),
   continue (`continue.fxl`, `p1cont.cmp`), credits and high-score table: all done in the 3D front end (see `docs/FRONTEND.md`)
5. ✅ **Sound**: CD music as MP3, `.asf` sound effects, banks, announcer, menu sounds. The original uses no spatial audio (no
   pan, pitch or distance attenuation); the `.slb` files are not sound parameters but sprite-effect libraries
   (✅ `src/sprfx.c`: block sparks, blood, the effects of the special moves, water splash, teleport; see `docs/FORMATS.md`.
   Projectiles (`Sfxproj.c`) are dead code: Turbo's move tables do not launch them)
6. ✅ **AI**: the original `0x425892` reverse-engineered (`ai_think` in `src/fight.c`, description: `docs/FORMATS.md`), difficulty level in the OPTIONS menu ·
   the `0x424d8f` ring-edge check also follows the original (2 units from the tracked pelvis: left, right or back)
7. **Extras**: gamepad (SDL_GameController), widescreen, high resolution, FMV player (✅ `src/video.c`).

## Known issues
- ~~The textures of fighters and arenas were wrong~~ → fixed (4×4 tile, one tile per model).
- The shading is the original BRender index lighting (one light from above, strength depending on the arena, from
  below in the 0th, 8th and 9th arenas), computed on the CPU. The effect of the `colmap` table is approximated by two texture
  levels (darkening, above 48 blending toward color 137); the differences are that the texturing is perspective-correct (affine in the original),
  and the blending is continuous, not rounded to palette colors.
- The background follows the layout of the original `bground.dat`; its vertical position is the original formula (`0x44556c`, `0x41d5b0`):
  y = 11 + trunc(250·cos(tilt))/2, with `0x4f1b5a` = 178 in every arena.
- The camera handles the moves' camera presets, the view used at the ring edge and the fixed views as in the original. On own damage (op 100–149) the camera shakes vertically (0.1 amplitude, 45° per tick, damped by 0.008).
  The "SAT CAM / CAR CAM / SIDE CAM" BRender cameras (`0x4a1e38`) are dead data, nothing references them.
- The jump arc is now the original formula (FUN_00440f42: (t·v + t²·g/2)·s + 0.3), about 1.4 m high. The ground alignment uses the lowest vertex of the models, so the sole stands on the floor.
- During a jump the fighter cannot leave the ring (our own decision); after a punch, throw or step out he can still fall out.
- After a ring-out the fighter falls with gravity and lands on the arena's lower level (if there is geometry there). The camera then targets the fighter who fell; this setting is our own, not from the original.
- Daze has no separate visual element: the write `0x4f1a48 = 0x17f` (`0x449148`) is dead, nothing reads it; the visible part is the DAZE move, which the port has too.
- The pairing of portraits and fighters comes from the materials of `frontend/demo.mat` (e.g. KIKO → PLYR2, LINNA → PLYR6, KWONDO → PLYR9).
- The menus are the original 3D scenes, with the original scripts (`docs/FRONTEND.md`); the old 2D menus were removed.
- Jerzan (fm12.dat, `pan_cr12`) is unfinished in the original: he does not appear in the ladder table, his body is shrunk to a point, his animation is Magnon's. In the port he is a hidden fighter (extension): he appears with Magnon's body parts and Jerzan's texture, and in the selector he can be chosen on Magnon with kick + punch.
