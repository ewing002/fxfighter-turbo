/*
 * FX Fighter Turbo - open reimplementation
 * 3D front end renderer (OpenGL 1.x): the three views of FUN_004135f1, see docs/FRONTEND.md
 */
#ifndef FX_FE3D_GL_H
#define FX_FE3D_GL_H

#include "fe3d.h"

/* One actor of the front end scene. Units as BRender sees them: position in world units
 * (strat units / 1000), rotation in degrees, uniform scale (strat 256 = 1.0). The matrix is
 * Scale (pre) * RotY * RotX * RotZ, then the translation, then the parent's matrix. */
typedef struct {
    int   type;          /* model type (index into the 0x4a2948 table), -1 = no model */
    float pos[3], rot[3], scale;
    int   parent;        /* index of the parent actor, -1 = world root */
    int   world;         /* 0 main, 1 right panel, 2 left panel */
    int   hidden;
} FeNode;

#define FE_MAX_NODES 512
typedef struct {
    FeNode n[FE_MAX_NODES];
    int    count;
} FeScene;

void fe_gl_init(const FeAssets *fe);     /* uploads the textures (needs a GL context) */
void fe_gl_free(void);
/* draws back1.pix, the right and left panel worlds and the main world into the 640x480
 * area letterboxed in a w x h window */
void fe_gl_render(const FeAssets *fe, const FeScene *sc, int w, int h);

#endif
