/*
 * FX Fighter Turbo - open reimplementation
 * Sprite effects (fight/sfx/<name>.slb), see sprfx.h and docs/FORMATS.md (".slb").
 *
 * A library holds scripts (9 dword commands), particle emitters (17 dwords), effect entries
 * (id -> first command) and 8 bit pixelmaps. An effect request runs the entry's script at once
 * with a temporary player (0x44cba0, dt 0): its SPAWN / EMIT commands create the sprites, which
 * then run their own scripts every fight tick. A sprite is a unit quad (the "BrSprite" model,
 * 0x4483aa) placed with the frame's 2D matrix in the camera plane (0x4480c7), optionally turned
 * and pulled toward the camera.
 */
#include <SDL.h>
#include <SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sprfx.h"

#define NLIB    25
#define MAX_SPR 256            /* gEffectPool: 0x100 objects (0x44e890) */
#define FX1     (1.0f / 65536.0f)

typedef struct { int w, h; uint8_t *px; GLuint tex; } SPix;
typedef struct {
    int       loaded;
    uint32_t  mask;
    int       ncmd, nemit, nfx, npix;
    int32_t (*cmd)[9];
    int32_t (*emit)[17];
    struct { int id, start; char name[25]; } *fx;
    SPix     *pix;
} SLib;

/* 0x4a6b10: {path, mask}: generic and teleport always, Arena%02d with 1 << (16 + arena),
 * the fighters with 1 << fighter id (Magnon = 1 ... Kwondo = 11) */
static const struct { const char *path; uint32_t mask; } LIBS[NLIB] = {
    { "fight/sfx/generic.slb", 0xffffffffu }, { "fight/sfx/teleport.slb", 0xffffffffu },
    { "fight/sfx/arena00.slb", 1u << 16 }, { "fight/sfx/arena01.slb", 1u << 17 },
    { "fight/sfx/arena02.slb", 1u << 18 }, { "fight/sfx/arena03.slb", 1u << 19 },
    { "fight/sfx/arena04.slb", 1u << 20 }, { "fight/sfx/arena05.slb", 1u << 21 },
    { "fight/sfx/arena06.slb", 1u << 22 }, { "fight/sfx/arena07.slb", 1u << 23 },
    { "fight/sfx/arena08.slb", 1u << 24 }, { "fight/sfx/arena09.slb", 1u << 25 },
    { "fight/sfx/arena10.slb", 1u << 26 }, { "fight/sfx/arena11.slb", 1u << 27 },
    { "fight/sfx/magnon.slb", 1u << 1 }, { "fight/sfx/sheba.slb", 1u << 2 },
    { "fight/sfx/venam.slb", 1u << 3 },  { "fight/sfx/jake.slb", 1u << 4 },
    { "fight/sfx/kiko.slb", 1u << 5 },   { "fight/sfx/siren.slb", 1u << 6 },
    { "fight/sfx/ashraf.slb", 1u << 7 }, { "fight/sfx/cyben.slb", 1u << 8 },
    { "fight/sfx/rygil.slb", 1u << 9 },  { "fight/sfx/leena.slb", 1u << 10 },
    { "fight/sfx/kwondo.slb", 1u << 11 },
};

