# FX Fighter Turbo – file formats

Reverse-engineered from `FIGHT.EXE` (Watcom C, BRender 1.2.2, 16.16 fixed-point build) and the data files.
Formats marked ✅ are already read by the port, ◐ are partially understood, ❌ are not yet processed.

## FIGHT.PAC ✅
```
u32 count
count × { u32 offset; char name[80]; }   // little-endian; the last entry has an empty name = end of file
```
655 files. The names have the form `fight\...` and may be in lower or upper case.

## *.cmp – RLE images ✅
```
u32 unpacked_size
c < 0x80  : repeat the next byte (c + 2) times
c >= 0x80 : copy (256 - c) literal bytes
```
Exception: the `bgrounds/picp*.cmp` files are not compressed but are BRender pixelmaps (see below).

| File | Size | Contents |
|---|---|---|
| `fighters/f%d.cmp`, `f%db.cmp` | 256×256, 8 bit | fighter texture (`b` = the second outfit / P2 colors) |
| `bgrounds/pic%x.cmp` | 640×200 | arena panorama (360°) |
| `bgrounds/picp%x.cmp` | 256×1 RGBX | the arena's palette |
| `floors/floor_[hlm]%x.cmp` | 256×256 | arena texture (h/m/l = level of detail); `floor_h%x.dat` also contains the structures surrounding the ring (pillars, walls, crates, towers) |
| `fighters/fm%d.cmp`, `chr%d.cmp`, `fi%d.cmp` | – | ◐ menu portraits and icons (fm=256×256, chr=512 B, fi≈580 B) |

## BRender chunk files (.dat, .mat, .pix) ✅
Big-endian `u32 id, u32 length, data`. The ids follow `core/fw/datafile.h`
(BRender 1.3.2, MIT license, github.com/foone/BRender-v1.3.2).

These occur:
- `18 FILE_INFO`: type (0xFACE = model, 5 = material, 2 = pixmap)
- `54 OLD_MODEL_2`: u16 flags, name
- `23 VERTICES`: u32 n, n×3 float
- `24 VERTEX_UV`: u32 n, n×2 float
- `53 FACES`: u32 n, n×{u16 v0,v1,v2, u16 smoothing, u8 flags}
- `22 MATERIAL_INDEX`: u32 n, n×ASCIZ name. **The length field is wrong and must not be relied on.**
- `26 FACE_MATERIAL`: u32 n, u32 element size (2), n×u16 (1-based index, 0 = no material)
- `4 MATERIAL_OLDEST`: RGB(3) opacity(1) ka kd ks power(4×float) flags(u16) map_xform(6×float) index_base(u8) index_range(u8) name
- `28 COLOUR_MAP_REF`: ASCIZ
- `3 OLD_PIXELMAP`: u8 type, u16 row_bytes, u16 w, u16 h, u16 ox, u16 oy, ASCIZ name
- `33 PIXELS`: u32 count, u32 element size, data

Materials whose names start with `TX` are textured; the code assigns the texture to them (`f%d.cmp` for a fighter, `floor_h%x.cmp` for a floor).

**Texture tiles:** every 256×256 texture is 4×4 tiles of 64×64. **The i-th model of the .dat file uses the i-th tile**
(row-major, left to right, top to bottom), and its UV coordinates (0..1) are relative to the tile. This holds for the fighters
(16 body parts = 16 tiles, the Skull is tile 14) and for the floors (parts A..P) as well.
Some UVs contain garbage (−1.7e38); these must be clamped to 0..1.

**Palette banks:** the color of non-`TX` materials (`BANKn`, `FLOORn`, `FFLOORn`, …) comes from the palette: `index_base = 10 + 16·(n−1)`,
`index_range = 15`. This is a 16-step gradient running from dark to light, and the color is `palette[index_base + shade]`.
The RGB value in material.mat is only an approximation for true-color mode.

