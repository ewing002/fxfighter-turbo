/*
 * FX Fighter Turbo - open reimplementation
 * 2D sprites of the fight: messages and panels (boutgfx/<name>.pix), drawn in a 640x480 virtual
 * screen. The menus are the 3D front end (fe3d*.c).
 */
#ifndef FX_UI_H
#define FX_UI_H
#include <SDL_opengl.h>
#include "assets.h"

typedef struct { GLuint tex; int w, h; float u1, v1; int ok; } Sprite;

void ui_load_bout(const Pac *pac, const Rgb *arena_pal);   /* boutgfx sprites use the arena palette */
const Sprite *ui_bout(const char *name);                   /* boutgfx/<name>.pix */

void ui_begin(int win_w, int win_h);                       /* 640x480 virtual, letterboxed */
void ui_end(void);
void ui_draw(const Sprite *s, float x, float y, float scale, float alpha);
void ui_draw_c(const Sprite *s, float cx, float cy, float scale, float alpha);
void ui_draw_part(const Sprite *s, int sx0, int sx1, float x, float y, float scale, float alpha);
void ui_rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a);
int  ui_timer_digit_x(int digit, int *x0, int *x1);        /* glyph columns in timer.pix */
int  ui_round_digit_x(int digit, int *x0, int *x1);        /* glyph columns in rndnumbr.pix */
#endif
