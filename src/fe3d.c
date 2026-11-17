/*
 * FX Fighter Turbo - open reimplementation
 * 3D front end: assets and script data (see fe3d.h, docs/FRONTEND.md)
 */
#include "fe3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* ------------------------------------------------------------------ */
/*  FIGHT.EXE: map the .data section (strat bytecode, tables)          */
/* ------------------------------------------------------------------ */
int exe_data_load(ExeData *x, const char *path)
{
    memset(x, 0, sizeof *x);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0x400 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return 0; }
    fclose(f);
    int ok = 0;
    uint32_t pe = le32(d + 0x3c);
    if (pe + 24 < (uint32_t)n && !memcmp(d + pe, "PE\0\0", 4)) {
        int ns = le16(d + pe + 6), osz = le16(d + pe + 20);
        uint32_t ib = le32(d + pe + 52);
        for (int i = 0; i < ns; i++) {
            const uint8_t *s = d + pe + 24 + osz + i * 40;
            if (s + 40 > d + n || memcmp(s, ".data", 6)) continue;
            uint32_t vs = le32(s + 8), va = le32(s + 12), rs = le32(s + 16), ra = le32(s + 20);
            uint32_t size = vs > rs ? vs : rs;
            if (ra + rs > (uint32_t)n) break;
            x->base = ib + va;
            x->size = size;
            x->data = calloc(size, 1);
            if (x->data) { memcpy(x->data, d + ra, rs); ok = 1; }
            break;
        }
    }
    free(d);
    return ok;
}

void exe_data_free(ExeData *x) { free(x->data); memset(x, 0, sizeof *x); }

const uint8_t *exe_ptr(const ExeData *x, uint32_t va, uint32_t n)
{
    if (!x->data || va < x->base || va - x->base + n > x->size) return NULL;
    return x->data + (va - x->base);
}

uint32_t exe_u32(const ExeData *x, uint32_t va)
{
    const uint8_t *p = exe_ptr(x, va, 4);
    return p ? le32(p) : 0;
}

/* ------------------------------------------------------------------ */
/*  Front end assets                                                   */
/* ------------------------------------------------------------------ */
static int name_eq(const char *a, const char *b)
{
    while (*a && *b) { if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0; a++; b++; }
    return *a == *b;
}

static const uint8_t *fe_file(const Pac *pac, const char *name, size_t *sz)
{
    char path[96];
    snprintf(path, sizeof path, "fight/frontend/%s", name);
    return pac_get(pac, path, sz);
}

const Model *fe_type_model(const FeAssets *fe, int type)
{
    if (type < 0 || type >= FE_NUM_TYPES || fe->type_model[type] < 0) return NULL;
    return &fe->models[fe->type_model[type]];
}

#define STRAT_TABLE 0x49fb88u
#define TYPE_TABLE  0x4a2948u

int fe_assets_load(FeAssets *fe, const Pac *pac, const char *exe_path, const char **err)
{
    const char *dummy;
    if (!err) err = &dummy;
    memset(fe, 0, sizeof *fe);
    size_t sz;
    const uint8_t *d;

    /* models, materials, textures, palette, background */
    if (!(d = fe_file(pac, "crshipl1.dat", &sz))) { *err = "crshipl1.dat missing"; return 0; }
    fe->nmodels = model_load_all(d, sz, fe->models, FE_MAX_MODELS);
    if (!(d = fe_file(pac, "demo.mat", &sz))) { *err = "demo.mat missing"; return 0; }
    fe->nmats = material_load_all(d, sz, fe->mats, FE_MAX_MATS);
    /* the scene registers demo.pix (FUN_00412a7a); maps missing there are looked up as
     * fight/frontend/<name>.pix by the find hook (FUN_004129c0) - none of those exist */
    if (!(d = fe_file(pac, "demo.pix", &sz))) { *err = "demo.pix missing"; return 0; }
    fe->nmaps = pix_load_all(d, sz, fe->maps, FE_MAX_MAPS);
    if (!(d = fe_file(pac, "demo.pal", &sz)) || !pal_load_pixelmap(d, sz, fe->pal)) { *err = "demo.pal missing"; return 0; }
    if ((d = fe_file(pac, "back1.pix", &sz))) pix_load_all(d, sz, &fe->back, 1);
    for (int i = 0; i < fe->nmats; i++) {
        fe->mat_map[i] = -1;
        if (!fe->mats[i].colour_map[0]) continue;
        for (int k = 0; k < fe->nmaps; k++)
            if (name_eq(fe->maps[k].name, fe->mats[i].colour_map)) { fe->mat_map[i] = k; break; }
    }

    /* script bytecode and tables from the original EXE */
    if (!exe_data_load(&fe->exe, exe_path)) { *err = "FIGHT.EXE missing or unreadable"; return 0; }
    for (int i = 0; i < FE_NUM_STRATS; i++) {
        fe->strat[i] = exe_u32(&fe->exe, STRAT_TABLE + 4 * i);
        if (!exe_ptr(&fe->exe, fe->strat[i], 1)) { *err = "FIGHT.EXE: unexpected version (strat table)"; return 0; }
    }
    for (int t = 0; t < FE_NUM_TYPES; t++) {
        fe->type_model[t] = -1;
        const uint8_t *s = exe_ptr(&fe->exe, exe_u32(&fe->exe, TYPE_TABLE + 4 * t), 1);
        if (!s) continue;
        int k = 0;
        while (k < 31 && exe_ptr(&fe->exe, (uint32_t)(fe->exe.base + (s - fe->exe.data) + k), 1) && s[k]) { fe->type_name[t][k] = (char)s[k]; k++; }
        fe->type_name[t][k] = 0;
        for (int m = 0; m < fe->nmodels; m++)
            if (name_eq(fe->models[m].name, fe->type_name[t])) { fe->type_model[t] = m; break; }
    }
    return 1;
}

void fe_assets_free(FeAssets *fe)
{
    for (int i = 0; i < fe->nmodels; i++) model_free(&fe->models[i]);
    for (int i = 0; i < fe->nmaps; i++) pix_free(&fe->maps[i]);
    pix_free(&fe->back);
    exe_data_free(&fe->exe);
    memset(fe, 0, sizeof *fe);
}