/* 0x4aa510: bone groups {count, bones...}; rows 0..15 are the single body parts */
static const uint8_t GROUPS[64][17] = {
    {1,0},{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},{1,7},{1,8},{1,9},{1,10},{1,11},{1,12},{1,13},{1,14},{1,15},
    {2,10,12},{2,12,11},{3,10,12,11},{2,2,4},{2,4,3},{3,2,4,3},{2,10,2},{4,10,2,12,4},
    {4,12,4,11,3},{6,10,2,12,4,11,3},{2,9,13},{2,13,8},{3,9,13,8},{2,1,5},{2,5,0},{3,1,5,0},
    {2,9,1},{4,9,1,13,5},{4,13,5,8,0},{6,9,1,13,5,8,0},{2,10,9},{4,10,12,9,13},{4,12,11,13,8},{6,10,12,11,9,13,8},
    {2,2,1},{4,2,4,1,5},{4,4,3,5,0},{6,2,4,3,1,5,0},{2,10,1},{4,10,12,1,5},{4,12,11,5,0},{6,10,12,11,1,5,0},
    {2,2,9},{4,2,4,9,13},{4,4,3,13,8},{6,2,4,3,9,13,8},{3,7,8,0},{5,7,13,8,5,0},{7,7,9,13,8,1,5,0},{2,7,15},
    {3,7,15,14},{2,15,14},{3,15,11,3},{5,15,12,4,11,3},{7,15,10,2,12,4,11,3},{4,15,14,11,3},{6,15,14,12,4,11,3},
    {16,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
};

static SLib g_lib[NLIB];
static struct { SLib *lib; int start; } g_slot[256][2];   /* 0x4bd9a8: [id][player] */

/* sprite classes */
enum { C_FIXED, C_FOLLOW, C_BONE, C_PARTICLE };
typedef struct {
    int     cls, euler;             /* C_BONE: 2 / 3 / 4 = spawn kind (euler order 2, 5, 4) */
    /* Sprplay instance (+0x18 of the sprite) */
    SLib   *lib;
    int     pc, frame;              /* frame: pixelmap index + 1, 0 = none yet */
    float   timer;
    int32_t m[6];                   /* 16.16 2x3 matrix of the frame */
    int     player, group;
    /* sprite */
    float   pos[3], extra, angle;   /* extra: pulled this far toward the camera (+0x5c) */
    int     bone; float off[3];
    float   vel[3], acc[3];         /* particles */
    int32_t bounce, life;
} Spr;
static Spr g_spr[MAX_SPR];
static int g_nspr;
static uint32_t g_rand = 1;

static int ms_rand(void) { g_rand = g_rand * 214013u + 2531011u; return (int)((g_rand >> 16) & 0x7fff); }
/* 0x43bb2d: v + ((rand - 0x3fff) * 16) % spread */
static int32_t rnd(int32_t v, int32_t spread) { return spread ? v + ((ms_rand() - 0x3fff) * 16) % spread : v; }

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int rd16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static void lib_free(SLib *l)
{
    for (int i = 0; i < l->npix; i++) {
        if (l->pix[i].tex) glDeleteTextures(1, &l->pix[i].tex);
        free(l->pix[i].px);
    }
    free(l->cmd); free(l->emit); free(l->fx); free(l->pix);
    memset(l, 0, sizeof *l);
}

/* 0x42e5c9: PackBits with a bias, every stored value is pixel + 1 */
static int unpack(const uint8_t *s, size_t n, uint8_t *out, int want)
{
    size_t i = 0; int o = 0;
    while (o < want && i < n) {
        int c = ~(int)(int8_t)s[i++];
        if (c > 0) {
            if (o + c + 1 > want || i + (size_t)c + 1 > n) return 0;
            for (int k = 0; k <= c; k++) out[o++] = (uint8_t)(s[i++] - 1);
        } else if (c == -128) break;
        else {
            if (o + 1 - c > want || i >= n) return 0;
            uint8_t v = (uint8_t)(s[i++] - 1);
            for (int k = 0; k < 1 - c; k++) out[o++] = v;
        }
    }
    return o == want;
}

/* 0x41fc97 */
static int lib_parse(SLib *l, const uint8_t *d, size_t n)
{
    size_t o = 8;
    if (n < 8 || rd32(d) != 1) return 0;
    int npix = (int)rd32(d + 4);
    static const int ES[3] = { 0x24, 0x44, 0x20 };
    int cnt[3]; size_t at[3];
    for (int a = 0; a < 3; a++) {
        if (o + 8 > n) return 0;
        cnt[a] = (int)rd32(d + o);
        if (rd32(d + o + 4) != (uint32_t)ES[a] || cnt[a] < 0 || cnt[a] > 4096) return 0;
        at[a] = o + 8;
        o = at[a] + (size_t)cnt[a] * ES[a];
        if (o > n) return 0;
    }
    if (npix < 0 || npix > 1024) return 0;
    l->ncmd = cnt[0]; l->nemit = cnt[1]; l->nfx = cnt[2];
    l->cmd = calloc((size_t)cnt[0] + 1, sizeof *l->cmd);
    l->emit = calloc((size_t)cnt[1] + 1, sizeof *l->emit);
    l->fx = calloc((size_t)cnt[2] + 1, sizeof *l->fx);
    l->pix = calloc((size_t)npix + 1, sizeof *l->pix);
    for (int i = 0; i < cnt[0]; i++) for (int k = 0; k < 9; k++) l->cmd[i][k] = (int32_t)rd32(d + at[0] + i * 0x24 + k * 4);
    for (int i = 0; i < cnt[1]; i++) for (int k = 0; k < 17; k++) l->emit[i][k] = (int32_t)rd32(d + at[1] + i * 0x44 + k * 4);
    for (int i = 0; i < cnt[2]; i++) {
        const uint8_t *e = d + at[2] + i * 0x20;
        l->fx[i].id = (int)rd32(e); l->fx[i].start = (int)rd32(e + 4);
        memcpy(l->fx[i].name, e + 8, 24); l->fx[i].name[24] = 0;
    }
    /* pixelmaps (0x43a050): s16 row bytes, u8 type, u8, u16 base x / y, u16 w / h, s16 origin x / y,
     * u32 packed size, u32 unpacked size, packed data */
    for (int i = 0; i < npix; i++) {
        if (o + 0x18 > n) return 0;
        const uint8_t *h = d + o;
        int rb = rd16s(h), w = h[8] | h[9] << 8, ht = h[10] | h[11] << 8;
        uint32_t ps = rd32(h + 0x10), us = rd32(h + 0x14);
        o += 0x18;
        if (o + ps > n || w <= 0 || ht <= 0 || us < (uint32_t)(w * ht) || us > (1u << 20)) return 0;
        uint8_t *raw = calloc(us, 1), *px = malloc((size_t)w * ht);
        if (!unpack(d + o, ps, raw, (int)us)) { free(raw); free(px); return 0; }
        int arb = rb < 0 ? -rb : rb;
        for (int y = 0; y < ht; y++) {                        /* negative row bytes: stored bottom up */
            int sy = rb < 0 ? ht - 1 - y : y;
            memcpy(px + (size_t)y * w, raw + (size_t)sy * arb, (size_t)w);
        }
        free(raw);
        l->pix[i].w = w; l->pix[i].h = ht; l->pix[i].px = px;
        l->npix = i + 1;
        o += ps;
    }
    return 1;
}

static GLuint upload(const SPix *p, const Rgb *pal)
{
    uint8_t *rgba = malloc((size_t)p->w * p->h * 4);
    for (int i = 0; i < p->w * p->h; i++) {
        Rgb c = pal[p->px[i]];
        rgba[i * 4] = c.r; rgba[i * 4 + 1] = c.g; rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = p->px[i] ? 255 : 0;                /* index 0 is transparent */
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p->w, p->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return t;
}

void sprfx_clear(void) { g_nspr = 0; }

void sprfx_free(void)
{
    sprfx_clear();
    for (int i = 0; i < NLIB; i++) lib_free(&g_lib[i]);
    memset(g_slot, 0, sizeof g_slot);
}

/* 0x433350 */
void sprfx_load(const Pac *pac, int f1, int f2, int arena, const Rgb *pal)
{
    sprfx_free();
    uint32_t am = arena >= 0 && arena < 12 ? 1u << (16 + arena) : 0;
    uint32_t m1 = f1 >= 0 && f1 < 16 ? 1u << f1 : 0, m2 = f2 >= 0 && f2 < 16 ? 1u << f2 : 0;
    for (int i = 0; i < NLIB; i++) {
        if (!(LIBS[i].mask & (am | m1 | m2))) continue;
        size_t sz;
        const uint8_t *d = pac_get(pac, LIBS[i].path, &sz);
        if (!d) continue;                                   /* only arena 2 and 6 have a library */
        SLib *l = &g_lib[i];
        if (!lib_parse(l, d, sz)) { fprintf(stderr, "sprfx: bad %s\n", LIBS[i].path); lib_free(l); continue; }
        l->loaded = 1; l->mask = LIBS[i].mask;
        for (int k = 0; k < l->npix; k++) l->pix[k].tex = upload(&l->pix[k], pal);
        /* a later library takes an id over when its slot is empty or it belongs to that player's
         * fighter or to the arena: fighter blood replaces the generic one per player */
        for (int k = 0; k < l->nfx; k++) {
            int id = l->fx[k].id & 255;
            for (int p = 0; p < 2; p++)
                if (!g_slot[id][p].lib || (l->mask & (am | (p ? m2 : m1)))) {
                    g_slot[id][p].lib = l; g_slot[id][p].start = l->fx[k].start;
                }
        }
    }
    for (int k = 0; k < 4; k++)                              /* 0xc8..0xcb: the other player's blood */
        for (int p = 0; p < 2; p++) g_slot[200 + k][p] = g_slot[0x17 + k][p ^ 1];
}

const char *sprfx_name(int id, int player)
{
    if (id < 0 || id > 255) return NULL;
    SLib *l = g_slot[id][player & 1].lib;
    if (!l) return NULL;
    for (int k = 0; k < l->nfx; k++) if (l->fx[k].start == g_slot[id][player & 1].start) return l->fx[k].name;
    return NULL;
}

int sprfx_count(void) { return g_nspr; }

/* ------------------------------------------------------------------ */
static const Fight *g_f;                                    /* fight being drawn (bone positions) */

static void bone_m(int player, int bone, float m[4][3])
{
    if (g_f && bone >= 0 && bone < 16) fight_bone(g_f, player & 1, bone, m);
    else { memset(m, 0, sizeof(float) * 12); m[0][0] = m[1][1] = m[2][2] = 1; }
}
static void apply_p(const float m[4][3], const float v[3], float out[3])   /* BrMatrix34ApplyP */
{
    float r[3];
    for (int k = 0; k < 3; k++) r[k] = v[0] * m[0][k] + v[1] * m[1][k] + v[2] * m[2][k] + m[3][k];
    memcpy(out, r, sizeof r);
}
/* a8690: the fighter's floor position (z = the fall depth after a ring-out) */
static void root_pos(int player, float out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (!g_f) return;
    float yaw;
    fight_place(g_f, player & 1, &out[0], &out[1], &out[2], &yaw);
    out[2] = g_f->p[player & 1].ringout ? g_f->fall_depth : 0;
}

static Spr *spr_new(int cls)
{
    if (g_nspr >= MAX_SPR) return NULL;
    Spr *s = &g_spr[g_nspr++];
    memset(s, 0, sizeof *s);
    s->cls = cls;
    return s;
}
/* 0x44d759 / 0x44836e: start script `start` of lib */
static void spr_init(Spr *s, SLib *lib, int start, int player, int group, int32_t extra)
{
    s->lib = lib; s->pc = start; s->timer = 0; s->frame = 0;
    s->player = player & 1; s->group = group; s->extra = (float)extra * FX1;
}

static int run(Spr *s, float dt);

/* 0x43bbfa / 0x43be3c / 0x43ba41: `count` particles of emitter `e` from bone matrix m + offset */
static void emitter(int count, SLib *lib, const int32_t *e, const float m[4][3], const float off[3], int player)
{
    float org[3], vel[3] = { 0, 0, 0 };
    apply_p(m, off, org);
    switch (e[2]) {
    case 0: player ^= 1;                                    /* toward the opponent... */
        /* fall through */
    case 1: {                                               /* ...or toward an own bone */
        float t[3] = { 0, 0, 0 };
        if (e[3] != 0x10 && e[3] >= 0 && e[3] < 16) {      /* 0x10: the projectile (none in Turbo) */
            float tm[4][3], to[3] = { e[4] * FX1, e[5] * FX1, e[6] * FX1 };
            bone_m(player, e[3], tm);
            apply_p(tm, to, t);
            for (int k = 0; k < 3; k++) t[k] -= org[k];
        }
        float l = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (l < 3 * FX1) { t[0] = 1; t[1] = t[2] = 0; } else for (int k = 0; k < 3; k++) t[k] /= l;
        for (int k = 0; k < 3; k++) vel[k] = t[k] * (float)e[7] * FX1;
        break;
    }
    case 2: case 4:
        for (int k = 0; k < 3; k++) vel[k] = (float)e[3 + k] * FX1;
        break;
    case 3: {                                               /* the original uses ApplyP here, with the */
        float v[3] = { e[3] * FX1, e[4] * FX1, e[5] * FX1 };   /* bone's translation (kept as it is) */
        apply_p(m, v, vel);
        break;
    }
    }
    for (int n = 0; n < count; n++) {
        Spr *s = spr_new(C_PARTICLE);
        if (!s) return;
        for (int k = 0; k < 3; k++) s->pos[k] = (float)rnd((int32_t)lrintf(org[k] * 65536.0f), e[1]) * FX1;
        if (e[2] == 4) s->pos[2] = 0;
        for (int k = 0; k < 3; k++) s->vel[k] = (float)rnd((int32_t)lrintf(vel[k] * 65536.0f), e[8]) * FX1;
        s->acc[2] = (float)rnd(e[9], e[10]) * FX1;
        s->bounce = rnd(e[11], e[12]);
        s->life = rnd(e[13], e[14]);
        (void)rnd(e[15], e[16]);                            /* +0x88: not used */
        spr_init(s, lib, e[0], player, -1, 0);
    }
}

/* 0x44d868: runs the script until the current frame has time left; returns the frame (0 = ended) */
static int run(Spr *s, float dt)
{
    if (!s->lib) return 0;
    SLib *lib = s->lib;
    s->timer -= dt;
    for (int guard = 0; s->timer < 0 || s->frame == 0; guard++) {
        if (s->pc < 0 || s->pc >= lib->ncmd || guard > 4096) { s->lib = NULL; return 0; }
        const int32_t *c = lib->cmd[s->pc++];
        switch (c[0]) {
        case 1: {                                           /* frame: pixelmap, time, 2x3 matrix */
            float t; memcpy(&t, &c[2], 4);
            s->frame = c[1] + 1;
            memcpy(s->m, c + 3, sizeof s->m);
            s->timer += t;
            break;
        }
        case 2:                                             /* goto */
            if (c[2] == -1) s->pc = c[1];
            break;
        case 3:                                             /* end (0x44d811) */
            s->lib = NULL; s->frame = 0;
            return 0;
        case 4: {                                           /* particle emitter on a bone group */
            if (c[1] < 0 || c[1] >= lib->nemit) break;
            int g = c[2];
            float off[3] = { c[3] * FX1, c[4] * FX1, c[5] * FX1 };
            if (g == -1) { g = s->group; off[0] = off[1] = off[2] = 0; }
            if (g < 0 || g > 63) break;
            for (int b = 1; b <= GROUPS[g][0]; b++) {
                float m[4][3];
                bone_m(s->player, GROUPS[g][b], m);
                emitter(c[6], lib, lib->emit[c[1]], m, off, s->player);
            }
            break;
        }
        case 5: {                                           /* child sprite */
            int g = c[2];
            if (s->group == -2) {                           /* on the fighter's floor position */
                Spr *n = spr_new(C_FIXED);
                if (!n) break;
                root_pos(s->player, n->pos);
                spr_init(n, lib, c[1], s->player, -2, c[7]);
                break;
            }
            if (g == 0x10) break;                           /* the projectile: none in Turbo */
            float off[3] = { c[3] * FX1, c[4] * FX1, c[5] * FX1 };
            if (g == -1) { g = s->group; off[0] = off[1] = off[2] = 0; }
            if (g < 0 || g > 63) break;
            if (c[6] < 0 || c[6] > 4) break;                /* "bad sprite type" */
            for (int b = 1; b <= GROUPS[g][0]; b++) {
                int bone = GROUPS[g][b];
                Spr *n = spr_new(c[6] == 0 ? C_FIXED : c[6] == 1 ? C_FOLLOW : C_BONE);
                if (!n) break;
                n->bone = bone; n->euler = c[6];
                memcpy(n->off, off, sizeof off);
                if (c[6] == 0) {                            /* 0x447f4a: fixed where the bone is now */
                    float m[4][3];
                    bone_m(s->player, bone, m);
                    apply_p(m, off, n->pos);
                }
                spr_init(n, lib, c[1], s->player, bone, c[7]);
            }
            break;
        }
        default:
            s->lib = NULL;
            return 0;
        }
    }
    return s->frame;
}

/* 0x44cba0 */
void sprfx_trigger(const Fight *f, int player, int group, int id)
{
    if (id < 0 || id > 255) return;
    g_f = f;
    SLib *lib = g_slot[id][player & 1].lib;
    if (!lib) return;
    Spr tmp;
    memset(&tmp, 0, sizeof tmp);
    spr_init(&tmp, lib, g_slot[id][player & 1].start, player, group, 0);
    run(&tmp, 0);
    if (getenv("FX_SPRTRACE"))
        fprintf(stderr, "sprfx t%d P%d group %d id %d %s -> %d sprites\n", f ? f->round_age : -1,
                (player & 1) + 1, group, id,
                sprfx_name(id, player) ? sprfx_name(id, player) : "?", g_nspr);
}

/* BrMatrix34ToEuler (Shoemake), first angle only, M(r, c) = m[c][r] of the row-vector matrix */
static float euler_first(float m[3][3], int order)
{
    static const int next[4] = { 1, 2, 0, 1 };
    int f = order & 1, r = (order >> 1) & 1, p = (order >> 2) & 1, i = (order >> 3) & 3;
    int j = next[i + p], k = next[i + 1 - p];
#define M(a, b) (m[b][a])
    float a, c;
    if (r) {
        float sy = sqrtf(M(i, j) * M(i, j) + M(i, k) * M(i, k));
        if (sy > 1e-4f) { a = atan2f(M(i, j), M(i, k)); c = atan2f(M(j, i), -M(k, i)); }
        else { a = atan2f(-M(j, k), M(j, j)); c = 0; }
    } else {
        float cy = sqrtf(M(i, i) * M(i, i) + M(j, i) * M(j, i));
        if (cy > 1e-4f) { a = atan2f(M(k, j), M(k, k)); c = atan2f(M(j, i), M(i, i)); }
        else { a = atan2f(-M(j, k), M(j, j)); c = 0; }
    }
#undef M
    if (p) { a = -a; c = -c; }
    return f ? c : a;
}

static void cam_axes(const float dir[3], float s[3], float u[3], float b[3])
{
    float f[3] = { dir[0], dir[1], dir[2] }, l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (l <= 0) { f[0] = 1; f[1] = f[2] = 0; l = 1; }
    for (int k = 0; k < 3; k++) f[k] /= l;
    s[0] = f[1]; s[1] = -f[0]; s[2] = 0;                     /* as gl_look(): f x up */
    l = sqrtf(s[0] * s[0] + s[1] * s[1]);
    if (l <= 0) { s[0] = 1; s[1] = 0; l = 1; }
    s[0] /= l; s[1] /= l;
    u[0] = s[1] * f[2] - s[2] * f[1]; u[1] = s[2] * f[0] - s[0] * f[2]; u[2] = s[0] * f[1] - s[1] * f[0];
    for (int k = 0; k < 3; k++) b[k] = -f[k];                /* BRender cameras look down -Z */
}

/* 0x44e9ea: every sprite's update (vtable +4), the dead ones removed */
void sprfx_update(const Fight *f, float dt, const float eye[3], const float dir[3])
{
    (void)eye;
    g_f = f;
    float cs[3], cu[3], cb[3];
    cam_axes(dir, cs, cu, cb);
    for (int i = 0; i < g_nspr; i++) {                     /* children spawned now are updated too */
        Spr *s = &g_spr[i];
        int alive = 1;
        if (s->cls == C_FOLLOW) {                           /* 0x41642c */
            float m[4][3]; bone_m(s->player, s->bone, m);
            memcpy(s->pos, m[3], sizeof s->pos);
        } else if (s->cls == C_BONE) {                      /* 0x43c3c5 */
            float m[4][3]; bone_m(s->player, s->bone, m);
            apply_p(m, s->off, s->pos);
            float r[3][3];                                  /* camera -> bone */
            const float *ca[3] = { cs, cu, cb };
            for (int a = 0; a < 3; a++)
                for (int c = 0; c < 3; c++)
                    r[a][c] = ca[a][0] * m[c][0] + ca[a][1] * m[c][1] + ca[a][2] * m[c][2];
            static const int ORDER[5] = { 0, 0, 2, 5, 4 };
            float e = euler_first(r, ORDER[s->euler]);
            s->angle = s->euler == 2 ? (float)0x7ff8 / 65536.0f * 6.2831853f - e : e;
        } else if (s->cls == C_PARTICLE && dt != 0) {       /* 0x43bec1 */
            int32_t idt = (int32_t)(dt * 65536.0f);
            if (s->life != -0x10000) { s->life -= idt; if (s->life < 0) alive = 0; }
            if (alive) {
                float half = (float)(idt >> 17) * FX1;      /* the original's dt / 2 is an integer: */
                for (int k = 0; k < 3; k++) {               /* 0 for whole ticks */
                    float dv = s->acc[k] * dt;
                    s->pos[k] += dv * half + s->vel[k] * dt;
                    s->vel[k] += dv;
                }
                if (s->pos[2] < 0) {
                    if (!s->bounce) alive = 0;
                    else { s->pos[2] = 0; s->vel[2] *= -(float)s->bounce * FX1; }
                }
            }
        }
        if (alive && !run(s, dt)) alive = 0;                /* 0x4480c7 */
        if (!alive) s->lib = NULL;
    }
    int n = 0;
    for (int i = 0; i < g_nspr; i++) if (g_spr[i].lib) g_spr[n++] = g_spr[i];
    g_nspr = n;
}

/* 0x4480c7: unit quad * frame matrix * rotation, in the camera plane, at the position pulled
 * `extra` toward the camera. Texture v runs top down (BrSprite UVs 0..0.97, 0x4483aa). */
void sprfx_draw(const float eye[3], const float dir[3])
{
    if (!g_nspr) return;
    float cs[3], cu[3], cb[3];
    cam_axes(dir, cs, cu, cb);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST); glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(1, 1, 1, 1);
    const float UV = (float)0xf851 / 65536.0f;
    for (int i = 0; i < g_nspr; i++) {
        const Spr *s = &g_spr[i];
        if (!s->lib || s->frame <= 0 || s->frame > s->lib->npix) continue;
        float p[3] = { s->pos[0], s->pos[1], s->pos[2] };
        if (s->extra != 0) {
            float d[3] = { p[0] - eye[0], p[1] - eye[1], p[2] - eye[2] };
            float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l < 3 * FX1) { d[0] = 1; d[1] = d[2] = 0; } else for (int k = 0; k < 3; k++) d[k] /= l;
            for (int k = 0; k < 3; k++) p[k] -= d[k] * s->extra;
        }
        float ca = cosf(s->angle), sa = sinf(s->angle);
        glBindTexture(GL_TEXTURE_2D, s->lib->pix[s->frame - 1].tex);
        glBegin(GL_QUADS);
        static const float Q[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int v = 0; v < 4; v++) {
            float x = Q[v][0], y = Q[v][1];
            float lx = (x * s->m[0] + y * s->m[2] + s->m[4]) * FX1;
            float ly = (x * s->m[1] + y * s->m[3] + s->m[5]) * FX1;
            float rx = lx * ca - ly * sa, ry = lx * sa + ly * ca;
            glTexCoord2f(x * UV, (1 - y) * UV);
            glVertex3f(p[0] + rx * cs[0] + ry * cu[0], p[1] + rx * cs[1] + ry * cu[1], p[2] + rx * cs[2] + ry * cu[2]);
        }
        glEnd();
    }
    glPopAttrib();
}
