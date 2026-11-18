/*
 * FX Fighter Turbo - open reimplementation
 * Loaders for the original data files.
 */
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static uint32_t rd32le(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t rd32be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static float    rdfbe(const uint8_t *p)  { uint32_t u = rd32be(p); float f; memcpy(&f, &u, 4); return f; }

/* ============================ PAC ============================ */
/* Layout: u32 count; count * { u32 offset; char name[80]; }
 * The last entry has an empty name and marks the end of data. */
int pac_open(Pac *pac, const char *path)
{
    memset(pac, 0, sizeof *pac);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 4) { fclose(f); return 0; }
    pac->data = malloc((size_t)sz);
    pac->size = (size_t)sz;
    if (fread(pac->data, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); pac_close(pac); return 0; }
    fclose(f);

    uint32_t n = rd32le(pac->data);
    if (4 + (size_t)n * 84 > pac->size) { pac_close(pac); return 0; }
    pac->entries = calloc(n, sizeof(PacEntry));
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = pac->data + 4 + i * 84;
        uint32_t off = rd32le(e);
        uint32_t end = (i + 1 < n) ? rd32le(e + 84) : (uint32_t)pac->size;
        if (!e[4]) continue;
        PacEntry *pe = &pac->entries[pac->count++];
        memcpy(pe->name, e + 4, 79);
        pe->name[79] = 0;
        pe->offset = off;
        pe->size = (end > off && end <= pac->size) ? end - off : 0;
    }
    return 1;
}

void pac_close(Pac *pac)
{
    free(pac->data);
    free(pac->entries);
    memset(pac, 0, sizeof *pac);
}

static int name_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char ca = *a == '\\' ? '/' : (char)tolower((unsigned char)*a);
        char cb = *b == '\\' ? '/' : (char)tolower((unsigned char)*b);
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

const uint8_t *pac_get(const Pac *pac, const char *name, size_t *size)
{
    for (int i = 0; i < pac->count; i++)
        if (name_eq(pac->entries[i].name, name)) {
            if (size) *size = pac->entries[i].size;
            return pac->data + pac->entries[i].offset;
        }
    return NULL;
}

/* ============================ CMP ============================ */
/* u32 unpacked size, then: c < 0x80 -> repeat next byte c+2 times,
 *                          c >= 0x80 -> copy 256-c literal bytes. */
uint8_t *cmp_decode(const uint8_t *src, size_t n, size_t *out_size)
{
    if (!src || n < 4) return NULL;
    uint32_t total = rd32le(src);
    if (total > 16u * 1024 * 1024) return NULL;
    uint8_t *out = calloc(1, total ? total : 1);
    size_t p = 4, o = 0;
    while (p < n && o < total) {
        uint8_t c = src[p++];
        if (c < 0x80) {
            if (p >= n) break;
            size_t k = (size_t)c + 2;
            if (o + k > total) k = total - o;
            memset(out + o, src[p++], k);
            o += k;
        } else {
            size_t k = 256 - c;
            if (p + k > n) k = n - p;
            if (o + k > total) k = total - o;
            memcpy(out + o, src + p, k);
            p += k; o += k;
        }
    }
    if (out_size) *out_size = total;
    return out;
}

/* ===================== BRender chunk files ===================== */
enum {
    FID_END = 0, FID_OLD_PIXELMAP = 3, FID_MATERIAL_OLDEST = 4, FID_FILE_INFO = 18,
    FID_MATERIAL_INDEX = 22, FID_VERTICES = 23, FID_VERTEX_UV = 24, FID_FACE_MATERIAL = 26,
    FID_COLOUR_MAP_REF = 28, FID_INDEX_SHADE_REF = 31, FID_PIXELS = 33, FID_FACES = 53,
    FID_OLD_MODEL_2 = 54
};

/* Read NUL-terminated string at p (bounded); returns position after NUL. */
static size_t rd_str(const uint8_t *d, size_t n, size_t p, char *out, size_t cap)
{
    size_t i = 0;
    while (p < n && d[p]) { if (i + 1 < cap) out[i++] = (char)d[p]; p++; }
    if (cap) out[i] = 0;
    return p + 1;
}