### Fighter model: `fighters/fm%d.dat`
16 body parts, in alphabetical order. **This order is also the bone index**:
```
0 LeftFemur  1 LeftFoot   2 LeftHand   3 LeftHumoru  4 LeftRadius  5 LeftTibia
6 Neck       7 Pelvis     8 RightFemur 9 RightFoot  10 RightHand  11 RightHumor
12 RightRadiu 13 RightTibia 14 Skull   15 Thorax
```
In the coordinate system Z points up and +Y forward. The pelvis is at the origin.
If both players pick the same fighter, P2 gets the `fm%db.dat` + `f%db.cmp` pair (the other outfit).
The body parts are flat children of a common root actor (built by the function at `0x445dc8`); there is no bone hierarchy.

## Animation: `fighters/f%d.ani` ✅
(The `.an4` has the same format, but the EXE does not load it; presumably it is an older version with a lower frame count.)
```
u32 nbones (=16), u32 nclips, u32 total_frames
nclips × {
  char name[16]            // there may be garbage after the NUL
  u32  nframes
  nframes × {
    s32 root_x, root_height, root_forward      // × 406 / 65536
    16 × { u8 ax, az, ay;  s8 tx, tz, ty }
  }
}
```
Bone matrix (FIGHT.EXE `0x445f16`, BRender row-vector convention):
```
M = RotateZ((255-az)<<8) · PostRotateX((255-ax)<<8) · PostRotateY((255-ay)<<8)
T = (tx, ty, tz) × 406 / 65536        // note: in the file the order is tx, tz, ty
```
The angle is a br_angle (65536 = 360°). The Skull (14) additionally gets a head-turn offset from the game state.
A change in `root_forward` moves the fighter forward (`BrMatrix34PostTranslate(0, Δ/2, 0)`).

## Moves: `moves/f%d.hdr` ✅ / `moves/f%d.ste` ✅
Loader: `0x433120`. P1's tables are at `0x4d6390` (moves) and `0x4ccd90` (states); P2's are at offsets of +0x4800 / +0x4b00.

### `.hdr`: `u16 ?` + N × 36 bytes
| Offset | Field |
|---|---|
| 0x00 | name (20 bytes, e.g. "STAND", "PUNCH", "HIT FACE L") |
| 0x14 | attacking body-part group: index into the `LIMB_SETS` table (`0x4aa510`) |
| 0x15 | stance: lower 5 bits = identifier (4 = standing, 8 = during a throw…), 0xC0 = attack type |
| 0x16 | class (3 = on the ground) |
| 0x17 | animation: ≥200 → clip (value−200) of the fighter's own `f%d.ani`, <200 → clip of the shared `f0.ani`, 255 → no change |
| 0x18 | sound |
| 0x19 | AI flags: bits 0–1 = kind (1 attack, 2 block, 3 turn around), 0x0C = ring-edge check, 0x40 = high, 0x80 = the AI never picks it |
| 0x1A–0x1E | AI score for position slots 1–5 |
| 0x1F | position mask (in which opponent positions it is stronger, see below) |
| 0x20 | number of frames the move needs before the opponent's punch |
| 0x22 | u16: index of the first state record in the `.ste` |

### `.ste`: 12-byte records (little-endian)
```
u8 op; u8 fr; u16 val; u16 mask; u8 cond; u8 minfr; u8 ctx; u8 pad; s16 target
```
A move's records run from the index at `0x22` to the terminating record (`mask == 0 && ctx == 0`).
- **Input transition** (`0x4427c4`): fires if `((in ^ previous) | in << 8) & mask == val`, and `minfr ≤ frame`,
  `cond` (the opponent's stance: 0 = don't care; bit `0x10` = "not equal") and `ctx` (`ctx>>4 == ctx & context`) also match. Then → `target`.
- **Event** (`0x441c0b`): when the frame reaches `fr`: `op` 1–99 = hit check with `op` damage (`cond>>6` = direction),
  100–199 = own damage, 0xD0–0xD7 = movement mode, 0xD8 = turn around, 0xDE = sound, 0xE0 = hold frame, … A record with `cond == 8` means a throw.
- **Terminating record**: if `fr ≤ frame` or the animation has ended → `target` (`cond == 0x0A`: 180° turn).
- The `op` of blocking states is 0xFF (high/mid block) or 0xFD (full); 0xFE = invulnerable.
- Transitions with `val=0x00FF` are for the AI only (unreachable with human input).

