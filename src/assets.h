/*
 * FX Fighter Turbo - open reimplementation
 * Asset formats (reverse engineered from FIGHT.EXE / FIGHT.PAC, see docs/FORMATS.md)
 */
#ifndef FX_ASSETS_H
#define FX_ASSETS_H

#include <stdint.h>
#include <stddef.h>

/* ---------- FIGHT.PAC archive ---------- */
typedef struct {
    char     name[80];
    uint32_t offset;
    uint32_t size;
} PacEntry;

typedef struct {
    uint8_t  *data;
    size_t    size;
    PacEntry *entries;
    int       count;
} Pac;

int  pac_open(Pac *pac, const char *path);
void pac_close(Pac *pac);
/* Name lookup is case-insensitive and treats '/' and '\' the same.
 * Returns pointer into the archive (do not free) or NULL. */
const uint8_t *pac_get(const Pac *pac, const char *name, size_t *size);

/* ---------- .CMP run-length images ---------- */
/* Returns malloc'd buffer of *out_size bytes, or NULL. */
uint8_t *cmp_decode(const uint8_t *src, size_t src_size, size_t *out_size);

/* ---------- Palettes ---------- */
typedef struct { uint8_t r, g, b; } Rgb;
/* Reads a BRender RGBX_888 256x1 pixelmap (bgrounds/picpN.cmp). */
int pal_load_pixelmap(const uint8_t *src, size_t size, Rgb pal[256]);

/* ---------- BRender pixelmaps (.pix) ---------- */
typedef struct {
    char     name[64];
    int      type, width, height, row_bytes;
    uint8_t *pixels;       /* malloc'd, row_bytes*height */
    int      has_pal;      /* palette attached with ADD_MAP (frontend images) */
    Rgb      pal[256];
} Pixmap;
int  pix_load_all(const uint8_t *src, size_t size, Pixmap *out, int max);
void pix_free(Pixmap *p);

/* ---------- BRender models (.dat) ---------- */
typedef struct { float x, y, z; } Vec3;
typedef struct { float u, v; } Vec2;
typedef struct { uint16_t v[3]; uint16_t smooth; uint8_t flags; uint16_t mat; } Face;

typedef struct {
    char   name[32];
    int    nverts, nfaces, nmats;
    Vec3  *verts;
    Vec2  *uvs;          /* may be NULL */
    Face  *faces;
    char (*mats)[32];    /* material names, face.mat is 1-based (0 = none) */
} Model;

int  model_load_all(const uint8_t *src, size_t size, Model *out, int max);
void model_free(Model *m);

/* ---------- BRender materials (.mat) ---------- */
typedef struct {
    char    name[32];
    uint8_t r, g, b, opacity;
    float   ka, kd, ks, power;
    uint16_t flags;
    uint8_t index_base, index_range;   /* palette bank: BANKn -> 10+16*(n-1), 15 */
    char    colour_map[32];
} Material;
int material_load_all(const uint8_t *src, size_t size, Material *out, int max);
const Material *material_find(const Material *m, int n, const char *name);

/* ---------- Fighter animations (.ani / .an4) ---------- */
#define ANI_MAX_BONES 16
typedef struct {
    int32_t root[3];                 /* x, height, forward  (units: *406/65536) */
    uint8_t bone[ANI_MAX_BONES][6];  /* 3x angle byte, 3x signed translation byte */
} AniFrame;

typedef struct {
    char      name[17];
    int       nframes;
    AniFrame *frames;
} AniClip;

typedef struct {
    int      nbones, nclips;
    AniClip *clips;
    AniFrame *frame_pool;
} AniSet;

int  ani_load(const uint8_t *src, size_t size, AniSet *out);
void ani_free(AniSet *a);

/* Builds the 4x3 row-vector BRender matrix for one bone of a frame.
 * Result layout: m[row][col], rows 0..2 rotation, row 3 translation. */
void ani_bone_matrix(const AniFrame *f, int bone, float m[4][3]);
#define ANI_UNIT (406.0f / 65536.0f)

#endif