int pal_load_pixelmap(const uint8_t *d, size_t n, Rgb pal[256])
{
    size_t p = 0;
    while (p + 8 <= n) {
        uint32_t id = rd32be(d + p), len = rd32be(d + p + 4);
        size_t q = p + 8;
        if (id == FID_PIXELS) {
            uint32_t count = rd32be(d + q), esz = rd32be(d + q + 4);
            const uint8_t *px = d + q + 8;
            if (esz != 4 || count < 256 || q + 8 + 1024 > n) return 0;
            for (int i = 0; i < 256; i++) { pal[i].r = px[i*4+1]; pal[i].g = px[i*4+2]; pal[i].b = px[i*4+3]; }
            return 1;
        }
        if (id == FID_OLD_PIXELMAP) { char tmp[64]; p = rd_str(d, n, q + 11, tmp, sizeof tmp); continue; }
        p = q + len;
    }
    return 0;
}

/* Pixelmap files are stack based: OLD_PIXELMAP pushes a map, PIXELS fills the top one,
 * ADD_MAP (34) pops the top (a 256x1 RGBX palette) and attaches it to the map below,
 * END flushes. Only 8-bit maps (type 3) are returned. */
int pix_load_all(const uint8_t *d, size_t n, Pixmap *out, int max)
{
    Pixmap stack[4];
    int sp = 0, cnt = 0;
    size_t p = 0;
    memset(stack, 0, sizeof stack);
    while (p + 8 <= n) {
        uint32_t id = rd32be(d + p), len = rd32be(d + p + 4);
        size_t q = p + 8;
        if (id == FID_OLD_PIXELMAP) {
            if (sp == 4) { pix_free(&stack[0]); memmove(stack, stack + 1, 3 * sizeof(Pixmap)); sp = 3; }
            Pixmap *pm = &stack[sp++];
            memset(pm, 0, sizeof *pm);
            pm->type = d[q];
            pm->row_bytes = rd16be(d + q + 1);
            pm->width = rd16be(d + q + 3);
            pm->height = rd16be(d + q + 5);
            p = rd_str(d, n, q + 11, pm->name, sizeof pm->name);   /* length field is unreliable */
            continue;
        }
        if (id == FID_PIXELS) {
            uint32_t count = rd32be(d + q), esz = rd32be(d + q + 4);
            size_t bytes = (size_t)count * esz;
            if (q + 8 + bytes > n) break;
            if (sp > 0) {
                free(stack[sp - 1].pixels);
                stack[sp - 1].pixels = malloc(bytes ? bytes : 1);
                memcpy(stack[sp - 1].pixels, d + q + 8, bytes);
            }
            p = q + 8 + bytes;
            continue;
        }
        if (id == 34 && sp >= 2) {                     /* ADD_MAP: attach palette */
            Pixmap *pal = &stack[--sp], *img = &stack[sp - 1];
            if (pal->pixels && pal->height >= 256) {
                for (int i = 0; i < 256; i++) {
                    img->pal[i].r = pal->pixels[i * 4 + 1];
                    img->pal[i].g = pal->pixels[i * 4 + 2];
                    img->pal[i].b = pal->pixels[i * 4 + 3];
                }
                img->has_pal = 1;
            }
            pix_free(pal);
        }
        if (id == FID_END || p + 8 + len >= n) {
            for (int k = 0; k < sp; k++) {
                if (stack[k].type == 3 && stack[k].pixels && cnt < max) out[cnt++] = stack[k];
                else pix_free(&stack[k]);
            }
            sp = 0;
        }
        p = q + len;
    }
    for (int k = 0; k < sp; k++) {
        if (stack[k].type == 3 && stack[k].pixels && cnt < max) out[cnt++] = stack[k];
        else pix_free(&stack[k]);
    }
    return cnt;
}

void pix_free(Pixmap *p) { free(p->pixels); p->pixels = NULL; }