### Computer opponent (`0x425892`, `ai_think` in the port)
The AI does not press buttons: it chooses among the transitions of the current move and starts the target move directly (`0x43e828`).
- It does not act if either fighter is in a throw (stance 8). If it is facing away, it starts the first kind-3 (turn-around) target.
- Distance (`0x4a5698`): < 0.8 close (slots 1–5), < 1.0 mid (slots 1–3), otherwise far (slot 1 only).
- Scoring (`0x425166`) for each transition (`val` and `mask` ≠ 0xFF00, `cond`/`ctx`/`minfr` match):
  `score = target[0x19+slot] × MULT[dist][slot] × MULT[3 + opponent_low][slot] × weight − opponent_move[0x19+slot]`.
  `MULT` = `0x4a56a8` (5×6 int). The weight (`0x4250cc`) comes from the intersection of the position bits (`0x424f80`: ducking, airborne, on the ground, dazed, blocking, attacking high/low)
  and the target's 0x1F mask: per bit `0x4a5778[bit] + level`. Each slot counts only with probability `rand & 15 ≤ 0x4a5720[level]`.
  A transition with `mask == 0`, `ctx ≠ 0` is mandatory (999999 points).
- Defense (`0x4256cd`): if the opponent attacks (kind 1), the AI computes the frame of its next hit.
  With probability `rand & 15 ≤ 0x4a5728[level]` and at a distance below 2.1, it picks a kind-2 (block) target with 5000 points,
  matching the height of the attack if possible.
- It switches only if the score of its own current move in that slot is lower. At most 3 steps per tick.
  In a neutral move (kind 0) it waits `8 − level` ticks before every step.
- Difficulty level: 0–7 (`0x4a587c`, default 4).

**Input bits**: 0x01 forward, 0x02 back, 0x04 down, 0x08 up, 0x10 punch, 0x20 kick.
**Context bits**: bit0 = the opponent is within punching range, bit1 = facing away from the opponent, bit2 = the opponent is facing away.

