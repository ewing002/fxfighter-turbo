/* Headless driver of the front end script VM (src/fe3d_vm.c).
 * usage: fevm FIGHT.PAC [FIGHT.EXE] [ticks] [events...] [options]
 *   event:   T:KEY[*N]   press KEY at tick T for N ticks (default 1)
 *            KEY = P1RIGHT P1LEFT P1UP P1DOWN P1PUNCH P1KICK, P2..., ESC, RAW<hex scancode>
 *   options: --every N   print the menu globals every N ticks (and on every change of G69/G72/G75)
 *            --entry E   G58 entry type      --seed S   RNG seed      --debug   script debug output
 *            --stop      stop at the first START_GAME (mode >= 0)
 *            --fuzz P    random key presses (P percent per tick); every START_GAME restarts the
 *                        front end (modes 7/8/9 with their entry type, others with 0)
 * Prints the live actor tree, the menu globals, the mode and the sound log at the end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../src/fe3d_vm.h"

#define MAXEV 256
typedef struct { int tick, len, player; unsigned bits; int raw; } Ev;

static const struct { const char *name; unsigned bits; } KEYS[] = {
    { "RIGHT", 0x01 }, { "LEFT", 0x02 }, { "DOWN", 0x04 }, { "UP", 0x08 }, { "PUNCH", 0x10 }, { "KICK", 0x20 } };

static int parse_ev(const char *s, Ev *e)
{
    char key[32];
    memset(e, 0, sizeof *e);
    e->len = 1;
    if (sscanf(s, "%d:%31[A-Za-z0-9]", &e->tick, key) != 2) return 0;
    const char *st = strchr(s, '*');
    if (st) e->len = atoi(st + 1);
    for (char *p = key; *p; p++) *p = (char)toupper((unsigned char)*p);
    if (!strcmp(key, "ESC")) { e->raw = 1; return 1; }
    if (!strncmp(key, "RAW", 3)) { e->raw = (int)strtol(key + 3, NULL, 16); return e->raw > 0; }
    if ((key[0] != 'P') || (key[1] != '1' && key[1] != '2')) return 0;
    e->player = key[1] - '1';
    for (size_t k = 0; k < sizeof KEYS / sizeof KEYS[0]; k++)
        if (!strcmp(key + 2, KEYS[k].name)) { e->bits = KEYS[k].bits; return 1; }
    return 0;
}

static const char *aname(const FeVm *vm, const FeActor *a)
{
    if (a->brtype == 0) return "(null)";
    if (a->brtype == 2) return "(camera)";
    if (a->model < FE_NUM_TYPES && vm->fe->type_name[a->model][0]) return vm->fe->type_name[a->model];
    return "(?)";
}

static double U(const FeActor *a, int off) { int32_t v; memcpy(&v, (const uint8_t *)a->f + off, 4); return v / 65536.0; }

static void tree(const FeVm *vm, int i, int depth)
{
    const FeActor *a = &vm->a[i];
    printf("%*s%-12s pos(%g,%g,%g) rot(%g,%g,%g) scale(%g,%g,%g) world %d/%d id %g strat %d ip %x%s\n",
           depth * 2, "", aname(vm, a), U(a, 0x3c), U(a, 0x40), U(a, 0x44), U(a, 0x30), U(a, 0x34), U(a, 0x38),
           U(a, 0x48), U(a, 0x4c), U(a, 0x50), (int)U(a, 0xe4), a->rworld, U(a, 0x28), a->strat, a->ip,
           a->rstyle == 1 ? " HIDDEN" : "");
    /* children in spawn order (the list is head-inserted) */
    int kids[FEVM_MAX_ACTORS], n = 0;
    for (int c = a->child; c >= 0 && n < FEVM_MAX_ACTORS; c = vm->a[c].sib_next) kids[n++] = c;
    while (n > 0) tree(vm, kids[--n], depth + 1);
}