int model_load_all(const uint8_t *d, size_t n, Model *out, int max)
{
    size_t p = 0; int cnt = -1;
    Model *m = NULL;
    while (p + 8 <= n) {
        uint32_t id = rd32be(d + p), len = rd32be(d + p + 4);
        size_t q = p + 8;
        switch (id) {
        case FID_OLD_MODEL_2:
            if (cnt + 1 >= max) return cnt + 1;
            m = &out[++cnt];
            memset(m, 0, sizeof *m);
            rd_str(d, n, q + 2, m->name, sizeof m->name);
            p = q + len;
            break;
        case FID_VERTICES: {
            uint32_t c = rd32be(d + q);
            if (!m || q + 4 + c * 12 > n) return cnt + 1;
            m->nverts = (int)c;
            m->verts = malloc(sizeof(Vec3) * c);
            for (uint32_t i = 0; i < c; i++) {
                const uint8_t *v = d + q + 4 + i * 12;
                m->verts[i].x = rdfbe(v); m->verts[i].y = rdfbe(v + 4); m->verts[i].z = rdfbe(v + 8);
            }
            p = q + len;
            break; }
        case FID_VERTEX_UV: {
            uint32_t c = rd32be(d + q);
            if (!m || q + 4 + c * 8 > n) return cnt + 1;
            m->uvs = malloc(sizeof(Vec2) * c);
            for (uint32_t i = 0; i < c; i++) {
                const uint8_t *v = d + q + 4 + i * 8;
                m->uvs[i].u = rdfbe(v); m->uvs[i].v = rdfbe(v + 4);
            }
            p = q + len;
            break; }
        case FID_FACES: {
            uint32_t c = rd32be(d + q);
            if (!m || q + 4 + c * 9 > n) return cnt + 1;
            m->nfaces = (int)c;
            m->faces = calloc(c, sizeof(Face));
            for (uint32_t i = 0; i < c; i++) {
                const uint8_t *f = d + q + 4 + i * 9;
                m->faces[i].v[0] = rd16be(f); m->faces[i].v[1] = rd16be(f + 2); m->faces[i].v[2] = rd16be(f + 4);
                m->faces[i].smooth = rd16be(f + 6); m->faces[i].flags = f[8];
            }
            p = q + len;
            break; }
        case FID_MATERIAL_INDEX: {     /* u32 count, then count NUL strings; length field unreliable */
            uint32_t c = rd32be(d + q);
            if (!m || c > 64) return cnt + 1;
            m->nmats = (int)c;
            m->mats = calloc(c ? c : 1, 32);
            size_t r = q + 4;
            for (uint32_t i = 0; i < c; i++) r = rd_str(d, n, r, m->mats[i], 32);
            p = r;
            break; }
        case FID_FACE_MATERIAL: {      /* u32 count, u32 elem size (2), u16[count] */
            uint32_t c = rd32be(d + q);
            if (!m || q + 8 + c * 2 > n) return cnt + 1;
            for (uint32_t i = 0; i < c && (int)i < m->nfaces; i++) m->faces[i].mat = rd16be(d + q + 8 + i * 2);
            p = q + 8 + c * 2;
            break; }
        default:
            p = q + len;
        }
    }
    return cnt + 1;
}

void model_free(Model *m)
{
    free(m->verts); free(m->uvs); free(m->faces); free(m->mats);
    memset(m, 0, sizeof *m);
}

int material_load_all(const uint8_t *d, size_t n, Material *out, int max)
{
    size_t p = 0; int cnt = -1;
    while (p + 8 <= n) {
        uint32_t id = rd32be(d + p), len = rd32be(d + p + 4);
        size_t q = p + 8;
        if (id == FID_MATERIAL_OLDEST) {
            if (cnt + 1 >= max) break;
            Material *m = &out[++cnt];
            memset(m, 0, sizeof *m);
            m->r = d[q]; m->g = d[q+1]; m->b = d[q+2]; m->opacity = d[q+3];
            m->ka = rdfbe(d + q + 4); m->kd = rdfbe(d + q + 8); m->ks = rdfbe(d + q + 12); m->power = rdfbe(d + q + 16);
            m->flags = rd16be(d + q + 20);
            m->index_base = d[q + 46];
            m->index_range = d[q + 47];
            p = rd_str(d, n, q + 48, m->name, sizeof m->name);
        } else if (id >= FID_COLOUR_MAP_REF && id <= 32) {
            char tmp[32];
            p = rd_str(d, n, q, tmp, sizeof tmp);
            if (cnt >= 0 && id == FID_COLOUR_MAP_REF) strcpy(out[cnt].colour_map, tmp);
        } else {
            p = q + len;
        }
    }
    return cnt + 1;
}