### Hit (`0x4491f7`, `0x448cbb`)
The body parts are **cubes around their joint point** (`FUN_004488ab`): the center is the translation of the body part's world matrix, the half edge length is
`fi%d[0x88 + body part] / 256` (typically 35/256 ≈ 0.14, the pelvis 50, the thorax 150). Not the mesh's bounding box!
The cubes of the attacking group are merged by `0x448a69`: a union in x and y, but in z both the lower AND the upper bound get the smaller
value (a bug of the original, which the port keeps). This is tested against the victim's cubes [Skull, LHumerus, RHumerus, LTibia, RTibia, Pelvis, LFemur, RFemur, Neck],
in order (the list is the same in all four defensive states). The zone is the Z of the center of the cube that was hit: < 0.4 low, < 1.0 mid, otherwise high.
Sweep: the box is also merged with the (unmerged) box of the move's previous check (`a8268` / `aa4c0`). The storage is written only from the second
check on, and StartMove zeroes it, so the move's second check is merged with a zero box (likewise as in the original).
Before the check the frame is set to the event's `fr`; afterwards the move's events do not run until a record that is 0 or ≥ 0xF1 (`cb540`).
After opcode 0xD4, the next attacking event is "continuous": the check is repeated every tick until it hits or the move ends.
Damage = `op × fi[0x40] × fi[0x44] × attack[attacker] × defense[victim]`, truncated (`0x441865`; the two `fi` members are 1.0).
The two multipliers are set before every round by `0x446690`: for a computer fighter from the difficulty level (attack `0x4a5738`:
1.0 1.11 1.12 1.12 1.15 1.15 1.2 1.2; defense `0x4a5758`: 1.0 1.0 0.95 0.9 0.9 0.9 0.85 0.8), between two humans the
attack is the handicap (`0x4a4030`: 0.7 … 1.4, in steps of 0.1; level 4 is 1.0) and the defense is 1.0; human against the computer
1.0 / 1.0. Knockback: 0.1 / 0.2 / 0.3 depending on the direction (`cond>>6`).
**Combo** (`0x448cbb`): if the victim was in a reaction move before the hit (`0x4a4ac0` flag table, StartMove writes into `cb4c8`),
the damage and the daze at the n-th combo hit are multiplied by `0x4aa470[n]` (0.2 / 0.15 / 0.1 / 0.05 / 0.02, then 0.01). On a clean hit n is reset to 0.
**Daze** (`cb3f8`): a clean hit adds `fi[0x58] × damage` points (3.0). Every tick `5 × fi[0x5c]` points drain away (2.0),
and the value is clamped to between 0 and 64 each tick. If after a hit it is above 75 and the victim is alive, the value is reset to zero,
and (if not lying down) the 0x68 "FLYING FALL 2 DAZE" move starts. Then GETTING UP DAZED → DAZE START → DAZE LOOP 1–3 → DAZE END.
**Breaking free** (events 0xCC / 0xCD / 0xCE): if the number of input changes so far (`cb460`) reaches `fi[0x60] / 2`, `fi[0x64]`,
or `fi[0x68]` (12 / 25 / 25), it switches immediately to the record's target. The counter increases on every tick in which the input
changes. StartMove zeroes it, unless the first record of the new move is also 0xCC–0xCE. The CPU does not move in this case,
but adds `0x4a5790[level]` to the counter every tick. There is a 0xCC at the start of DAZE LOOP 1 and 2: with persistent mashing, the further loops are skipped.
Attack types 1 and 2 (`stance >> 6`) rotate the entire fight line around the attacker by `−(damage << 7)` br_angle.
The reaction index is: `(attack type>>3) | zone<<5 | direction | (lethal ? 4 : 0)`, which selects the victim's next move from the 128-entry `REACT_*` tables.
Throw: the `cond == 8` event starts the victim from the **thrower's table** (`StartMove(p+2)`); the 0xCB terminating record is
"same player, the other table" (`StartMove(p^3)`); 0xE8 sets the distance between the two fighters (`target << 9`, 16.16,
+0.1 if the last throw was started by player 2: `0x4a8344 == 2`). When the throw starts (`0x441c0b`) the thrower is not
solid (`0x4a8260[thrower] = 0`), so the repulsion does not push back; without this the lifted fighter would hover in front of the thrower rather than
above his head.
The 0xC9 terminating record fires when the fighter lands.

Health: 120. The game speed is 48 ticks/s (PIT 1193182 / 24850), a round is 60 seconds.

## Fighter and arena parameters: `fighters/fi%d.cmp` ✅
RLE, then **subtract 1 from every byte**. 584 bytes. The n-th block is the data of the n-th fighter and the n-th arena at the same time.
| Offset | Field |
|---|---|
| 0x00 / 0x10 | fighter name / arena name (MAGNON/INFERNO, SHEBA/RHOMB, VENAM/PERES, JAKE/SENTRAL, KIKO/LUSK, SIREN/URSAE, ASHRAF/KARLAK, CYBEN/AXONE, RYGIL/ANARCHIS, LINNA, KWONDO) |
| 0x40, 0x44 | damage multipliers (16.16) |
| 0x4c | depth of the floor outside the ring (16.16, e.g. −0.6) |
| 0x54 | half size of the ring (float, 5.04; 2.52 in the practice arena) |
| 0x58 | daze multiplier (3.0) |
| 0x5c | daze decay (0.4; ×5 per tick) |
| 0x60, 0x64, 0x68 | input changes needed to break free, for event 0xCC (half) / 0xCD / 0xCE (25) |
| 0x6c, 0x70, 0x74 | jump arc: v0, g, scale |
| 0x78 + 4·class | body radius per move class (0.3 / 0.3 / 0.5 / 0.6) |
| 0x88 + body part | half edge length of the hit cube ×256 |

## Fight line and ring
The fight takes place along a line (`a8684` matrix, the two 1D positions: `a8280`/`a8284`, initially −1 / +1). The line rotates:
- **sidestep** (`0x43e424`): on a move change, the lateral root offset of the previous move rotates the line around the opponent by the angle `atan(x / distance)` (threshold: 0.02 rad);
- **swinging punches** (see above).