static void globals(const FeVm *vm, int tick)
{
#define G(n) (vm->G[n] / 65536.0)
    printf("tick %4d: G69=%g G72=%g G75=%g G73=%g G74=%g G11=%g G12=%g G81=%g mode=%d live=%d\n", tick,
           G(69), G(72), G(75), G(73), G(74), G(11), G(12), G(81), vm->mode, fe_vm_live(vm));
#undef G
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: fevm FIGHT.PAC [FIGHT.EXE] [ticks] [T:KEY[*N] ...] [--every N] [--entry E] [--seed S] [--debug] [--stop]\n");
        return 1;
    }
    static Pac pac;
    static FeAssets fe;
    static FeVm vm;
    static Ev ev[MAXEV];
    int nev = 0, ticks = 60, every = 0, entry = 0, debug = 0, stop = 0, fuzz = 0, starts = 0;
    unsigned seed = 1;
    char exe[1024] = "";
    for (int i = 2; i < argc; i++) {
        const char *s = argv[i];
        if (!strcmp(s, "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(s, "--entry") && i + 1 < argc) entry = atoi(argv[++i]);
        else if (!strcmp(s, "--seed") && i + 1 < argc) seed = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(s, "--debug")) debug = 1;
        else if (!strcmp(s, "--stop")) stop = 1;
        else if (!strcmp(s, "--fuzz") && i + 1 < argc) fuzz = atoi(argv[++i]);
        else if (strchr(s, ':') && isdigit((unsigned char)s[0])) {
            if (nev < MAXEV && parse_ev(s, &ev[nev])) nev++;
            else { fprintf(stderr, "bad event %s\n", s); return 1; }
        } else if (isdigit((unsigned char)s[0])) ticks = atoi(s);
        else snprintf(exe, sizeof exe, "%s", s);
    }
    if (!exe[0]) {   /* default: FIGHT.EXE next to FIGHT.PAC */
        snprintf(exe, sizeof exe, "%s", argv[1]);
        char *s = strrchr(exe, '/'), *b = strrchr(exe, '\\');
        if (b > s) s = b;
        snprintf(s ? s + 1 : exe, sizeof exe - (size_t)(s ? s + 1 - exe : 0), "FIGHT.EXE");
    }
    if (!pac_open(&pac, argv[1])) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    const char *err = NULL;
    if (!fe_assets_load(&fe, &pac, exe, &err)) { fprintf(stderr, "front end: %s\n", err); return 1; }
    if (!fe_vm_init(&vm, &fe)) { fprintf(stderr, "vm init failed\n"); return 1; }
    vm.debug = debug;
    fe_vm_seed(&vm, seed);
    if (entry) fe_vm_restart(&vm, entry);

    char snd_log[4096] = "";
    size_t sl = 0;
    int32_t last69 = 0, last72 = 0, last75 = 0;
    int t;
    for (t = 0; t < ticks; t++) {
        unsigned bits[2] = { 0, 0 };
        static int rawdown[256], rawprev[256];
        memset(rawdown, 0, sizeof rawdown);
        for (int k = 0; k < nev; k++)
            if (t >= ev[k].tick && t < ev[k].tick + ev[k].len) {
                if (ev[k].raw) rawdown[ev[k].raw & 255] = 1; else bits[ev[k].player] |= ev[k].bits;
            }
        if (fuzz) {
            static unsigned frng = 12345;
            for (int p = 0; p < 2; p++) {
                frng = frng * 1103515245u + 12345u;
                if ((int)((frng >> 16) % 100) < fuzz) bits[p] |= 1u << ((frng >> 8) % 6);
            }
            frng = frng * 1103515245u + 12345u;
            if ((frng >> 16) % 1000 < 3) rawdown[1] = 1;
        }
        fe_vm_set_input(&vm, 0, bits[0]);
        fe_vm_set_input(&vm, 1, bits[1]);
        for (int k = 1; k < 256; k++) if (rawdown[k] != rawprev[k]) { fe_vm_key_raw(&vm, k, rawdown[k]); rawprev[k] = rawdown[k]; }
        int oldmode = vm.mode;
        fe_vm_tick(&vm);
        for (int s; (s = fe_vm_pop_sound(&vm)) >= 0;)
            if (sl + 24 < sizeof snd_log) sl += (size_t)snprintf(snd_log + sl, sizeof snd_log - sl, " %d@%d", s, t);
        if (every && (t % every == 0 || vm.G[69] != last69 || vm.G[72] != last72 || vm.G[75] != last75)) globals(&vm, t);
        last69 = vm.G[69]; last72 = vm.G[72]; last75 = vm.G[75];
        if (vm.mode != oldmode && vm.mode >= 0) {
            printf("tick %4d: START_GAME mode %d p1 %d p2 %d arena %d hc %d/%d\n", t, vm.mode, vm.p1, vm.p2, vm.arena, vm.hc1, vm.hc2);
            if (stop) { t++; break; }
            starts++;
            if (fuzz) fe_vm_restart(&vm, vm.mode >= 7 ? (vm.mode == 7 ? 0 : vm.mode) : 0);
        }
    }
    printf("== after %d ticks: live actors %d, script errors %d, START_GAMEs %d\n", t, fe_vm_live(&vm), vm.errors, starts);
    for (int i = vm.head; i >= 0; i = vm.a[i].next) if (vm.a[i].parent < 0) tree(&vm, i, 0);
    globals(&vm, t);
    static FeScene sc;
    fe_vm_scene(&vm, &sc);
    int models = 0, hid = 0, bad = 0;
    for (int k = 0; k < sc.count; k++) {
        models += sc.n[k].type >= 0; hid += sc.n[k].hidden;
        bad += sc.n[k].parent >= sc.count || (sc.n[k].parent >= 0 && sc.n[sc.n[k].parent].world != sc.n[k].world);
    }
    printf("scene: %d nodes, %d with a model, %d hidden, %d bad parent links\n", sc.count, models, hid, bad);
    printf("mode %d p1 %d p2 %d arena %d hc %d/%d music %d bg", vm.mode, vm.p1, vm.p2, vm.arena, vm.hc1, vm.hc2, vm.music);
    for (int k = 0; k < 4; k++) printf(" %d/%d", vm.bg[k].map, vm.bg[k].mode);
    printf("\nsounds:%s\n", snd_log);
    printf("options G20..G57:");
    for (int k = 20; k <= 57; k++) printf(" %g", vm.G[k] / 65536.0);
    printf("\n");
    fe_assets_free(&fe);
    pac_close(&pac);
    return 0;
}
