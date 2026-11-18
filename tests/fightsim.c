/* Headless fight simulation: two CPU fighters, logs move changes, hits, ring-outs and health.
 * usage: fightsim FIGHT.PAC [f1] [f2] [ticks] [seed] [arena]     (env V=1 for move log) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/assets.h"
#include "../src/fight.h"

static Pac pac;
static const uint8_t *getf(const char *fmt, int a, size_t *sz) { char n[128]; snprintf(n, sizeof n, fmt, a); return pac_get(&pac, n, sz); }

int main(int argc, char **argv)
{
    if (argc < 2 || !pac_open(&pac, argv[1])) { fprintf(stderr, "usage: fightsim FIGHT.PAC [f1] [f2] [ticks] [seed] [arena]\n"); return 1; }
    int id[2] = { argc > 2 ? atoi(argv[2]) : 1, argc > 3 ? atoi(argv[3]) : 2 };
    int ticks = argc > 4 ? atoi(argv[4]) : 48 * 60;
    srand(argc > 5 ? atoi(argv[5]) : 1);
    int arena = argc > 6 ? atoi(argv[6]) : 1;
    static AniSet own[2], gen; static MoveSet ms[2]; static FighterInfo fi[2], afi;
    size_t sz, s2;
    const uint8_t *d = pac_get(&pac, "fight/fighters/f0.ani", &sz); ani_load(d, sz, &gen);
    static Fight f;
    for (int i = 0; i < 2; i++) {
        d = getf("fight/fighters/f%d.ani", id[i], &sz); ani_load(d, sz, &own[i]);
        d = getf("fight/moves/f%d.hdr", id[i], &sz); const uint8_t *e = getf("fight/moves/f%d.ste", id[i], &s2);
        moveset_load(&ms[i], d, sz, e, s2);
        d = getf("fight/fighters/fi%d.cmp", id[i], &sz); fighter_info_load(&fi[i], d, sz);
        f.p[i].cpu = 1;
        static Model mesh[2][32];
        d = getf("fight/fighters/fm%d.dat", id[i], &sz);
        if (d && !getenv("NOMESH")) { f.p[i].nmesh = model_load_all(d, sz, mesh[i], 32); f.p[i].mesh = mesh[i]; }
        fight_init_player(&f.p[i], &ms[i], &own[i], &gen, &fi[i]);
    }
    d = getf("fight/fighters/fi%d.cmp", arena, &sz); fighter_info_load(&afi, d, sz);
    fight_set_arena(&f, &afi);
    f.ringouts = 1;
    f.ring_guard = getenv("RING_GUARD") != NULL;               /* port option: no stepping out */
    f.ai_level = getenv("AI") ? atoi(getenv("AI")) : 4;      /* CPU difficulty 0..7 */
    fight_reset(&f);
    int last[2] = { -1, -1 }, hp[2] = { MAX_HEALTH, MAX_HEALTH }, changes = 0, hits = 0, rounds = 0, ringouts = 0, kos = 0;
    float maxrot = 0;
    for (int t = 0; t < ticks; t++) {
        int w = f.winner;
        if (getenv("FORCE_IN")) { f.p[0].cpu = 0; f.p[1].cpu = 0; f.p[0].in = (t > 60 && t < (getenv("HOLD") ? atoi(getenv("HOLD")) : 70)) ? (unsigned)strtol(getenv("FORCE_IN"), 0, 16) : 0; }
        fight_tick(&f);
        if (getenv("FORCE_IN") && t > 55 && t < 130) printf("%d %-22s mode %d jt %.2f z %.2f s %.2f vel %.2f\n", t, player_move_name(&f.p[0]), f.p[0].mode, f.p[0].jt, f.p[0].z, f.p[0].s, f.p[0].vel);
        if (getenv("SND")) for (int k = 0; k < f.nsnd; k++) printf("SND %d %d %d\n", t, f.snd[k].bank, f.snd[k].index);
        f.nsnd = 0;
        float r = fabsf(f.ang - 1.5707963f); if (r > maxrot) maxrot = r;
        if (getenv("FLOOR")) for (int i = 0; i < 2; i++) {
            if (f.p[i].ringout) continue;
            float mn = 9; int mp = -1;
            for (int b = 0; b < 16; b++) { float j[3]; fight_joint(&f, i, b, j); if (j[2] < mn) { mn = j[2]; mp = b; } }
            printf("FLOOR %d %.3f part %d z %.2f mode %d %s\n", i, mn, mp, f.p[i].z, f.p[i].mode, player_move_name(&f.p[i]));
        }
        for (int i = 0; i < 2; i++) {
            if (f.p[i].move != last[i]) {
                changes++;
                if (getenv("V")) {
                    float x, y, z, yaw; fight_place(&f, i, &x, &y, &z, &yaw);
                    printf("%5d P%d -> %3d %-20s s %+.2f world (%+.2f,%+.2f,%.2f)\n", t, i + 1, f.p[i].move, player_move_name(&f.p[i]), f.p[i].s, x, y, z);
                }
                last[i] = f.p[i].move;
            }
            if (f.p[i].health != hp[i]) {
                if (f.p[i].health < hp[i]) hits++;
                if (getenv("V")) printf("%5d P%d health %3d -> %3d (%s)\n", t, i + 1, hp[i], f.p[i].health, player_move_name(&f.p[i]));
                hp[i] = f.p[i].health;
            }
        }
        if (w < 0 && f.winner >= 0) {
            rounds++;
            if (f.ko_reason == 1) ringouts++; else if (f.ko_reason == 0) kos++;
            printf("%5d ROUND OVER winner P%d (%s) line angle %.0f deg\n", t, f.winner + 1,
                   f.ko_reason == 1 ? "RING OUT" : f.ko_reason == 0 ? "KO" : "time", (f.ang - 1.5708f) * 57.3f);
        }
    }
    printf("move changes %d, damage events %d, rounds %d (KO %d, ring out %d), wins %d-%d, max line rotation %.0f deg\n",
           changes, hits, rounds, kos, ringouts, f.p[0].wins, f.p[1].wins, maxrot * 57.3f);
    return 0;
}