Jump and dash (`0x440f42`): 0xD0/D1/D2 are a jump in place/forward/backward (speed ±2 units / 32 ticks), 0xD5/D6/D7 are a dash toward the opponent (with speeds of the distance, +1, −1).
Repulsion (`0x43fbca`): if both are solid (`a8260`) and their "body height" (the averaged center of the thorax cube and the left radius cube) differs by less than 0.65,
it pushes them apart by 0.3 of the amount by which the distance is smaller than the sum of the radii, toward the side they were on before contact (`a8264`, not updated during contact).
If one is lying on the ground (class 3), the other one moves instead. If they have passed through each other, it pulls them toward the middle by half of the old distance.
Ring boundary (`0x4408f9`): the distance measured from the pelvis along the fight line, in both directions up to the walls at `half size + 1.3`, is at least 0.5
(1.8 if ring-out is disabled and the move did not release it with 0xDA, or if the opponent's pelvis has already been outside). It does not push during a throw.
Ring-out (`0x43f36e`): each tick only the fighter farther from the center can be checked (`0x445002`, with pelvis positions tracked with a dead band of 0.1).
The "pelvis outside" flag (`df3c8`: |x| or |y| > half size − 0.1) persists until the end of the round. A lying pelvis (z < 0.15) and the flag → RING OUT SLIDE HEAD / FEET (9/10);
a standing fighter with a foot that sticks out and is near the ground (z < 0.2) → RING OUT BACKWARDS / STEP (7/6). The first variant (9 and 7, respectively) is chosen
if the fighter is facing the opponent and the opponent's pelvis has not been outside, or is facing away and has been outside; otherwise the second. This loses the round.
Ground height (`cb498`): the same function sets the ground under the fighter on every tick, independently of ring-out: 0, or the arena's
lower level (`fi[0x4c]`, fall depth) if the pelvis (half size − 0.1) or either foot (half size) is outside; with ring-out disabled, 0.
The landing (`0x441245`) adjusts to this (height ≥ ground + 0.3). This way a fighter who fell or was KO'd beyond the edge falls down instead of hovering.

## Sound ✅
### `sound/*.asf` – 4-bit DPCM (decoder: `FUN_0044f7b1`, routines `0x44f7e6` / `0x44fb1e`)
```
0x00 "ASF\0"   0x10 name[8]   0x18 u32 block count   0x1c u32 samples/block (32)
0x24 u16 sample rate (22050 / 44100)   0x28 u32 flags (bit0 16 bit, bit1 stereo; all are 13 = 16-bit mono)
0x2c data: per block 1 control byte + 16 bytes (32 signed 4-bit deltas, high nibble first)
```
Control byte: upper 4 bits = shift, bit value 2 = prediction. The sample (`n` = nibble×16, `cl` = shift+2):
- bit2 = 0: `s = (previous·64 + (n << cl)) >> 6`
- bit2 = 1: `s = (2·previous·64 − before_previous·64 + (n << cl) + carry) >> 6`

The result is truncated to 16 bits, without clipping. There is an extra, unused block at the end of the file. 284 samples in total, approx. 375 s.

### `sound/*.fxl` – sound banks (`FUN_0043509b`)
A text list, one sample name per line, up to `end`; `;` = comment; `null` = empty. The prefix is the playback mode
(the entry's `+0x10` field): none = 0, `*` = 1 and `#` = 3 are speech – played from disk on a single channel, and if
that is busy, it queues in a 10-element queue (`FUN_0041142a`), so the announcements play one after another (e.g. after the selection
"name – versus – name"); `+` = 2 is a short sound through a small buffer ring (in the port it sounds like 0).
- Bank 0: `generic` (announcer: 1 "FIGHT", 2 victory, 3 defeat, 4 time, 7 flawless, 14/15 P1/P2 won, 16 sudden death, 17 countdown, 19 teleport);
  in the tournament `tour` (the same, plus 20–30 the fighters' names in order 1–11, 31 "wins")
- Bank 1/2: the fighter's name (`magnon.fxl`, …, 45 entries)
- arena bank: `b%d.fxl` (4 entries: step, thud, landing, stomp)

Playback (`FUN_00435eb4(bank, index)`): the move's field 0x18 in `StartMove`, or the `target` field of opcode 0xDE.
< 0x80: the player's own bank (1-based), 0x80–0xEF: the arena bank (0-based), ≥ 0xF0: no sound.
The hit sounds come from the sound field of the reaction moves (e.g. HIT FACE L = 1 = `mag_l5`).

### `*.slb` – sprite effect libraries ✅
These are not sound parameters: they are visual effects (`Sprlib.c`, `Sprplay.c`, `Sfxroot.c`, `Effects.c`; "sfx" here means special effects).
The port: `src/sprfx.c`. Header: `u32 version = 1, u32 npix`, then three arrays (`u32 count, u32 element size`): commands
(0x24 bytes), emitters (0x44 bytes) and effect entries (0x20 bytes: `u32 id 0..255, u32 first command, char name[24]`),
and finally `npix` pixelmaps: a 0x18-byte header (`s16 row length, u8 type = 3 (8-bit indexed), u8, u16 base x/y, u16 width/height,
s16 origin x/y, u32 compressed size, u32 unpacked size`), followed by the PackBits variant of `0x42e5c9`: `c = ~(int8)b`; `c > 0`: `c+1`
literal bytes, `c == -128`: end, otherwise the next byte `1-c` times; every stored value is the pixel + 1. Index 0 is transparent.

**Commands** (9 dwords, interpreter `0x44d868`, the time unit is the fight tick, 48 Hz):
| type | meaning |
|---|---|
| 1 | frame: `[1]` pixelmap, `[2]` float time (ticks), `[3..8]` 16.16 2×3 matrix (the position and size of the quad in the camera plane) |
| 2 | jump to `[1]` if `[2] == -1` |
| 3 | end: the sprite ceases to exist |
| 4 | emitter `[1]` on every bone of bone group `[2]`, `[3..5]` offset, `[6]` particle count |
| 5 | child sprite: `[1]` start command, `[2]` bone group, `[3..5]` offset, `[6]` kind, `[7]` pull toward the camera (16.16) |

Bone group: a row of the `0x4aa510` table (`u8 count, bones`; rows 0..15 are the individual body parts, rows 16..63 are groups,
the last one is all 16). `-1` = the parent's group without offset, `0x10` = the projectile (if there is one), `-2` = the fighter's foot point (`0x4a8690`).
Child sprite kinds: 0 = fixed where the bone is now (`0x447f4a`); 1 = follows the bone (`0x4163d0`); 2–4 = follows the bone
with an offset, and rotates relative to the bone (`0x43c2f0`: the first term of the Euler angle of the camera→bone matrix, for 2
`0x7ff8 - angle`, order 2 / 5 / 4).
**Emitter** (17 dwords, `0x43bbfa`, particle `0x43ba41`, update `0x43bec1`): `[0]` the particle's script, `[1]` position spread,
`[2]` velocity mode (0 = toward the opponent's bone `[3]` with offset `[4..6]`, with speed `[7]`; 1 = the same for the own bone;
2 = `[3..5]` constant; 3 = `[3..5]` with the bone's matrix, in the original together with the offset; 4 = like 2, but on the ground, z = 0),
`[8]` velocity spread, `[9,10]` gravity (z, with spread), `[11,12]` bounce (0 = disappears on the ground), `[13,14]` lifetime
(16.16 ticks, `-1.0` = infinite), `[15,16]` unused. Spread: `v + ((rand() - 0x3fff) * 16) % s`. Per step,
`pos += v·dt` and `v += g·dt` (the `dt/2` term is an integer division, 0 for whole ticks).
**Drawing** (`0x4480c7`, `BrSprite` model `0x4483aa`): unit quad, UV 0..0.97, the frame's matrix × rotation in the
camera plane (billboard), the position pulled toward the camera by `[7]`. Z-buffered, texel 0 is transparent, no lighting.
At most 256 sprites (`0x44e890`).

**Libraries** (`0x433350`, table `0x4a6b10`): generic and teleport always, `Arena%02d` (only arenas 2 and 6 exist, the water arenas),
and those of the two fighters (`1 << fighter`, Magnon = 1 … Kwondo = 11). Per player an `[id]` table (`0x4bd9a8`): a later
library overwrites the id if it is empty, or if the library belongs to that player's fighter or to the arena – this is how the fighter's
own blood takes the place of the generic one.
**Triggers** (`0x44c666` queues them, at most 4 per frame; `0x44c851` / `0x44cba0` runs them, with dt = 0):
- hit (`0x448cbb`): on the body part that was hit (`c6f00`) blood `0x17 + direction` (Blood 1–4) if the reaction is damage, otherwise
  block sparks `0x13 + direction` (Block 1–4); the direction is bits `cond >> 6` of the attacker's event;
- move event `0xe1`: id = the record's `tgt`, bone group = `cond & 0x3f` (Kwondo charge, muzzle flash;
  Cyben jets, rotor; the water arenas' Step / Land / Splash effects);
- start of the match (`0x44cd7c` → `0x44cdd5`): teleport (`0x2d`) on both fighters' foot points; in endurance only on the newcomer;
  the teleporting fighter is invisible (`0x444ea6`: the body-part actors' type is 0, `0x4a8828 = 1`) until `0x1fd999` PIT ticks
  (approx. 1.75 s of real time, 84 ticks at 48 Hz) have elapsed since the start of the round
  (`0x4c7270`, `0x4464c2`) (`0x444f54`) – at that point the light beam is at its widest;
- projectiles (`0x43cdb2`, `Sfxproj.c`: own hit check with `0x4491f7`) would be launched from event 0xd3 and from the 0xf0–0xf7 move
  sounds, but **none of the Turbo's move tables use them** – dead code, the port does not implement it.
The id 43 of Rygil's (9) moves is not present in any library, and in the original it draws nothing either.
`FX_SPRTRACE=1` prints the requested effects.

Sound playback (`Windsoun.c`): no spatial effect. `0x435eb4(bank, index)` gets no position, SetPan is only 0, there is no
SetFrequency, no looping. 8 round-robin buffers (at 22 500 Hz, about 2 % higher), one separate buffer for the long sounds
(22 050 Hz), 2 for the `+` sounds (44 100 Hz). Volume is only the master volume; the one exception is the countdown
beep (`0x422399`), which does not change at a master volume of 0 dB.

### Music
- Music: CD audio tracks (MCI, `FUN_0043adf8`). During a fight, track `arena + 2` plays (`FUN_0043b077(a8308 + 2)`), at the credits track 2, in demo mode track 14.
  Track 1 is the CD's data track (MODE1/2352). The image is a single BIN file, and the audio in it is **big-endian** 16-bit stereo 44.1 kHz.

## Videos: `fight/video/*.brp` ✅
They are on the CD (`FIGHT/VIDEO`; the installed `FIGHT\VIDEO\WIN.BRP` is actually the BRender logo), and `tools/rip_video.py`
copies them out. Player: `0x42aad0` (`Video.c`), demuxer `0x419900` / `0x419bae`. The port: `src/video.c`, checker
tool: `tests/brpcheck.c` (pixel- and sample-identical to the Python reference).
```
'BRPP', u32 nstreams, u32 0x4b000
nstreams × { fourcc 'BVID' | 'BASF', u32 index, u32 length (ms), u32 max chunk, u32 extra length, extra }
  BVID extra: u32 frames, u32 width (320), u32 height (200 / 180 / 150), u32 depth (24)
  BASF extra: 'ASF\0', u16 1, u16 2, u32 number of audio chunks, u32 0x18, char name[8]
chunks up to index 0xFFFFFFFF: { u32 stream, u32 timestamp (ms), u32 size, data }
```
Order: the audio chunk(s) (in FXINTRO, pieces of approx. 0.5 s interleaved), one `PAL8` (not used by the game), then
one `MAD1` per frame. The frames come in timestamp order (15 frames/s; the credits clips run at 13, ARGLOGO at 30).
**MAD1** (24-bit branch, `0x42ba80`): opcodes up to 0xFF, in place, into a persistent frame buffer; the colors are R, G, B.
8 = key frame (clear, 12×12 solid-color blocks); 10 = save the frame as "previous"; 11 / 13 = B×B blocks with 2-bit codes
(skip / motion vector / solid color / vector difference; the source is the previous or the current frame, respectively); 12 = per-column 4×4
blocks with per-pixel codes (left, top, offset tables A/B, literal color); 7 = list of block moves.
The `.CLT` (RGB555 → palette index) and the `.PAL` are only needed for an 8-bit screen, not for the port.
**Audio:** every chunk is an `.asf` file without its first 0x18 bytes: stereo, 22050 Hz, 34-byte blocks (left, right:
control byte + 16 bytes); the DPCM is that of the sound effects, and the predictor continues per channel across blocks and chunks.
Esc also skips the other videos of the sequence (`0x4a5cf0`), any other key only the current one. At startup: ARGLOGO, BRENDER, FXLOGO,
FXINTRO (`0x44427a`; in this FIGHT.EXE every video path is rewritten to `win.brp`, the names come from the CD);
at the end of the ladder WIN (`0x413f2d`), after a 1P game over LOSE (`0x413e9d`, not with Rygil).

## Miscellaneous
- `FIGHT/VIDEO/*.CLT/*.PAL/*.BRP`: FMVs (Argonaut format), ❌
- `boutgfx/*.pix`: HUD graphics (FIGHT, KO, ROUND, …), BRender pixmaps, ✅ readable
- `boutgfx/colmap.tab`: a 64-row × 256-column shade table (BRender pixelmap "shade_table"); row 48 passes the indices
  10..137 through unchanged, below it darkens toward black (0x0A) (≈ row/48), above it whitens toward color 137.
- `frontend/shade.tab`: the menu's shade table.

### Shading (BRender index lighting)
The renderer is not in `FIGHT.EXE` but in the BRender 1.2 drivers (`DLL\SOFTRNDX.BDD`, `SOFTPRIM.BDD`,
`SOFTIZ.BDD`). The fight's only light (`0x42f6fd`) is a directional light from world +Z (from above), with strength 1/c, where c
comes from the `0x4a4470[arena]` table: 0.9, and −0.6 in the 0th, 8th and 9th arenas (there the light effectively comes from below).
There is no ambient light, only the material's ka. Per vertex (Gouraud) or per face:
`l = ka + (d > 0 ? kd·d + spec : 0)`, `d = s·(n·L)`, `spec = h > 0.6172 ? h·ks·s / (p − p·h + h) : 0`,
`h = n·normalize(L + eye)`, `l` clamped to the range [0, 1). Without a texture: `palette[base + 0.5 + range·l]`;
with a lit texture: `colmap[0.5 + 63·l][texel]`; with an unlit texture the raw texel.
Fighter materials (`0x41c6c0`): mode = texture·2 | Gouraud, ka/kd/ks from the `0x4a4508` table (flat: .3/.3/.5, flat
textured: .4/.3/.5, Gouraud textured: .5/.2/.1, p = 20). In textured mode the TX materials are unlit, in the Gouraud
case TXBANK1–8 and BANK1–8 are lit and smoothed (TXFBANK/FBANK are always flat); without a texture every material is
its bank's ramp (10 + 16(n−1), 15 steps). Floor (`0x41cdcd`): with a texture the TXF/TXGFLOOR raw texel; without one,
TXGFLOOR .35/.30/.99 smoothed, TXFFLOOR .15/.45/.15 flat, 10-step ramp. The port computes this on the CPU (OpenGL
lighting is turned off); it modulates the lit texture with a gray of `min(row/48, 1)`, then blends it toward `palette[137]`
by the ratio `clamp((row − 48)/12, 0, 1)` (rows 49–59 of the table approximate this, rows 60–63
are already all 137).
