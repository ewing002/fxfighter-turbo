/*
 * FX Fighter Turbo - open reimplementation
 * Sprite effects (fight/sfx/<name>.slb): block sparks, blood, special move effects, arena splashes,
 * the round start teleport. FIGHT.EXE: Sprlib.c (libraries, 0x41fc97 / 0x433350 / 0x433630),
 * Sprplay.c (script player 0x44d868), Sfxroot.c (sprite list 0x44e9ea), Effects.c (requests
 * 0x44c666 / 0x44c851 / 0x44cba0), the sprite classes 0x447f4a (fixed), 0x4163d0 (follows a bone),
 * 0x43c2f0 (bone + offset, turned with the bone) and 0x43b9f0 (particle, emitter 0x43bbfa).
 */
#ifndef FX_SPRFX_H
#define FX_SPRFX_H

#include "assets.h"
#include "fight.h"

/* loads the libraries for this match (0x433350: generic, teleport, the arena's and both fighters')
 * and uploads their pixelmaps with the fight palette; needs a GL context */
void sprfx_load(const Pac *pac, int fighter1, int fighter2, int arena, const Rgb *pal);
void sprfx_free(void);
void sprfx_clear(void);                                 /* removes every sprite (0x44ea75) */
/* starts effect `id` (0..255) of `player` on bone group `group` (0..63, row of 0x4aa510;
 * -2 = the fighter's floor position), as 0x44cba0 does */
void sprfx_trigger(const Fight *f, int player, int group, int id);
/* advances every sprite by dt fight ticks (0x44e9ea); eye / dir: the camera */
void sprfx_update(const Fight *f, float dt, const float eye[3], const float dir[3]);
/* draws the sprites as camera facing quads into the current GL view (world coordinates) */
void sprfx_draw(const float eye[3], const float dir[3]);
int  sprfx_count(void);
/* name of an effect id as the libraries call it ("Block 1", "Blood 3"...), NULL if none */
const char *sprfx_name(int id, int player);

#endif
