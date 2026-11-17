/* Front end asset check: loads the 3D front end data and the script tables, prints a summary.
 * usage: fecheck FIGHT.PAC [FIGHT.EXE]      (V=1 lists the model types and untextured materials) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fe3d.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: fecheck FIGHT.PAC [FIGHT.EXE]\n"); return 1; }
    static Pac pac;
    if (!pac_open(&pac, argv[1])) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    char exe[1024];
    if (argc > 2) snprintf(exe, sizeof exe, "%s", argv[2]);
    else {   /* default: FIGHT.EXE next to FIGHT.PAC */
        snprintf(exe, sizeof exe, "%s", argv[1]);
        char *s = strrchr(exe, '/'), *b = strrchr(exe, '\\');
        if (b > s) s = b;
        snprintf(s ? s + 1 : exe, sizeof exe - (size_t)(s ? s + 1 - exe : 0), "FIGHT.EXE");
    }
    static FeAssets fe;
    const char *err = NULL;
    if (!fe_assets_load(&fe, &pac, exe, &err)) { fprintf(stderr, "front end: %s\n", err); return 1; }
    int textured = 0, missing = 0, types = 0, notype = 0;
    for (int i = 0; i < fe.nmats; i++) {
        if (fe.mat_map[i] >= 0) textured++;
        else if (fe.mats[i].colour_map[0]) { missing++; if (getenv("V")) printf("material %-12s map %s not found\n", fe.mats[i].name, fe.mats[i].colour_map); }
    }
    for (int t = 0; t < FE_NUM_TYPES; t++) {
        if (fe.type_model[t] >= 0) types++; else if (fe.type_name[t][0]) notype++;
        if (getenv("V")) printf("type %3d %-12s -> %d\n", t, fe.type_name[t], fe.type_model[t]);
    }
    printf("models %d, materials %d (textured %d, map missing %d), maps %d, back %dx%d\n",
           fe.nmodels, fe.nmats, textured, missing, fe.nmaps, fe.back.width, fe.back.height);
    printf("palette[1] = %d,%d,%d\n", fe.pal[1].r, fe.pal[1].g, fe.pal[1].b);
    printf("exe .data %08x..%08x, strats %d (0: %08x, 92: %08x), model types resolved %d, unresolved %d\n",
           fe.exe.base, fe.exe.base + fe.exe.size, FE_NUM_STRATS, fe.strat[0], fe.strat[92], types, notype);
    fe_assets_free(&fe);
    pac_close(&pac);
    return 0;
}
