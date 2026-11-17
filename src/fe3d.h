/*
 * FX Fighter Turbo - open reimplementation
 * 3D front end (winfront.c in the original), see docs/FRONTEND.md.
 * This part: the assets of the front end scene and the script bytecode, which is read at run
 * time from the user's own FIGHT.EXE (no original code is stored in the repository).
 */
#ifndef FX_FE3D_H
#define FX_FE3D_H

#include "assets.h"

/* .data section of FIGHT.EXE mapped at its virtual addresses (0x499000..) */
typedef struct {
    uint32_t base, size;   /* virtual address and size (virtual size, zero filled) */
    uint8_t *data;
} ExeData;

int  exe_data_load(ExeData *x, const char *path);
void exe_data_free(ExeData *x);
/* pointer to n bytes at virtual address va, or NULL when outside the section */
const uint8_t *exe_ptr(const ExeData *x, uint32_t va, uint32_t n);
uint32_t exe_u32(const ExeData *x, uint32_t va);

#define FE_MAX_MODELS   256
#define FE_MAX_MATS     300
#define FE_MAX_MAPS     256
#define FE_NUM_STRATS   93          /* 0x49fb88 */
#define FE_NUM_TYPES    212         /* 0x4a2948 */
#define FE_NUM_OPS      238         /* 0x4a0048 */

typedef struct {
    /* scene assets (fight/frontend) */
    Model    models[FE_MAX_MODELS]; int nmodels;   /* crshipl1.dat */
    Material mats[FE_MAX_MATS];     int nmats;     /* demo.mat */
    Pixmap   maps[FE_MAX_MAPS];     int nmaps;     /* demo.pix (texture registry) */
    Rgb      pal[256];                             /* demo.pal: the 8-bit screen palette */
    Pixmap   back;                                 /* back1.pix: 640x480 frame art */
    int      mat_map[FE_MAX_MATS];                 /* material -> maps[] index, -1 = untextured */
    /* script data from FIGHT.EXE */
    ExeData  exe;
    uint32_t strat[FE_NUM_STRATS];                 /* start address of each script */
    char     type_name[FE_NUM_TYPES][32];          /* model type -> model name */
    int      type_model[FE_NUM_TYPES];             /* model type -> models[] index, -1 = none */
} FeAssets;

/* exe_path: the original FIGHT.EXE (usually next to FIGHT.PAC). Returns 1 on success;
 * on failure *err (if given) gets a short reason. */
int  fe_assets_load(FeAssets *fe, const Pac *pac, const char *exe_path, const char **err);
void fe_assets_free(FeAssets *fe);
const Model *fe_type_model(const FeAssets *fe, int type);

#endif