const Material *material_find(const Material *m, int n, const char *name)
{
    for (int i = 0; i < n; i++) if (name_eq(m[i].name, name)) return &m[i];
    return NULL;
}

/* ============================ ANI ============================ */
/* u32 nbones, u32 nclips, u32 total_frames;
 * per clip: char name[16]; u32 nframes;
 *   per frame: s32 root_x, root_height, root_forward; nbones * 6 bytes. */
int ani_load(const uint8_t *d, size_t n, AniSet *a)
{
    memset(a, 0, sizeof *a);
    if (n < 12) return 0;
    a->nbones = (int)rd32le(d);
    a->nclips = (int)rd32le(d + 4);
    uint32_t total = rd32le(d + 8);
    if (a->nbones != ANI_MAX_BONES || a->nclips <= 0 || a->nclips > 1000) return 0;
    a->clips = calloc((size_t)a->nclips, sizeof(AniClip));
    a->frame_pool = calloc(total ? total : 1, sizeof(AniFrame));
    size_t p = 12, used = 0;
    for (int c = 0; c < a->nclips; c++) {
        AniClip *cl = &a->clips[c];
        if (p + 20 > n) return 0;
        memcpy(cl->name, d + p, 16);
        cl->name[16] = 0;
        cl->name[strnlen(cl->name, 16)] = 0;
        p += 16;
        cl->nframes = (int)rd32le(d + p); p += 4;
        if (used + (size_t)cl->nframes > total) return 0;
        cl->frames = a->frame_pool + used;
        used += (size_t)cl->nframes;
        for (int f = 0; f < cl->nframes; f++) {
            if (p + 12 + 6 * (size_t)a->nbones > n) return 0;
            AniFrame *fr = &cl->frames[f];
            for (int k = 0; k < 3; k++) fr->root[k] = (int32_t)rd32le(d + p + k * 4);
            memcpy(fr->bone, d + p + 12, 6 * (size_t)a->nbones);
            p += 12 + 6 * (size_t)a->nbones;
        }
    }
    return 1;
}

void ani_free(AniSet *a)
{
    free(a->clips); free(a->frame_pool);
    memset(a, 0, sizeof *a);
}

/* BRender row-vector matrices (v' = v * M). */
static void m_rot(float m[4][3], int axis, float ang)
{
    float c = cosf(ang), s = sinf(ang);
    memset(m, 0, sizeof(float) * 12);
    if (axis == 0) { m[0][0] = 1; m[1][1] = c; m[1][2] = s;  m[2][1] = -s; m[2][2] = c; }
    if (axis == 1) { m[0][0] = c; m[0][2] = -s; m[1][1] = 1; m[2][0] = s;  m[2][2] = c; }
    if (axis == 2) { m[0][0] = c; m[0][1] = s;  m[1][0] = -s; m[1][1] = c; m[2][2] = 1; }
}

static void m_mul33(float out[4][3], float a[4][3], float b[4][3])
{
    float r[4][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    for (int j = 0; j < 3; j++) r[3][j] = 0;
    memcpy(out, r, sizeof r);
}

/* Reconstructed from FIGHT.EXE @ 0x445f16:
 *   RotateZ((255-b1)<<8); PostRotateX((255-b0)<<8); PostRotateY((255-b2)<<8);
 *   T = ((s8)b3, (s8)b5, (s8)b4) * 406  (16.16 fixed) */
void ani_bone_matrix(const AniFrame *f, int bone, float m[4][3])
{
    const uint8_t *b = f->bone[bone];
    const float k = 6.28318530718f / 65536.0f;
    float rx = (float)((255 - b[0]) << 8) * k;
    float rz = (float)((255 - b[1]) << 8) * k;
    float ry = (float)((255 - b[2]) << 8) * k;
    float a[4][3], t[4][3];
    m_rot(a, 2, rz);
    m_rot(t, 0, rx); m_mul33(a, a, t);
    m_rot(t, 1, ry); m_mul33(a, a, t);
    a[3][0] = (float)(int8_t)b[3] * ANI_UNIT;
    a[3][1] = (float)(int8_t)b[5] * ANI_UNIT;
    a[3][2] = (float)(int8_t)b[4] * ANI_UNIT;
    memcpy(m, a, sizeof a);
}
