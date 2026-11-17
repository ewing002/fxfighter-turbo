/*
 * FX Fighter Turbo - open reimplementation (SDL2 + OpenGL)
 *
 * Loads arenas, fighters, animations and move tables straight from the original
 * FIGHT.PAC. Two modes:
 *
 * FIGHT mode (default)
 *   Player 1: A/D = left/right, W = up (jump), S = down (duck), F = punch, G = kick
 *   Player 2: arrows, K = punch, L = kick   (CPU-controlled unless --2p or F3)
 *   F3 toggle P2 human/CPU   F4 toggle P1 CPU (demo)   F5 restart round   R ring-outs on/off
 *   Home fixed camera view on/off, Insert / Delete next / previous view, PageUp / PageDown distance,
 *   arrows (P2 = CPU) turn / tilt the fixed view, Shift = fine steps
 *   M music on/off   X sound effects on/off   - / = music volume   (music/trackNN.mp3 from the CD, optional)
 *   Up/Down arrows are P2 keys - fighters are changed with 1/2 (P1) and 9/0 (P2)
 *
 * VIEWER mode (F1)
 *   Left / Right clip, Up / Down fighter 1, PageUp / PageDown fighter 2, Space pause, , . step
 *
 * Front end: loading screen, main menu, fighter / arena select, VS, best-of-3 matches, 1P ladder
 * (menus: WASD or arrows, Enter/Space/F select, Esc back). --p1/--p2/--arena/--demo/--2p/--fight skip the menus.
 *
 * Both: N next arena, B background, mouse drag orbit, wheel zoom, F11 fullscreen, Esc main menu
 */
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "assets.h"
#include "fight.h"
#include "audio.h"
#include "ui.h"

#define MAX_PARTS 32
#define MAX_MATS  256

typedef struct {
    Model   parts[MAX_PARTS];
    int     nparts;
    AniSet  anim;
    MoveSet moves;
    FighterInfo fi;
    GLuint  tex;
    int     id;
    int     clip;
    float   time;
} Fighter;

typedef struct {
    Model   parts[MAX_PARTS];
    int     nparts;
    GLuint  floor_tex, bg_tex;
    Rgb     pal[256];
    FighterInfo fi;          /* arena parameters (ring size, fall depth) from fi%d.cmp */
    float   land_half;       /* extent of the lower ground at the fall depth */
    int     id;
} Arena;

static Pac       g_pac;
static Material  g_mats[MAX_MATS];
static int       g_nmats;
static Arena     g_arena;
static Fighter   g_f[2];
static Model     g_shadow;
static AniSet    g_generic;              /* fighters/f0.ani: shared reaction animations */
static Fight     g_fight;
static int       g_game = 1;
static int       g_ringouts = 1;
static float     g_view_yaw = -1.5708f, g_yaw_ofs = 0, g_cam_x = 0, g_cam_y = 0;
static float     g_yaw = 0.6f, g_pitch = 0.38f, g_dist = 5.0f;
static int       g_paused = 0, g_show_bg = 1;
static float g_cam_line, g_cam_D, g_eye[3], g_cdir[3];
static int   g_cam_track;
static float g_bg_turn = 90, g_cm_pitch_bg = 88;
#define CAM_FOV_V 45.0f                 /* br_camera field_of_view 0x1ffe, aspect 0x175c2 = 1.46 */
#define CAM_ASPECT 1.46f

/* ---- front end / match state ---- */
enum { ST_TITLE, ST_MENU, ST_SELECT, ST_ARENA, ST_VS, ST_OPTIONS, ST_FIGHT };
static int  g_state = ST_TITLE, g_state_t;      /* current screen, ticks spent in it */
static int  g_two_players;                      /* match type: 0 = 1 player vs CPU ladder, 1 = 2 players */
static int  g_debug_fight;                      /* started from the command line: endless rounds */
static int  g_round = 1, g_match_over_t;        /* round number, ticks left on the match result */
static int  g_menu_sel, g_cur[2], g_pick[2], g_arena_sel = 1;
static int  g_ladder[16], g_nladder, g_ladder_pos;
static int  g_bank0_front;                      /* sound bank 0 holds frontend.fxl (menus) */
static char g_fname[16][16], g_aname[16][16];   /* fighter / arena names from fi%d.cmp */

/* ---- options (saved in the user's preferences folder) ---- */
typedef struct { int fullscreen, vsync, music, sfx, rounds, round_secs, ringouts, rumble, difficulty; } Config;
static Config g_cfg = { 0, 1, 6, 9, 3, 60, 1, 1, 4 };   /* difficulty: CPU level 0..7 (0x4a587c) */
static SDL_Window *g_win;
static int  g_opt_sel, g_fight_paused;
static SDL_GameController *g_pad[2];            /* game controller of player 1 / 2 */
static int  g_pad_dir[2][2];                    /* last stick direction per pad (menu navigation) */
static int  g_last_health[2];

/* ------------------------------------------------------------------ */
static const uint8_t *getf(const char *fmt, int a, size_t *sz)
{
    char name[128];
    snprintf(name, sizeof name, fmt, a);
    return pac_get(&g_pac, name, sz);
}

static int next_pot(int v) { int p = 1; while (p < v) p <<= 1; return p; }

/* Indexed -> RGBA texture. Non power-of-two images (the 640x200 panoramas) are resampled
 * to power-of-two sizes so they also work on old / software OpenGL implementations. */
static GLuint upload_indexed(const uint8_t *idx, int sw, int sh, const Rgb *pal, int key0)
{
    int w = next_pot(sw), h = next_pot(sh);
    uint8_t *rgba = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sx = x * sw / w, sy = y * sh / h, i = y * w + x;
            uint8_t v = idx[sy * sw + sx];
            Rgb c = pal[v];
            rgba[i*4+0] = c.r; rgba[i*4+1] = c.g; rgba[i*4+2] = c.b;
            rgba[i*4+3] = (key0 && v == 0) ? 0 : 255;
        }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return t;
}

static GLuint load_cmp_tex(const char *fmt, int a, int w, const Rgb *pal)
{
    size_t sz, usz;
    const uint8_t *d = getf(fmt, a, &sz);
    if (!d) return 0;
    uint8_t *px = cmp_decode(d, sz, &usz);
    if (!px) return 0;
    GLuint t = upload_indexed(px, w, (int)(usz / (size_t)w), pal, 0);
    free(px);
    return t;
}

/* ------------------------------------------------------------------ */
static void arena_free(Arena *a)
{
    for (int i = 0; i < a->nparts; i++) model_free(&a->parts[i]);
    if (a->floor_tex) glDeleteTextures(1, &a->floor_tex);
    if (a->bg_tex) glDeleteTextures(1, &a->bg_tex);
    memset(a, 0, sizeof *a);
}

static int arena_load(Arena *a, int id)
{
    arena_free(a);
    a->id = id;
    size_t sz;
    const uint8_t *d = getf("fight/bgrounds/picp%x.cmp", id, &sz);
    if (!d || !pal_load_pixelmap(d, sz, a->pal)) return 0;
    d = getf("fight/floors/floor_h%x.dat", id, &sz);
    if (d) a->nparts = model_load_all(d, sz, a->parts, MAX_PARTS);
    a->floor_tex = load_cmp_tex("fight/floors/floor_h%x.cmp", id, 256, a->pal);
    a->bg_tex = load_cmp_tex("fight/bgrounds/pic%x.cmp", id, 640, a->pal);
    d = getf("fight/fighters/fi%d.cmp", id, &sz);
    if (d) fighter_info_load(&a->fi, d, sz);
    a->land_half = 0;                           /* where a fighter falling out of the ring can land */
    for (int i = 0; i < a->nparts; i++)
        for (int k = 0; k < a->parts[i].nverts; k++) {
            const Vec3 *v = &a->parts[i].verts[k];
            if (fabsf(v->z - a->fi.fall_depth) < 0.25f) {
                float e = fmaxf(fabsf(v->x), fabsf(v->y));
                if (e > a->land_half) a->land_half = e;
            }
        }
    ui_load_bout(&g_pac, a->pal);               /* in-fight messages use the arena palette */
    return 1;
}

static void fighter_free(Fighter *f)
{
    for (int i = 0; i < f->nparts; i++) model_free(&f->parts[i]);
    ani_free(&f->anim);
    moveset_free(&f->moves);
    if (f->tex) glDeleteTextures(1, &f->tex);
    memset(f, 0, sizeof *f);
}

static int fighter_exists(int id)
{
    return getf("fight/fighters/fm%d.dat", id, NULL) && getf("fight/fighters/f%d.ani", id, NULL)
        && getf("fight/fighters/f%d.cmp", id, NULL) && getf("fight/moves/f%d.hdr", id, NULL)
        && getf("fight/moves/f%d.ste", id, NULL);
}

static int fighter_load(Fighter *f, int id, const Rgb *pal)
{
    int clip = f->clip;
    fighter_free(f);
    f->id = id;
    /* Mirror match: player 2 gets the alternate costume (fm%db.dat / f%db.cmp), like FIGHT.EXE. */
    int alt = (f == &g_f[1] && g_f[0].id == id && getf("fight/fighters/fm%db.dat", id, NULL)
               && getf("fight/fighters/f%db.cmp", id, NULL));
    size_t sz;
    const uint8_t *d = getf(alt ? "fight/fighters/fm%db.dat" : "fight/fighters/fm%d.dat", id, &sz);
    if (!d) return 0;
    f->nparts = model_load_all(d, sz, f->parts, MAX_PARTS);
    d = getf("fight/fighters/f%d.ani", id, &sz);
    if (!d || !ani_load(d, sz, &f->anim)) { fprintf(stderr, "bad anim f%d\n", id); return 0; }
    f->tex = load_cmp_tex(alt ? "fight/fighters/f%db.cmp" : "fight/fighters/f%d.cmp", id, 256, pal);
    f->clip = clip < f->anim.nclips ? clip : 0;
    size_t hs, ss;
    const uint8_t *hd = getf("fight/moves/f%d.hdr", id, &hs), *sd = getf("fight/moves/f%d.ste", id, &ss);
    if (hd && sd) moveset_load(&f->moves, hd, hs, sd, ss);
    hd = getf("fight/fighters/fi%d.cmp", id, &hs);
    if (hd) fighter_info_load(&f->fi, hd, hs);
    return 1;
}

static int next_fighter(int id, int dir)
{
    for (int k = 0; k < 20; k++) {
        id += dir;
        if (id < 1) id = 13;
        if (id > 13) id = 1;
        if (fighter_exists(id)) return id;
    }
    return id;
}

/* ------------------------------------------------------------------ */
static void face_normal(const Vec3 *a, const Vec3 *b, const Vec3 *c, float n[3])
{
    float u[3] = { b->x - a->x, b->y - a->y, b->z - a->z };
    float v[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
    n[0] = u[1]*v[2] - u[2]*v[1]; n[1] = u[2]*v[0] - u[0]*v[2]; n[2] = u[0]*v[1] - u[1]*v[0];
    float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (l > 0) { n[0] /= l; n[1] /= l; n[2] /= l; }
}

/* Shading rules reverse engineered from the data:
 *  - Every 256x256 texture (fighters f%d.cmp, floors floor_h%x.cmp) is a 4x4 grid of
 *    64x64 tiles; the i-th model of the .dat file uses tile i, its UVs are 0..1 inside it.
 *  - Materials named TX* are textured, the rest are flat: their colour is palette bank
 *    index_base .. index_base+index_range (BANKn = 10+16*(n-1)), a dark->bright ramp. */
static const Rgb *g_cur_pal;

static void draw_model(const Model *m, GLuint tex, int tile)
{
    const float in = 0.5f / 64.0f;
    float tu = (float)(tile & 3), tv = (float)((tile >> 2) & 3);
    for (int i = 0; i < m->nfaces; i++) {
        const Face *f = &m->faces[i];
        if (f->v[0] >= m->nverts || f->v[1] >= m->nverts || f->v[2] >= m->nverts) continue;
        const char *mn = (f->mat && f->mat <= m->nmats) ? m->mats[f->mat - 1] : "";
        int textured = (mn[0] == 'T' && mn[1] == 'X' && m->uvs && tex);
        const Material *mat = material_find(g_mats, g_nmats, mn);
        if (textured) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glColor3f(1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            if (mat && g_cur_pal && mat->index_range) {
                int idx = mat->index_base + (mat->index_range * 3) / 4;
                Rgb c = g_cur_pal[idx & 255];
                glColor3ub(c.r, c.g, c.b);
            } else if (mat) glColor3ub(mat->r, mat->g, mat->b);
            else glColor3f(0.6f, 0.6f, 0.6f);
        }
        float n[3];
        face_normal(&m->verts[f->v[0]], &m->verts[f->v[1]], &m->verts[f->v[2]], n);
        glBegin(GL_TRIANGLES);
        glNormal3fv(n);
        for (int k = 0; k < 3; k++) {
            int vi = f->v[k];
            if (textured) {
                float u = m->uvs[vi].u, v = m->uvs[vi].v;
                if (!(u >= in)) u = in;          /* also catches NaN / garbage values */
                if (!(v >= in)) v = in;
                if (u > 1 - in) u = 1 - in;
                if (v > 1 - in) v = 1 - in;
                glTexCoord2f((tu + u) * 0.25f, (tv + v) * 0.25f);
            }
            glVertex3f(m->verts[vi].x, m->verts[vi].y, m->verts[vi].z);
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

static void gl_mult_br(float m[4][3])
{
    /* BRender row-vector 4x3 -> OpenGL column-major 4x4 (same memory order). */
    GLfloat g[16] = {
        m[0][0], m[0][1], m[0][2], 0,
        m[1][0], m[1][1], m[1][2], 0,
        m[2][0], m[2][1], m[2][2], 0,
        m[3][0], m[3][1], m[3][2], 1 };
    glMultMatrixf(g);
}

/* A fighter pose: animation frame + world placement (floor position, height, yaw). */
typedef struct { const AniFrame *fr; float wx, wy, wz, yaw; float sx, sy; int flash; } Pose;

static Pose viewer_pose(const Fighter *f, float side)
{
    Pose p = { NULL, 0, side * 0.75f, 0, side > 0 ? 0.f : 180.f, 0, 0, 0 };
    if (!f->anim.nclips) return p;
    const AniClip *cl = &f->anim.clips[f->clip];
    if (!cl->nframes) return p;
    int fi = (int)(f->time * FIGHT_HZ) % cl->nframes;
    float face = side > 0 ? -1.f : 1.f;
    p.fr = &cl->frames[fi];
    p.wy -= face * (float)(p.fr->root[2] - cl->frames[0].root[2]) * ANI_UNIT;
    p.sx = -face * (float)p.fr->root[0] * ANI_UNIT;
    p.sy = p.wy;
    return p;
}

static Pose game_pose(int i)
{
    Pose p;
    memset(&p, 0, sizeof p);
    p.fr = player_frame(&g_fight.p[i]);
    fight_place(&g_fight, i, &p.wx, &p.wy, &p.wz, &p.yaw);
    float pel[3]; fight_joint(&g_fight, i, 7, pel);
    p.sx = pel[0]; p.sy = pel[1];
    p.flash = g_fight.p[i].hit_flash;
    return p;
}

static void draw_fighter(const Fighter *f, Pose p)
{
    if (!f->nparts || !p.fr) return;
    glPushMatrix();
    glTranslatef(p.wx, p.wy, p.wz);
    glRotatef(p.yaw, 0, 0, 1);                    /* models look down local -Y */
    glTranslatef((float)p.fr->root[0] * ANI_UNIT, 0, (float)p.fr->root[1] * ANI_UNIT);
    if (p.flash) { GLfloat e[4] = { 0.5f, 0.1f, 0.05f, 1 }; glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, e); }
    int nb = f->nparts < ANI_MAX_BONES ? f->nparts : ANI_MAX_BONES;
    for (int b = 0; b < nb; b++) {
        float m[4][3];
        ani_bone_matrix(p.fr, b, m);
        glPushMatrix();
        gl_mult_br(m);
        draw_model(&f->parts[b], f->tex, b);
        glPopMatrix();
    }
    if (p.flash) { GLfloat e[4] = { 0, 0, 0, 1 }; glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, e); }
    glPopMatrix();
}

/* fighters/shadow0.dat drawn flat on the floor under the pelvis */
static void draw_shadow(Pose p)
{
    if (!g_shadow.nverts || !p.fr || p.wz < -0.05f) return;
    float x = p.sx;
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-2, -2);
    glColor4f(0, 0, 0, 0.45f);
    glPushMatrix();
    glTranslatef(x, p.sy, 0.003f);
    glScalef(1.6f, 1.6f, 0);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < g_shadow.nfaces; i++)
        for (int k = 0; k < 3; k++) {
            const Vec3 *v = &g_shadow.verts[g_shadow.faces[i].v[k] % g_shadow.nverts];
            glVertex3f(v->x, v->y, 0);
        }
    glEnd();
    glPopMatrix();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* In-fight HUD from boutgfx/<name>.pix (320x240 art, drawn at x2 in the 640x480 virtual screen). */
static void draw_center_msg(const char *name, float y)
{
    ui_draw_c(ui_bout(name), 320, y, 2, 1);
}

static void draw_hud(int w, int h)
{
    ui_begin(w, h);
    const Sprite *pan = ui_bout("panel");
    for (int i = 0; i < 2; i++) {
        const Player *p = &g_fight.p[i];
        float x = i ? 380.f : 16.f, y = 18.f;
        ui_draw_part(pan, 0, 122, x, y, 2, 1);
        float hp = (float)(p->health < 0 ? 0 : p->health) / (float)MAX_HEALTH;
        float ix0 = x + 6, ix1 = x + 236, cut = (ix1 - ix0) * (1.f - hp);
        if (cut > 0) {           /* the bar drains towards the outer edge */
            if (i == 0) ui_rect(ix0, y + 6, ix0 + cut, y + 20, 0.08f, 0.0f, 0.02f, 1);
            else        ui_rect(ix1 - cut, y + 6, ix1, y + 20, 0.08f, 0.0f, 0.02f, 1);
        }
        int need = g_cfg.rounds / 2 + 1;
        for (int k = 0; k < need; k++) {         /* one slot per round needed to win the match */
            float wx = i ? x + k * 24.f : x + 244.f - (float)(k + 1) * 24.f;
            if (p->wins > k) ui_draw_part(pan, 134, 144, wx, y + 30, 2, 1);
            else             ui_draw_part(pan, 122, 134, wx, y + 30, 2, 1);
        }
        char nm[16];
        int id = g_f[i].id;
        snprintf(nm, sizeof nm, "pan_cr%d", id);
        float lx = i ? 624.f - 128.f : 16.f;
        ui_draw(ui_bout(nm), lx, y + 30, 2, 1);
        ui_draw(ui_bout(p->cpu ? "pan_cpu" : (i ? "pan_pl2" : "pan_pl1")), lx, y + 52, 2, 1);
    }
    /* round timer */
    const Sprite *tm = ui_bout("timer");
    int secs = (g_fight.round_timer + FIGHT_HZ - 1) / FIGHT_HZ, x0, x1;
    if (secs > 99) secs = 99;
    int d0 = secs / 10, d1 = secs % 10;
    float tx = 320 - 48;
    if (g_fight.round_secs < 0) {                /* no time limit: the infinity glyph */
        if (ui_timer_digit_x(10, &x0, &x1)) ui_draw_part(tm, x0, x1, 320 - (float)(x1 - x0), 20, 2, 1);
    } else {
        if (ui_timer_digit_x(d0, &x0, &x1)) { ui_draw_part(tm, x0, x1, tx + (float)(24 - (x1 - x0)), 20, 2, 1); }
        if (ui_timer_digit_x(d1, &x0, &x1)) { ui_draw_part(tm, x0, x1, tx + 48, 20, 2, 1); }
    }
    if (g_fight_paused) draw_center_msg("pause", 240);

    /* round intro and result messages */
    int age = g_fight.round_age;
    if (g_fight.winner < 0) {
        if (age > FIGHT_HZ * 3 / 10 && age < FIGHT_HZ * 2) {
            const Sprite *r = ui_bout("round"), *n = ui_bout("rndnumbr");
            int rd = g_round > 9 ? 9 : g_round;
            if (r && n && ui_round_digit_x(rd, &x0, &x1)) {
                float tw = (float)(r->w + 8 + (x1 - x0)) * 2;
                ui_draw(r, 320 - tw * 0.5f, 180, 2, 1);
                ui_draw_part(n, x0, x1, 320 - tw * 0.5f + (float)(r->w + 8) * 2, 180, 2, 1);
            }
        } else if (age >= FIGHT_HZ * 22 / 10 && age < FIGHT_HZ * 32 / 10) draw_center_msg("fight", 200);
    } else {
        int t = 6 * FIGHT_HZ - g_fight.over_timer;   /* ticks since the round ended */
        if (g_match_over_t > 0 && !g_debug_fight && !g_two_players && g_fight.winner == 1) draw_center_msg("gameover", 200);
        else if (t < FIGHT_HZ * 3 / 2)
            draw_center_msg(g_fight.ko_reason == 1 ? "ringout" : g_fight.ko_reason == 2 ? "timeover" : "ko", 200);
        else {
            int h0 = !g_fight.p[0].cpu, h1 = !g_fight.p[1].cpu;
            if (h0 != h1) draw_center_msg((h0 ? g_fight.winner == 0 : g_fight.winner == 1) ? "youwin" : "youlose", 200);
            else {
                char nm[16]; snprintf(nm, sizeof nm, "pan_cr%d", g_f[g_fight.winner].id);
                draw_center_msg("winner", 190);
                ui_draw_c(ui_bout(nm), 320, 236, 3, 1);
            }
        }
    }
    ui_end();
}

/* vertical field of view: the original's 45 degrees at its 1.46 aspect, wider screens see more
 * at the sides, narrower ones keep the original horizontal view */
static float cam_fov_v(float aspect)
{
    if (aspect >= CAM_ASPECT) return CAM_FOV_V;
    return 2.0f * atanf(tanf(CAM_FOV_V * 0.5f / 57.29578f) * CAM_ASPECT / aspect) * 57.29578f;
}

/* rotation-only view matrix looking along d (Z up) */
static void gl_look(const float d[3])
{
    float f[3] = { d[0], d[1], d[2] }, l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int k = 0; k < 3; k++) f[k] /= l;
    float s[3] = { f[1], -f[0], 0 };                       /* f x up */
    l = sqrtf(s[0] * s[0] + s[1] * s[1]); s[0] /= l; s[1] /= l;
    float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    GLfloat m[16] = { s[0], u[0], -f[0], 0,  s[1], u[1], -f[1], 0,  s[2], u[2], -f[2], 0,  0, 0, 0, 1 };
    glMultMatrixf(m);
}

/* The arena backdrop (bgrounds/pic%x.cmp) on boutgfx/bground.dat, which hangs in front of the
 * camera (FUN_0041d5b0): a band 100 units high at depth 145, one copy of the texture per 320
 * units, scrolled by 320 units per full turn of the camera, with sloped strips above and below
 * that repeat the top and bottom rows. Drawn in eye space before the 3D scene. */
static void draw_background(int w, int h)
{
    (void)w; (void)h;
    if (!g_show_bg || !g_arena.bg_tex) return;
    float turn = (g_bg_turn - 90.0f) / 360.0f;
    float xo = (turn - floorf(turn)) * 320.0f;
    float pitch = g_cm_pitch_bg;
    float yo = cosf(pitch / 57.29578f) * 250.0f * 0.5f - 4.5f;      /* rises when looking down */
    if (yo > 40) yo = 40;
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDepthMask(GL_FALSE);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_arena.bg_tex);
    glColor3f(1, 1, 1);
    const float X0 = -400, X1 = 400;
    float u0 = (X0 - xo) / 320.0f, u1 = (X1 - xo) / 320.0f;
    /* rows: y, z, v */
    static const float R[5][3] = { { 125.2f, -74, 0.0015f }, { 54.2f, -145, 0.0015f }, { -46.1f, -145, 0.9985f },
                                   { -117.1f, -74, 0.9985f }, { -183.3f, -7.8f, 0.9985f } };
    glBegin(GL_QUADS);
    for (int r = 0; r < 4; r++) {
        glTexCoord2f(u0, R[r][2]);     glVertex3f(X0, R[r][0] + yo, R[r][1]);
        glTexCoord2f(u1, R[r][2]);     glVertex3f(X1, R[r][0] + yo, R[r][1]);
        glTexCoord2f(u1, R[r + 1][2]); glVertex3f(X1, R[r + 1][0] + yo, R[r + 1][1]);
        glTexCoord2f(u0, R[r + 1][2]); glVertex3f(X0, R[r + 1][0] + yo, R[r + 1][1]);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}

static void draw_background_flat(int w, int h)
{
    if (!g_show_bg || !g_arena.bg_tex) return;
    float u0 = -g_view_yaw / 6.2831853f;
    float aspect = (float)w / (float)(h ? h : 1);
    float span = 2.0f * atanf(tanf(25.0f * 3.14159f / 180.0f) * aspect) / 6.2831853f; /* 640px = 360 deg */
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_arena.bg_tex);
    glColor3f(1, 1, 1);
    float top = 1.0f, bottom = 0.30f - g_pitch * 0.6f;
    glBegin(GL_QUADS);
    glTexCoord2f(u0, 1);        glVertex2f(0, bottom);
    glTexCoord2f(u0 + span, 1); glVertex2f(1, bottom);
    glTexCoord2f(u0 + span, 0); glVertex2f(1, top);
    glTexCoord2f(u0, 0);        glVertex2f(0, top);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

static void render_front(int w, int h);

static void render(int w, int h)
{
    if (g_state != ST_FIGHT) { render_front(w, h); return; }
    glViewport(0, 0, w, h);
    glClearColor(0.02f, 0.02f, 0.06f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!g_game) draw_background_flat(w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)w / (float)(h ? h : 1), zn = 0.05f;
    float th = tanf(cam_fov_v(aspect) * 0.5f / 57.29578f);
    glFrustum(-zn * th * aspect, zn * th * aspect, -zn * th, zn * th, zn, 160.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (g_game) {
        draw_background(w, h);
        gl_look(g_cdir);
        glTranslatef(-g_eye[0], -g_eye[1], -g_eye[2]);
    } else {
        glTranslatef(0, -0.15f, -g_dist);
        glRotatef(-90 + g_pitch * 57.3f, 1, 0, 0);    /* Z up */
        glRotatef(g_view_yaw * 57.3f, 0, 0, 1);
        glTranslatef(0, 0, -0.9f);
    }

    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL); glEnable(GL_NORMALIZE);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    GLfloat lp[4] = { 0.4f, -0.6f, 1.0f, 0 }, amb[4] = { 0.45f, 0.45f, 0.45f, 1 }, dif[4] = { 0.8f, 0.8f, 0.8f, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);

    g_cur_pal = g_arena.pal;
    for (int i = 0; i < g_arena.nparts; i++) draw_model(&g_arena.parts[i], g_arena.floor_tex, i);
    Pose p0 = g_game ? game_pose(0) : viewer_pose(&g_f[0], -1);
    Pose p1 = g_game ? game_pose(1) : viewer_pose(&g_f[1], +1);
    draw_shadow(p0); draw_shadow(p1);
    draw_fighter(&g_f[0], p0);
    draw_fighter(&g_f[1], p1);
    glDisable(GL_LIGHTING);
    if (g_game) draw_hud(w, h);
}

static void update_title(SDL_Window *win)
{
    char t[256];
    if (g_game) {
        snprintf(t, sizeof t, "FX Fighter Turbo (open port) | arena %X | P1 f%d%s %3d HP [%s]  vs  P2 f%d%s %3d HP [%s]",
                 g_arena.id, g_f[0].id, g_fight.p[0].cpu ? " CPU" : "", g_fight.p[0].health, player_move_name(&g_fight.p[0]),
                 g_f[1].id, g_fight.p[1].cpu ? " CPU" : "", g_fight.p[1].health, player_move_name(&g_fight.p[1]));
    } else {
        const char *c0 = g_f[0].anim.nclips ? g_f[0].anim.clips[g_f[0].clip].name : "-";
        snprintf(t, sizeof t, "FX Fighter Turbo (viewer) | arena %X | P1 f%d P2 f%d | clip %d/%d \"%s\"%s",
                 g_arena.id, g_f[0].id, g_f[1].id, g_f[0].clip, g_f[0].anim.nclips, c0, g_paused ? " [paused]" : "");
    }
    SDL_SetWindowTitle(win, t);
}

static char g_pac_dir[1024];

static void remember_pac_dir(const char *path)
{
    snprintf(g_pac_dir, sizeof g_pac_dir, "%s", path);
    char *s1 = strrchr(g_pac_dir, '/'), *s2 = strrchr(g_pac_dir, '\\');
    char *s = s1 > s2 ? s1 : s2;
    if (s) s[1] = 0; else g_pac_dir[0] = 0;
}

static int try_open_pac(int argc, char **argv)
{
    const char *cands[] = { argc > 1 ? argv[1] : NULL, "FIGHT.PAC", "../FIGHT.PAC", "../../FIGHT.PAC", "fight.pac" };
    for (size_t i = 0; i < sizeof cands / sizeof *cands; i++)
        if (cands[i] && pac_open(&g_pac, cands[i])) { printf("Loaded %s (%d files)\n", cands[i], g_pac.count); remember_pac_dir(cands[i]); return 1; }
    char *base = SDL_GetBasePath();
    if (base) {
        char p[1024];
        const char *rel[] = { "FIGHT.PAC", "../FIGHT.PAC", "../../FIGHT.PAC" };
        for (int i = 0; i < 3; i++) {
            snprintf(p, sizeof p, "%s%s", base, rel[i]);
            if (pac_open(&g_pac, p)) { printf("Loaded %s\n", p); remember_pac_dir(p); SDL_free(base); return 1; }
        }
        SDL_free(base);
    }
    return 0;
}

static void restart_fight(void)
{
    for (int i = 0; i < 2; i++)
    {
        g_fight.p[i].mesh = g_f[i].parts; g_fight.p[i].nmesh = g_f[i].nparts;
        fight_init_player(&g_fight.p[i], &g_f[i].moves, &g_f[i].anim, &g_generic, &g_f[i].fi);
    }
    fight_set_arena(&g_fight, &g_arena.fi);
    g_fight.land_half = g_arena.land_half;
    g_fight.ringouts = g_ringouts;
    fight_reset(&g_fight);
    g_yaw_ofs = 0;
    music_play(g_arena.id + 2);                 /* FIGHT.EXE: CD track = arena + 2 */
    sfx_bank_load(0, "generic");
    g_bank0_front = 0;
    sfx_bank_load(1, g_f[0].fi.name[0] ? g_f[0].fi.name : "magnon");
    sfx_bank_load(2, g_f[1].fi.name[0] ? g_f[1].fi.name : "magnon");
    { char bn[8]; snprintf(bn, sizeof bn, "b%d", g_arena.id); sfx_bank_load(3, bn); }
}

/* Keyboard -> FX input bits. Screen left/right are converted to forward/back
 * using the camera orientation and the fighter's facing. */
static void read_input(void)
{
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    static const SDL_Scancode keys[2][6] = {
        { SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_F, SDL_SCANCODE_G },
        { SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_K, SDL_SCANCODE_L } };
    float ux, uy; fight_line_dir(&g_fight, &ux, &uy);
    float screen_right_u = cosf(g_view_yaw) * ux - sinf(g_view_yaw) * uy;   /* screen right . line dir */
    for (int i = 0; i < 2; i++) {
        Player *p = &g_fight.p[i];
        if (p->cpu) continue;
        unsigned in = 0;
        int right = ks[keys[i][1]] ? 1 : 0, left = ks[keys[i][0]] ? 1 : 0;
        float dir = (float)(right - left) * (screen_right_u >= 0 ? 1.f : -1.f);   /* along the fight line */
        if (dir * (float)p->face > 0) in |= IN_FWD;
        if (dir * (float)p->face < 0) in |= IN_BACK;
        if (ks[keys[i][2]]) in |= IN_UP;
        if (ks[keys[i][3]]) in |= IN_DOWN;
        if (ks[keys[i][4]]) in |= IN_PUNCH;
        if (ks[keys[i][5]]) in |= IN_KICK;
        SDL_GameController *gc = g_pad[i];
        if (gc) {                                /* game controller: d-pad or left stick, X/Y punch, A/B kick */
            int ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
            int ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
            int pr = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 14000;
            int pl = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -14000;
            float pd = (float)(pr - pl) * (screen_right_u >= 0 ? 1.f : -1.f);
            if (pd * (float)p->face > 0) in |= IN_FWD;
            if (pd * (float)p->face < 0) in |= IN_BACK;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -14000) in |= IN_UP;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 14000) in |= IN_DOWN;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X) || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y) ||
                SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) in |= IN_PUNCH;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A) || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) ||
                SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) in |= IN_KICK;
            if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) in |= IN_PUNCH;
            if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) in |= IN_KICK;
        }
        p->in = in;
    }
}

/* ---- game controllers ---- */
static void pad_added(int device)
{
    if (!SDL_IsGameController(device)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
    for (int i = 0; i < 2; i++)
        if (g_pad[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad[i])) == id) return;
    for (int i = 0; i < 2; i++)
        if (!g_pad[i]) {
            g_pad[i] = SDL_GameControllerOpen(device);
            if (g_pad[i]) printf("controller %d: %s\n", i + 1, SDL_GameControllerName(g_pad[i]));
            return;
        }
}

static void pad_removed(SDL_JoystickID id)
{
    for (int i = 0; i < 2; i++)
        if (g_pad[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad[i])) == id) {
            SDL_GameControllerClose(g_pad[i]); g_pad[i] = NULL;
        }
}

static int pad_slot(SDL_JoystickID id)
{
    for (int i = 0; i < 2; i++)
        if (g_pad[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad[i])) == id) return i;
    return -1;
}

/* controller -> the keys of that player, so menus work the same way */
static void push_key(SDL_Keycode k)
{
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = k; e.key.repeat = 0; e.key.state = SDL_PRESSED;
    SDL_PushEvent(&e);
}

static void pad_button(int slot, int button)
{
    static const SDL_Keycode dir[2][4] = { { SDLK_w, SDLK_s, SDLK_a, SDLK_d }, { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT } };
    SDL_Keycode ok = slot ? SDLK_k : SDLK_f;
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    if (g_state != ST_FIGHT) push_key(dir[slot][0]); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  if (g_state != ST_FIGHT) push_key(dir[slot][1]); break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  if (g_state != ST_FIGHT) push_key(dir[slot][2]); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: if (g_state != ST_FIGHT) push_key(dir[slot][3]); break;
    case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_X: if (g_state != ST_FIGHT) push_key(ok); break;
    case SDL_CONTROLLER_BUTTON_B: if (g_state != ST_FIGHT) push_key(SDLK_BACKSPACE); break;
    case SDL_CONTROLLER_BUTTON_START: push_key(g_state == ST_FIGHT ? SDLK_p : ok); break;
    case SDL_CONTROLLER_BUTTON_BACK: push_key(SDLK_ESCAPE); break;
    default: break;
    }
}

static void pad_axis(int slot, int axis, int value)
{
    if (g_state == ST_FIGHT || (axis != SDL_CONTROLLER_AXIS_LEFTX && axis != SDL_CONTROLLER_AXIS_LEFTY)) return;
    int a = axis == SDL_CONTROLLER_AXIS_LEFTY, d = value > 20000 ? 1 : value < -20000 ? -1 : 0;
    if (d == g_pad_dir[slot][a]) return;
    g_pad_dir[slot][a] = d;
    if (!d) return;
    if (a) pad_button(slot, d < 0 ? SDL_CONTROLLER_BUTTON_DPAD_UP : SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    else   pad_button(slot, d < 0 ? SDL_CONTROLLER_BUTTON_DPAD_LEFT : SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
}

/* rumble the controller of a fighter who just took damage */
static void pad_rumble_hits(void)
{
    for (int i = 0; i < 2; i++) {
        int h = g_fight.p[i].health;
        if (g_cfg.rumble && g_pad[i] && h < g_last_health[i]) {
            int dmg = g_last_health[i] - h;
            Uint16 s = (Uint16)(dmg > 20 ? 0xffff : 0x5000 + dmg * 0x900);
            SDL_GameControllerRumble(g_pad[i], s, s, dmg > 20 ? 320 : 160);
        }
        g_last_health[i] = h;
    }
}

/* ---- options file ---- */
static void cfg_path(char *out, size_t n)
{
    char *pp = SDL_GetPrefPath("FXFighter", "Turbo");
    snprintf(out, n, "%soptions.cfg", pp ? pp : "");
    if (pp) SDL_free(pp);
}

static void cfg_load(void)
{
    char p[1100]; cfg_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char k[64]; int v;
    while (fscanf(f, " %63[^=]=%d", k, &v) == 2) {
        if (!strcmp(k, "fullscreen")) g_cfg.fullscreen = v != 0;
        else if (!strcmp(k, "vsync")) g_cfg.vsync = v != 0;
        else if (!strcmp(k, "music")) g_cfg.music = v < 0 ? 0 : v > 10 ? 10 : v;
        else if (!strcmp(k, "sfx")) g_cfg.sfx = v < 0 ? 0 : v > 10 ? 10 : v;
        else if (!strcmp(k, "rounds")) g_cfg.rounds = (v == 1 || v == 5) ? v : 3;
        else if (!strcmp(k, "round_time")) g_cfg.round_secs = (v == 30 || v == 90 || v == 99 || v == -1) ? v : 60;
        else if (!strcmp(k, "ringouts")) g_cfg.ringouts = v != 0;
        else if (!strcmp(k, "rumble")) g_cfg.rumble = v != 0;
        else if (!strcmp(k, "difficulty")) g_cfg.difficulty = v < 0 ? 0 : v > 7 ? 7 : v;
    }
    fclose(f);
}

static void cfg_save(void)
{
    char p[1100]; cfg_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "fullscreen=%d\nvsync=%d\nmusic=%d\nsfx=%d\nrounds=%d\nround_time=%d\nringouts=%d\nrumble=%d\ndifficulty=%d\n",
            g_cfg.fullscreen, g_cfg.vsync, g_cfg.music, g_cfg.sfx, g_cfg.rounds, g_cfg.round_secs, g_cfg.ringouts, g_cfg.rumble,
            g_cfg.difficulty);
    fclose(f);
}

static void cfg_apply(void)
{
    if (g_win) {
        int fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        if (fs != g_cfg.fullscreen) SDL_SetWindowFullscreen(g_win, g_cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    SDL_GL_SetSwapInterval(g_cfg.vsync ? 1 : 0);
    music_set_volume((float)g_cfg.music / 10.0f);
    sfx_set_volume((float)g_cfg.sfx / 10.0f);
    g_ringouts = g_cfg.ringouts;
    g_fight.ai_level = g_cfg.difficulty;
}

/* Game camera (FUN_004231c0): perpendicular to the fight line, distance and height from the
 * distance between the fighters, the line angle followed with a dead zone. */

static float wrap180(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

/* camera presets 0x4a4be8: yaw offset, angle from the vertical, distance, rate (first 20) */
static const float CAM_PRESET[20][4] = {
    { 90, 95, 3, 32 }, { 180, -5, 30, 24 }, { 0, -5, 30, 24 }, { 180, 80, 20, 32 }, { 0, 80, 20, 32 },
    { 270, 50, 34, 32 }, { -90, 50, 34, 32 }, { 90, 40, 32, 32 }, { 96, 90, 8, 32 }, { 138, 78, 8, 32 },
    { 60, 78, 8, 32 }, { 90, 62, 6, 32 }, { 144, 62, 6, 32 }, { 46, 62, 6, 32 }, { 162, 74, 4, 32 },
    { 36, 74, 4, 32 }, { 5, 60, 2, 32 }, { -252, 66, 3, 32 }, { 54, 66, 3, 32 }, { -126, 48, 2, 32 } };
static float cam_preset(int k, int j)
{
    if (k >= 0 && k < 20) return CAM_PRESET[k][j];
    /* 20..127 (same table) */
    static const float T[108][3] = {
        {10,84,2},{108,47,3},{48,65,3},{2,90,2},{-36,60,2},{-204,54,3},{-18,45,3},{-252,66,3},{132,42,2},{150,40,3},
        {30,60,3},{-33,72,2.4f},{-18,54,3},{150,60,3},{42,27,3},{-6,78,2.6f},{120,70,3},{150,70,3},{30,30,4},{60,30,4},
        {120,30,4},{150,30,4},{30,40,4},{60,40,4},{120,40,4},{150,40,4},{30,60,4},{60,60,4},{120,60,4},{150,60,4},
        {-6,90,2.5f},{60,70,4},{120,70,4},{150,70,4},{30,30,5},{6,90,3},{120,30,5},{150,30,5},{30,40,5},{60,40,5},
        {120,40,5},{150,40,5},{30,60,5},{60,60,5},{120,60,5},{150,60,5},{30,70,5},{60,70,5},{120,70,5},{150,70,5},
        {30,30,8},{60,30,8},{120,30,8},{150,30,8},{30,40,8},{60,40,8},{120,40,8},{150,40,8},{50,75,3.2f},{60,40,5},
        {120,40,5},{150,40,5},{30,60,5},{60,60,5},{120,60,5},{150,60,5},{30,70,5},{60,70,5},{120,70,5},{150,70,5},
        {30,30,8},{60,30,8},{120,30,8},{150,30,8},{30,40,8},{60,40,8},{120,40,8},{150,40,8},{50,75,3.2f},{60,40,5},
        {120,40,5},{150,40,5},{30,60,5},{60,60,5},{120,60,5},{150,60,5},{30,70,5},{60,70,5},{120,70,5},{150,70,5},
        {30,30,8},{60,30,8},{120,30,8},{150,30,8},{30,40,8},{60,40,8},{120,40,8},{150,40,8},{50,75,3.2f},{60,40,5},
        {120,40,5},{150,40,5},{30,60,5},{60,60,5},{120,60,5},{150,60,5},{30,70,5},{60,70,5} };
    if (k >= 20 && k < 128) return j < 3 ? T[k - 20][j] : 32.f;
    return CAM_PRESET[0][j];
}

/* camera direction (FUN_0042106c / the move camera bytes in FUN_00420e80):
 * mode 0 follows the fight, mode 2 eases the angles back, mode 1 eases everything to a preset */
static int   g_cm_mode, g_cm_auto, g_cm_focus = -1, g_cm_preset, g_cm_hold, g_cm_ringcam;
static float g_cm_yaw = 90, g_cm_pitch = 88, g_cm_dist = 3.4f, g_cm_ty, g_cm_tp, g_cm_td, g_cm_rate = 32;
static int   g_cm_edge;                     /* 4a4bc8 1/2: ring edge view active (FUN_004221b2) */
/* fixed views (FUN_00421728): Home toggles, Insert / Delete pick one of the presets 8..15,
 * PageUp / PageDown distance, arrows (when player 2 is the CPU) turn and tilt; Shift = fine */
static int   g_fixed_view, g_fixed_k = 8;
static float g_fixed[8][3] = { { 96, 90, 8 }, { 138, 78, 8 }, { 60, 78, 8 }, { 90, 62, 6 },
                               { 144, 62, 6 }, { 46, 62, 6 }, { 162, 74, 4 }, { 36, 74, 4 } };
static float g_shake_amp, g_shake_dec, g_shake_ph;   /* 4a4bd4 / 4a4bd8 / 4a4bdc */

static void fixed_view_key(SDL_Keycode k, int fine)
{
    float *v = g_fixed[g_fixed_k - 8];
    switch (k) {
    case SDLK_HOME:     g_fixed_view = !g_fixed_view; break;
    case SDLK_INSERT:   g_fixed_k = ((g_fixed_k + 1) & 7) + 8; break;
    case SDLK_DELETE:   g_fixed_k = ((g_fixed_k - 1) & 7) + 8; break;
    case SDLK_PAGEUP:   v[2] -= fine ? 0.1f : 1.f; if (v[2] < 2) v[2] = 2; break;
    case SDLK_PAGEDOWN: v[2] += fine ? 0.1f : 1.f; if (v[2] > 8) v[2] = 8; break;
    case SDLK_LEFT:     v[0] -= fine ? 2.f : 5.f; if (v[0] <= 0) v[0] += 360; break;
    case SDLK_RIGHT:    v[0] += fine ? 2.f : 5.f; if (v[0] > 360) v[0] -= 360; break;
    case SDLK_UP:       v[1] -= fine ? 2.f : 5.f; if (v[1] <= 30) v[1] = 30; break;
    case SDLK_DOWN:     v[1] += fine ? 2.f : 5.f; if (v[1] > 80) v[1] = 80; break;
    default: break;
    }
}

static void cam_set_preset(int k, int focus, int mode)
{
    g_cm_mode = mode; g_cm_focus = focus; g_cm_auto = (k == 0);
    g_cm_ty = cam_preset(k, 0); g_cm_tp = cam_preset(k, 1); g_cm_td = cam_preset(k, 2); g_cm_rate = cam_preset(k, 3);
    if (focus == 1) g_cm_ty = 180.0f - g_cm_ty;          /* mirrored for the fighter on the right */
}

static float ease(float cur, float tgt, float rate, float snap_step)
{
    float st = (tgt - cur) / (rate > 0.5f ? rate : 0.5f);
    if (fabsf(st) > snap_step || fabsf(tgt - cur) < 0.05f) return tgt;
    return cur + st;
}

static void camera_step(int snap)
{
    float x0, y0, z0, a0, x1, y1, z1, a1;
    fight_place(&g_fight, 0, &x0, &y0, &z0, &a0);
    fight_place(&g_fight, 1, &x1, &y1, &z1, &a1);
    float ux, uy; fight_line_dir(&g_fight, &ux, &uy);
    float dx = x1 - x0, dy = y1 - y0;
    if (dx * ux + dy * uy < 0) { ux = -ux; uy = -uy; }    /* keep player 1 on the left */
    /* distance between the fighters (with the higher jump), dead zone 0.01 */
    float zz = z0 > z1 ? z0 : z1; if (zz < 0) zz = 0;
    float D = sqrtf(dx * dx + dy * dy + zz * zz);
    if (snap || fabsf(D - g_cam_D) > 0.01f) g_cam_D = D;
    /* line angle: starts following when more than 30 degrees off, 1/32 of the error per tick */
    float target = atan2f(uy, ux) * 57.29578f;
    if (snap) { g_cam_line = target; g_cam_track = 0; g_cm_mode = 0; g_cm_preset = 0; g_cm_ringcam = 0; g_cm_focus = -1; g_cm_edge = 0; }
    float d = wrap180(target - g_cam_line);
    if (d < -30 || d > 30) g_cam_track = 1;
    if (g_cam_track) {
        g_cam_line += d / 32.0f;
        if (d > -1 && d < 1) { g_cam_line = target; g_cam_track = 0; }
    }
    g_cam_line = wrap180(g_cam_line);

    /* camera presets of the moves being played (throws, victory, defeat) */
    int mc = 0, who = 0;
    for (int i = 0; i < 2 && !mc; i++) if ((mc = player_move_cam(&g_fight.p[i])) != 0) who = i;
    if (mc) {
        int k = mc & 0x7f;
        if (k != g_cm_preset) {
            if ((mc & 0x40) && k >= 0x40) k = ((rand() & 7) + (mc & 0x38)) & 0x7f;
            g_cm_preset = mc & 0x7f;
            cam_set_preset(k, (mc & 0x80) || k >= 0x3c ? who : -1, (mc & 0x80) || k >= 0x3c ? 1 : 2);
        }
        g_cm_hold = 0;
    } else if (g_cm_preset && ++g_cm_hold > FIGHT_HZ) {
        g_cm_preset = 0; cam_set_preset(0, -1, 2);
    }
    /* ring edge (FUN_004221b2): a fighter within 1.8 of the edge and the two closer than 4.0:
     * the camera swings by 12 * (3.5 - gap) degrees and drops to 83 - 8 * (3.5 - gap) */
    if (!mc && !g_cm_preset && !g_fight.p[0].ringout && !g_fight.p[1].ringout && !snap) {
        float lim = g_fight.ring_half - 1.8f;
        int e = 0;
        if (fabsf(x0) > lim || fabsf(y0) > lim) e |= 1;
        if (fabsf(x1) > lim || fabsf(y1) > lim) e |= 2;
        if (e == 3) e = (x0 * x0 + y0 * y0 <= x1 * x1 + y1 * y1) ? 2 : 1;   /* the one further out */
        float gp = fabsf(g_fight.p[1].s - g_fight.p[0].s);
        if ((e == 1 || e == 2) && gp < 4.0f) {
            float a = 3.5f - gp; if (a < 0) a = 0;
            /* player 1 is always on the left here, which the original gets by flipping the sign */
            g_cm_mode = 2; g_cm_auto = 0; g_cm_focus = -1; g_cm_rate = 32;
            g_cm_ty = 90.f + (e == 1 ? -12.f : 12.f) * a;
            g_cm_tp = 83.f - 8.f * a;
            if (getenv("FX_CAMTRACE") && g_cm_edge != e) printf("%d edge view: P%d gap %.2f -> yaw %.0f pitch %.0f\n", g_fight.round_age, e, gp, g_cm_ty, g_cm_tp);
            g_cm_edge = e;
        } else if (g_cm_edge) {
            if (getenv("FX_CAMTRACE")) printf("%d edge view off (gap %.2f)\n", g_fight.round_age, gp);
            g_cm_edge = 0; cam_set_preset(0, -1, 2);           /* FUN_00420f87(0, 2): ease back */
        }
    }
    /* falling out of the ring: look at the faller */
    for (int i = 0; i < 2; i++)
        if (g_fight.p[i].ringout && !g_cm_ringcam && !mc) { g_cm_ringcam = 1; g_cm_preset = 0x7f; cam_set_preset(11, i, 1);
            g_cm_tp = 45; g_cm_td = 5.5f; g_cm_rate = 16; g_cm_hold = -3 * FIGHT_HZ; }   /* steeper than preset 11 to see the fall */
    if (!g_fight.p[0].ringout && !g_fight.p[1].ringout) g_cm_ringcam = 0;

    float auto_pitch = 95.0f - g_cam_D / 0.3f; if (auto_pitch < 0) auto_pitch = 0;
    float auto_dist = 0.9f * g_cam_D + 1.6f;
    if (snap || g_cm_mode == 0) { g_cm_yaw = 90; g_cm_pitch = auto_pitch; g_cm_dist = auto_dist; g_cm_focus = -1; }
    else if (g_cm_mode == 2) {
        float tp = g_cm_auto ? auto_pitch : g_cm_tp;
        g_cm_yaw = ease(g_cm_yaw, g_cm_ty, g_cm_rate, 45);
        g_cm_pitch = ease(g_cm_pitch, tp, g_cm_rate, 15);
        g_cm_dist = auto_dist;
        if (g_cm_auto && fabsf(g_cm_yaw - g_cm_ty) < 0.1f && fabsf(g_cm_pitch - tp) < 0.1f) g_cm_mode = 0;
    } else {
        g_cm_yaw = ease(g_cm_yaw, g_cm_ty, g_cm_rate, 45);
        g_cm_pitch = ease(g_cm_pitch, g_cm_tp, g_cm_rate, 15);
        g_cm_dist = ease(g_cm_dist, g_cm_td, g_cm_rate, 15);
    }
    float yaw = g_cm_yaw, cpitch = g_cm_pitch, cdist = g_cm_dist;
    if (g_fixed_view && g_fight.winner < 0) {               /* 4a4be0: fixed view (off for the round end) */
        const float *v = g_fixed[g_fixed_k - 8];
        yaw = v[0]; cpitch = v[1]; cdist = v[2]; g_cm_focus = -1;
    }
    if (g_fight.cam_shake) {                               /* self damage: vertical shake */
        g_fight.cam_shake = 0;
        g_shake_amp = 0.1f; g_shake_dec = 0.008f; g_shake_ph = 0;
    }
    float shake = 0;
    if (g_shake_amp > 0) {
        shake = sinf(g_shake_ph / 57.29578f) * g_shake_amp;
        g_shake_amp -= g_shake_dec;
        g_shake_ph += 45.f;
    }
    /* centre: between the fighters, or on the fighter the preset focuses on */
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    if (g_cm_focus == 0) { cx = x0; cy = y0; }
    if (g_cm_focus == 1) { cx = x1; cy = y1; }
    if (snap) { g_cam_x = cx; g_cam_y = cy; }
    else { g_cam_x += (cx - g_cam_x) * 0.15f; g_cam_y += (cy - g_cam_y) * 0.15f; }

    float zoom = g_dist / 3.6f;
    float dist = cdist * zoom;
    float pitch = cpitch - (g_pitch - 0.28f) * 57.3f;      /* mouse: tilt */
    float pr = pitch / 57.29578f;
    float ang = (g_cam_line - (yaw - 90.0f) + g_yaw_ofs * 57.29578f) / 57.29578f;
    float fx = -sinf(ang), fy = cosf(ang);               /* towards the scene, line to the right */
    float horiz = sinf(pr) * dist;
    g_eye[0] = g_cam_x - fx * horiz;
    g_eye[1] = g_cam_y - fy * horiz;
    g_eye[2] = cosf(pr) * dist + 0.5f + shake;
    /* camera tilt: RotX(pitch - 4 deg) . RotX(0x9c4 = 13.73 deg) from looking straight down */
    float elev = (pitch - 4.0f + 13.733f - 90.0f) / 57.29578f;
    g_cdir[0] = fx * cosf(elev); g_cdir[1] = fy * cosf(elev); g_cdir[2] = sinf(elev);
    g_view_yaw = -ang;
    g_bg_turn = ang * 57.29578f;
    g_cm_pitch_bg = pitch;
}

static void update_camera(float dt, int snap)
{
    (void)dt;
    if (!g_game) { g_view_yaw = g_yaw; return; }
    if (snap) camera_step(1);
}

/* ================================================================== */
/*  Front end (winfront.c in the original): title, menu, fighter and   */
/*  arena select, VS screen, best-of-three matches and the 1P ladder.  */
/* ================================================================== */
/* frontend/align.pix portrait of each fighter id (matched by eye; 12 = random "?" cell) */
static const char *PORTRAIT[13] = { "EMPTY", "MAGNON", "PLYR7", "PLYR10", "PLYR1", "PLYR6", "PLYR5",
                                    "PLYR4", "PLYR8", "PLYR3", "PLYR2", "PLYR9", "PLYR11" };
#define GRID_N    12
#define GRID_COLS 3
static const int GRID[GRID_N] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0 };   /* 0 = random */
enum { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_BACK };
enum { FS_MOVE = 1, FS_MOVE2 = 2, FS_SELECT = 3, FS_DENY = 4, FS_VERSUS = 11 };   /* frontend.fxl */

static void set_state(int s)
{
    g_state = s; g_state_t = 0;
    if (getenv("FX_AUTOTEST")) { printf("state %d round %d wins %d:%d ladder %d\n", s, g_round, g_fight.p[0].wins, g_fight.p[1].wins, g_ladder_pos); fflush(stdout); }
    if (s != ST_FIGHT) {
        if (!g_bank0_front) { sfx_bank_load(0, "frontend"); g_bank0_front = 1; }
        if (s != ST_VS && s != ST_TITLE) music_play(14);       /* attract / menu music */
    }
}

static void load_names(void)
{
    for (int id = 1; id <= 12; id++) {
        size_t sz; FighterInfo fi;
        const uint8_t *d = getf("fight/fighters/fi%d.cmp", id, &sz);
        if (d && fighter_info_load(&fi, d, sz)) {
            snprintf(g_fname[id], sizeof g_fname[id], "%s", fi.name);
            snprintf(g_aname[id], sizeof g_aname[id], "%s", fi.arena);
        }
    }
    snprintf(g_fname[12], sizeof g_fname[12], "JERZAN");         /* fi12 is a copy of Magnon's; name from pan_cr12 */
    snprintf(g_aname[0], sizeof g_aname[0], "ARENA");
}

static void begin_match(int p1, int p2, int arena, int cpu1, int cpu2)
{
    arena_load(&g_arena, arena);
    g_f[0].id = 0;                                  /* fighter_load() checks g_f[0] for the mirror costume */
    fighter_load(&g_f[0], p1, g_arena.pal);
    fighter_load(&g_f[1], p2, g_arena.pal);
    g_fight.p[0].wins = g_fight.p[1].wins = 0;
    g_fight.p[0].cpu = cpu1 || getenv("FX_AUTOTEST"); g_fight.p[1].cpu = cpu2 && !getenv("FX_AUTOWIN");
    g_fight.hold = 1;
    g_fight.start_delay = FIGHT_HZ * 22 / 10;      /* ROUND n ... FIGHT! */
    g_fight.round_secs = g_cfg.round_secs;
    g_ringouts = g_cfg.ringouts;
    g_fight.ai_level = g_cfg.difficulty;
    g_fight_paused = 0;
    g_round = 1; g_match_over_t = 0;
    g_game = 1; g_yaw = -1.5708f; g_pitch = 0.28f; g_dist = 3.6f;
    restart_fight();
    update_camera(0, 1);
    set_state(ST_FIGHT);
}

/* 1 player ladder (0x496150, built by the function around 0x41a6xx): 4 orders per fighter,
 * 7 regulars then the bosses 10, 11, 9 (fighter 9 meets 10, 11 last). The rows of fighter 12
 * (JERZAN) are empty: the original never sends him; fm12.dat only has head, hands and feet. */
static const uint8_t LADDER[11][4][10] = {
    { {6,3,5,8,2,4,7,10,11,9}, {6,2,4,7,3,5,8,10,11,9}, {7,4,6,3,5,2,8,10,11,9}, {7,5,2,8,4,6,3,10,11,9} },
    { {7,8,3,1,5,4,6,10,11,9}, {7,5,4,6,8,3,1,10,11,9}, {5,6,3,7,1,4,8,10,11,9}, {5,1,4,8,6,3,7,10,11,9} },
    { {2,7,1,4,8,5,6,10,11,9}, {2,8,5,6,7,1,4,10,11,9}, {8,6,4,2,1,5,7,10,11,9}, {8,1,5,7,6,4,2,10,11,9} },
    { {3,5,1,6,2,7,8,10,11,9}, {3,2,7,8,5,1,6,10,11,9}, {8,2,5,3,1,6,7,10,11,9}, {8,1,6,7,2,5,3,10,11,9} },
    { {2,4,8,1,3,7,6,10,11,9}, {2,3,7,6,4,8,1,10,11,9}, {1,4,3,2,7,8,6,10,11,9}, {1,7,8,6,4,3,2,10,11,9} },
    { {4,7,2,5,8,1,3,10,11,9}, {4,8,1,3,7,2,5,10,11,9}, {5,4,7,2,1,8,3,10,11,9}, {5,1,8,3,4,7,2,10,11,9} },
    { {6,2,8,1,5,4,3,10,11,9}, {6,5,4,3,2,8,1,10,11,9}, {4,3,1,8,2,6,5,10,11,9}, {4,2,6,5,3,1,8,10,11,9} },
    { {3,7,5,1,2,4,6,10,11,9}, {3,2,4,6,7,5,1,10,11,9}, {1,5,3,4,7,2,6,10,11,9}, {1,7,2,6,5,3,4,10,11,9} },
    { {8,5,4,7,6,2,1,3,10,11}, {8,6,2,1,3,5,4,7,10,11}, {6,1,5,7,8,2,4,3,10,11}, {6,8,2,4,3,1,5,7,10,11} },
    { {7,2,8,6,4,3,1,5,11,9}, {7,4,3,1,5,2,8,6,11,9}, {2,7,5,1,6,4,3,8,11,9}, {2,6,4,3,8,7,5,1,11,9} },
    { {3,7,2,6,4,8,1,5,10,9}, {3,4,8,1,5,7,2,6,10,9}, {1,5,3,4,7,2,8,6,10,9}, {1,7,2,8,6,5,3,4,10,9} },
};

static void build_ladder(int player)
{
    g_nladder = 0;
    if (player >= 1 && player <= 11) {
        int v = rand() & 3;                         /* the original picks one of the 4 orders at random */
        for (int k = 0; k < 10; k++) {
            int id = LADDER[player - 1][v][k];
            if (id != player && fighter_exists(id)) g_ladder[g_nladder++] = id;
        }
    } else {
        for (int id = 1; id <= 11; id++) if (id != player && fighter_exists(id)) g_ladder[g_nladder++] = id;
        for (int i = g_nladder - 1; i > 0; i--) { int j = rand() % (i + 1), t = g_ladder[i]; g_ladder[i] = g_ladder[j]; g_ladder[j] = t; }
    }
    g_ladder_pos = 0;
}

static int vs_opponent(void) { return g_two_players ? g_pick[1] : g_ladder[g_ladder_pos]; }

static void start_vs_fight(void)
{
    if (g_two_players) begin_match(g_pick[0], g_pick[1], g_arena_sel, 0, 0);
    else { int o = vs_opponent(); begin_match(g_pick[0], o, o <= 11 ? o : 0, 0, 1); }
}

static void after_match(void)
{
    if (g_debug_fight) {                            /* command line fight: keep going */
        g_fight.p[0].wins = g_fight.p[1].wins = 0; g_round = 1;
        fight_reset(&g_fight); g_yaw_ofs = 0; camera_step(1);
        return;
    }
    if (g_two_players) { g_pick[0] = g_pick[1] = 0; set_state(ST_SELECT); return; }
    if (g_fight.winner == 0) {
        if (++g_ladder_pos < g_nladder) { set_state(ST_VS); return; }
        set_state(ST_TITLE); music_play(2);         /* beat everyone: credits music */
        return;
    }
    set_state(ST_MENU);                             /* game over */
}

/* called once per FIGHT_HZ tick while fighting */
static void camera_step(int snap);
static void match_tick(void)
{
    if (g_match_over_t > 0) { if (--g_match_over_t == 0) after_match(); return; }
    if (g_fight.winner >= 0 && g_fight.over_timer <= 0) {
        int need = g_cfg.rounds / 2 + 1;
        if (g_fight.p[0].wins >= need || g_fight.p[1].wins >= need) g_match_over_t = 3 * FIGHT_HZ;
        else { g_round++; fight_reset(&g_fight); g_yaw_ofs = 0; camera_step(1); }
    }
}

static int menu_key(SDL_Keycode k, int *who)
{
    *who = 0;
    switch (k) {
    case SDLK_w: return K_UP;
    case SDLK_s: return K_DOWN;
    case SDLK_a: return K_LEFT;
    case SDLK_d: return K_RIGHT;
    case SDLK_f: case SDLK_g: case SDLK_SPACE: return K_OK;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: *who = -1; return K_BACK;
    default: break;
    }
    *who = 1;
    switch (k) {
    case SDLK_UP: return K_UP;
    case SDLK_DOWN: return K_DOWN;
    case SDLK_LEFT: return K_LEFT;
    case SDLK_RIGHT: return K_RIGHT;
    case SDLK_k: case SDLK_l: case SDLK_RETURN: case SDLK_KP_ENTER: return K_OK;
    default: return K_NONE;
    }
}

static int random_fighter(void)
{
    int ids[12], n = 0;
    for (int id = 1; id <= 11; id++) if (fighter_exists(id)) ids[n++] = id;
    return n ? ids[rand() % n] : 1;
}

/* ---- options screen ---- */
enum { OPT_DISPLAY, OPT_VSYNC, OPT_MUSIC, OPT_SFX, OPT_ROUNDS, OPT_TIME, OPT_RINGOUT, OPT_DIFF, OPT_RUMBLE, OPT_BACK, OPT_N };
static const char *OPT_LABEL[OPT_N] = { "DISPLAY", "VSYNC", "MUSIC", "SOUND FX", "ROUNDS", "ROUND TIME", "RING OUT", "DIFFICULTY", "RUMBLE", "BACK" };

static void option_value(int i, char *out, size_t n)
{
    switch (i) {
    case OPT_DISPLAY: snprintf(out, n, "%s", g_cfg.fullscreen ? "FULLSCREEN" : "WINDOW"); break;
    case OPT_VSYNC:   snprintf(out, n, "%s", g_cfg.vsync ? "ON" : "OFF"); break;
    case OPT_MUSIC:   snprintf(out, n, "%d", g_cfg.music); break;
    case OPT_SFX:     snprintf(out, n, "%d", g_cfg.sfx); break;
    case OPT_ROUNDS:  snprintf(out, n, "BEST OF %d", g_cfg.rounds); break;
    case OPT_TIME:    if (g_cfg.round_secs < 0) snprintf(out, n, "NO LIMIT"); else snprintf(out, n, "%d", g_cfg.round_secs); break;
    case OPT_RINGOUT: snprintf(out, n, "%s", g_cfg.ringouts ? "ON" : "OFF"); break;
    case OPT_DIFF:    snprintf(out, n, "%d", g_cfg.difficulty + 1); break;
    case OPT_RUMBLE:  snprintf(out, n, "%s", g_cfg.rumble ? "ON" : "OFF"); break;
    default: out[0] = 0;
    }
}

static void option_change(int i, int d)
{
    static const int times[5] = { 30, 60, 90, 99, -1 };
    switch (i) {
    case OPT_DISPLAY: g_cfg.fullscreen = !g_cfg.fullscreen; break;
    case OPT_VSYNC:   g_cfg.vsync = !g_cfg.vsync; break;
    case OPT_MUSIC:   g_cfg.music = (g_cfg.music + d + 11) % 11; break;
    case OPT_SFX:     g_cfg.sfx = (g_cfg.sfx + d + 11) % 11; sfx_set_volume((float)g_cfg.sfx / 10.0f); sfx_play(0, FS_SELECT); break;
    case OPT_ROUNDS:  g_cfg.rounds = g_cfg.rounds == 1 ? (d > 0 ? 3 : 5) : g_cfg.rounds == 3 ? (d > 0 ? 5 : 1) : (d > 0 ? 1 : 3); break;
    case OPT_TIME: {
        int k = 1; for (int j = 0; j < 5; j++) if (times[j] == g_cfg.round_secs) k = j;
        g_cfg.round_secs = times[(k + d + 5) % 5]; break; }
    case OPT_RINGOUT: g_cfg.ringouts = !g_cfg.ringouts; break;
    case OPT_DIFF:    g_cfg.difficulty = (g_cfg.difficulty + d + 8) % 8; g_fight.ai_level = g_cfg.difficulty; break;
    case OPT_RUMBLE:
        g_cfg.rumble = !g_cfg.rumble;
        for (int p = 0; p < 2; p++) if (g_cfg.rumble && g_pad[p]) SDL_GameControllerRumble(g_pad[p], 0x8000, 0x8000, 200);
        break;
    default: return;
    }
    cfg_apply();
}

/* returns 0 to quit */
static int front_key(SDL_Keycode k)
{
    int who, key = menu_key(k, &who);
    if (g_state == ST_TITLE) {
        if (key == K_BACK) return 0;
        if (key != K_NONE || k == SDLK_RETURN) { sfx_play(0, FS_SELECT); set_state(ST_MENU); }
        return 1;
    }
    if (g_state == ST_MENU) {
        if (key == K_BACK) return 0;
        if (key == K_UP || key == K_DOWN) { g_menu_sel = (g_menu_sel + (key == K_UP ? 3 : 1)) % 4; sfx_play(0, FS_MOVE); }
        if (key == K_OK) {
            sfx_play(0, FS_SELECT);
            if (g_menu_sel == 3) return 0;
            if (g_menu_sel == 2) { g_opt_sel = 0; set_state(ST_OPTIONS); return 1; }
            g_two_players = g_menu_sel == 1;
            g_pick[0] = g_pick[1] = 0; g_cur[0] = 0; g_cur[1] = 2;
            set_state(ST_SELECT);
        }
        return 1;
    }
    if (g_state == ST_OPTIONS) {
        if (key == K_BACK || (key == K_OK && g_opt_sel == OPT_N - 1)) { cfg_save(); sfx_play(0, FS_SELECT); set_state(ST_MENU); return 1; }
        if (key == K_UP || key == K_DOWN) { g_opt_sel = (g_opt_sel + (key == K_UP ? OPT_N - 1 : 1)) % OPT_N; sfx_play(0, FS_MOVE); }
        int d = key == K_LEFT ? -1 : (key == K_RIGHT || key == K_OK) ? 1 : 0;
        if (d) { option_change(g_opt_sel, d); sfx_play(0, FS_MOVE); }
        return 1;
    }
    if (g_state == ST_SELECT) {
        if (key == K_BACK) {
            if (g_pick[0] || g_pick[1]) { g_pick[0] = g_pick[1] = 0; sfx_play(0, FS_DENY); }
            else set_state(ST_MENU);
            return 1;
        }
        int p = g_two_players ? who : 0;
        if (p < 0 || p > 1 || g_pick[p]) return 1;
        int c = g_cur[p], col = c % GRID_COLS, rows = GRID_N / GRID_COLS;
        if (key == K_LEFT)  c = c - col + (col + GRID_COLS - 1) % GRID_COLS;
        if (key == K_RIGHT) c = c - col + (col + 1) % GRID_COLS;
        if (key == K_UP)    c = (c + GRID_N - GRID_COLS) % GRID_N;
        if (key == K_DOWN)  c = (c + GRID_COLS) % GRID_N;
        (void)rows;
        if (c != g_cur[p]) { g_cur[p] = c; sfx_play(0, FS_MOVE); }
        if (key == K_OK) {
            int id = GRID[c] ? GRID[c] : random_fighter();
            if (!fighter_exists(id)) { sfx_play(0, FS_DENY); return 1; }
            g_pick[p] = id;
            sfx_play(0, FS_SELECT);
            if (id <= 11) sfx_play(0, 12 + (id - 1) * 2);        /* announcer says the name */
            g_state_t = 0;
        }
        return 1;
    }
    if (g_state == ST_ARENA) {
        if (key == K_BACK) { g_pick[0] = g_pick[1] = 0; set_state(ST_SELECT); return 1; }
        if (key == K_LEFT || key == K_UP)    { g_arena_sel = g_arena_sel <= 1 ? 11 : g_arena_sel - 1; sfx_play(0, FS_MOVE); }
        if (key == K_RIGHT || key == K_DOWN) { g_arena_sel = g_arena_sel >= 11 ? 1 : g_arena_sel + 1; sfx_play(0, FS_MOVE); }
        if (key == K_OK) { sfx_play(0, FS_SELECT); set_state(ST_VS); }
        return 1;
    }
    if (g_state == ST_VS) {
        if (key == K_OK && g_state_t > FIGHT_HZ / 2) start_vs_fight();
        if (key == K_BACK) set_state(ST_MENU);
        return 1;
    }
    return 1;
}

static void front_tick(void)
{
    g_state_t++;
    if (g_state == ST_TITLE && g_state_t == 1) music_play(14);
    if (g_state == ST_TITLE && g_state_t >= FIGHT_HZ * 5 / 2 + FIGHT_HZ / 3) set_state(ST_MENU);
    if (g_state == ST_SELECT) {
        int done = g_pick[0] && (g_pick[1] || !g_two_players);
        if (done && g_state_t == FIGHT_HZ) {
            if (g_two_players) set_state(ST_ARENA);
            else { build_ladder(g_pick[0]); set_state(ST_VS); }
        }
    }
    if (g_state == ST_VS) {
        int a = g_pick[0], b = vs_opponent();
        if (g_state_t == FIGHT_HZ / 4 && a <= 11) sfx_play(0, 12 + (a - 1) * 2);
        if (g_state_t == FIGHT_HZ * 5 / 4) sfx_play(0, FS_VERSUS);
        if (g_state_t == FIGHT_HZ * 2 && b >= 1 && b <= 11) sfx_play(0, 12 + (b - 1) * 2 + 1);
        if (g_state_t == FIGHT_HZ * 4) start_vs_fight();
    }
}

/* ---- drawing (640x480 virtual screen on top of frontend/back1.pix) ---- */
static float blink(float speed) { return 0.55f + 0.45f * sinf((float)g_state_t * speed); }

static void text_c(const char *s, float cx, float y, float sc, float a)
{
    ui_text(s, cx - ui_text_width(s, sc) * 0.5f, y, sc, a);
}

/* side panels of back1: inner black areas of "PLAYER A" (left) and "PLAYER B" (right) */
static void draw_panel(int side, int id, int picked, const char *label)
{
    float x0 = side ? 497.f : 30.f, cx = x0 + 57.f;
    const Sprite *pt = ui_sprite(PORTRAIT[id >= 0 && id <= 12 ? id : 0]);
    ui_draw(pt, x0 + 2, 66, 0.86f, picked ? 1.f : 0.75f);
    if (id > 0 && id <= 12) text_c(g_fname[id], cx, 184, 1.0f, picked ? 1.f : 0.8f);
    if (picked) ui_frame(x0 + 1, 65, x0 + 113, 177, 2, 1, 0.85f, 0.2f, blink(0.25f));
    if (label) ui_draw_c(ui_sprite(label), cx, 445, 1, 1);
}

static void render_front(int w, int h)
{
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ui_begin(w, h);
    if (g_state == ST_TITLE) {
        ui_draw(ui_title(), 0, 0, 1, 1);           /* fxscreen.bmp: the original loading screen */
        float p = (float)g_state_t / (float)(FIGHT_HZ * 5 / 2);
        if (p > 1) p = 1;
        ui_rect(178, 203, 178 + 270 * p, 249, 0.95f, 0.45f, 0.08f, 1);
        ui_rect(178, 203, 178 + 270 * p, 210, 1, 0.75f, 0.3f, 1);
        ui_end();
        return;
    }
    ui_draw(ui_back(), 0, 0, 1, 1);
    if (g_state == ST_MENU) {
        static const char *items[4] = { "PLY1", "PLY2", "OPTIONS", "EXIT" };
        for (int i = 0; i < 4; i++) {
            float y = 140.f + (float)i * 64.f;
            int sel = i == g_menu_sel;
            ui_draw(ui_sprite(items[i]), 192, y, 1, sel ? 1.f : 0.45f);
            if (sel) ui_frame(188, y - 4, 452, y + 36, 2, 1, 0.6f, 0.1f, blink(0.2f));
        }
        draw_panel(0, 0, 0, NULL); draw_panel(1, 0, 0, NULL);
    } else if (g_state == ST_OPTIONS) {
        ui_draw_c(ui_sprite("OPTIONS"), 320, 78, 1, 1);
        for (int i = 0; i < OPT_N; i++) {
            float y = 116.f + (float)i * 32.f + (i == OPT_BACK ? 10.f : 0.f);
            int sel = i == g_opt_sel;
            float a = sel ? 1.f : 0.6f;
            if (sel) ui_rect(178, y - 5, 462, y + 17, 0.15f, 0.25f, 0.6f, 0.55f);
            if (i == OPT_BACK) text_c(OPT_LABEL[i], 320, y, 0.75f, a);
            else {
                char v[32]; option_value(i, v, sizeof v);
                ui_text(OPT_LABEL[i], 184, y, 0.75f, a);
                ui_text(v, 456 - ui_text_width(v, 0.75f), y, 0.75f, a);
            }
            if (sel) ui_frame(178, y - 5, 462, y + 17, 2, 1, 0.6f, 0.1f, blink(0.2f));
        }
        draw_panel(0, 0, 0, NULL); draw_panel(1, 0, 0, NULL);
        /* connected controllers */
        for (int p = 0; p < 2; p++) {
            float cx = p ? 554.f : 87.f;
            ui_draw_c(ui_sprite(p ? "2P" : "1P"), cx, 420, 1, 1);
            if (g_pad[p]) text_c("PAD", cx, 440, 0.75f, 1); else text_c("KEYS", cx, 440, 0.75f, 0.7f);
        }
    } else if (g_state == ST_SELECT) {
        ui_draw_c(ui_sprite("CHOOSE"), 320, 70, 0.56f, 1);
        for (int i = 0; i < GRID_N; i++) {
            float x = 194.f + (float)(i % GRID_COLS) * 86.f, y = 90.f + (float)(i / GRID_COLS) * 86.f;
            int id = GRID[i];
            ui_draw(ui_sprite(PORTRAIT[id ? id : 12]), x, y, 0.625f, 1);
            for (int p = 0; p < 1 + g_two_players; p++) {
                if (g_cur[p] != i) continue;
                float in = (p == 1 && g_cur[0] == i) ? 4.f : 0.f;
                float a = g_pick[p] ? 1.f : blink(0.3f);
                if (p == 0) ui_frame(x - 3 + in, y - 3 + in, x + 83 - in, y + 83 - in, 3, 1, 0.55f, 0.1f, a);
                else        ui_frame(x - 3 + in, y - 3 + in, x + 83 - in, y + 83 - in, 3, 0.2f, 0.8f, 1, a);
            }
        }
        int h0 = g_pick[0] ? g_pick[0] : GRID[g_cur[0]];
        draw_panel(0, h0 ? h0 : 12, g_pick[0] != 0, "1P");
        if (g_two_players) { int h1 = g_pick[1] ? g_pick[1] : GRID[g_cur[1]]; draw_panel(1, h1 ? h1 : 12, g_pick[1] != 0, "2P"); }
        else draw_panel(1, 12, 0, "CPU");
    } else if (g_state == ST_ARENA) {
        char nm[16]; snprintf(nm, sizeof nm, "ARENA%d", g_arena_sel);
        ui_draw_c(ui_sprite("ARENA"), 320, 78, 1, 1);
        ui_draw(ui_sprite(nm), 192, 110, 1, 1);
        ui_frame(190, 108, 450, 368, 2, 1, 0.55f, 0.1f, 1);
        ui_draw_c(ui_sprite("CURSORL"), 196, 400, 1, blink(0.25f));
        ui_draw_c(ui_sprite("CURSORR"), 444, 400, 1, blink(0.25f));
        text_c(g_aname[g_arena_sel], 320, 392, 1, 1);
        draw_panel(0, g_pick[0], 1, "1P"); draw_panel(1, g_pick[1], 1, "2P");
    } else if (g_state == ST_VS) {
        int a = g_pick[0], b = vs_opponent();
        float t = (float)g_state_t / (float)FIGHT_HZ, s = t < 0.4f ? t / 0.4f : 1.f;
        ui_draw(ui_sprite(PORTRAIT[a]), 180 - (1 - s) * 200, 92, 1, 1);
        ui_draw(ui_sprite(PORTRAIT[b]), 332 + (1 - s) * 200, 292, 1, 1);
        text_c(g_fname[a], 244, 226, 1, 1);
        text_c(g_fname[b], 396, 426, 1, 1);
        if (t > 1.1f) ui_draw_c(ui_sprite("VS"), 320, 262, 1, 1);
        draw_panel(0, a, 1, "1P"); draw_panel(1, b, 1, g_two_players ? "2P" : "CPU");
        if (!g_two_players) {            /* ladder progress */
            char s2[32]; snprintf(s2, sizeof s2, "%d", g_ladder_pos + 1);
            text_c(s2, 87, 404, 1, 1);
        }
    }
    ui_end();
}

static int screenshot_mode(const char *path, SDL_Window *win)
{
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    if (!g_game) update_camera(0, 1);
    render(w, h);
    uint8_t *px = malloc((size_t)w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    free(px);
    return 0;
}

int main(int argc, char **argv)
{
    if (!try_open_pac(argc, argv)) {
        fprintf(stderr, "FIGHT.PAC not found. Usage: fxfighter [path/to/FIGHT.PAC]\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FX Fighter", "FIGHT.PAC not found.\nPut fxfighter next to FIGHT.PAC or pass its path.", NULL);
        return 1;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0 && SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window *win = SDL_CreateWindow("FX Fighter Turbo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1280, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        win = SDL_CreateWindow("FX Fighter Turbo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1280, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    }
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    g_win = win;
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "GL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    size_t sz;
    const uint8_t *md = pac_get(&g_pac, "fight/boutgfx/material.mat", &sz);
    if (md) g_nmats = material_load_all(md, sz, g_mats, MAX_MATS);
    ui_init(&g_pac);
    load_names();

    float cli_pitch = -99, cli_dist = -1;
    int arena = -1, p1 = -1, p2 = -1, ticks = 0, two_players = 0, demo = 0, screen = -1;
    const char *shot = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--arena") && i + 1 < argc) { arena = atoi(argv[++i]); g_debug_fight = 1; }
        else if (!strcmp(argv[i], "--p1") && i + 1 < argc) { p1 = atoi(argv[++i]); g_debug_fight = 1; }
        else if (!strcmp(argv[i], "--p2") && i + 1 < argc) { p2 = atoi(argv[++i]); g_debug_fight = 1; }
        else if (!strcmp(argv[i], "--clip") && i + 1 < argc) g_f[0].clip = g_f[1].clip = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--time") && i + 1 < argc) g_f[0].time = g_f[1].time = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) g_yaw = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) cli_pitch = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--dist") && i + 1 < argc) cli_dist = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--viewer")) { g_game = 0; g_debug_fight = 1; }
        else if (!strcmp(argv[i], "--2p")) { two_players = 1; g_debug_fight = 1; }
        else if (!strcmp(argv[i], "--demo")) { demo = 1; g_debug_fight = 1; }
        else if (!strcmp(argv[i], "--fight")) g_debug_fight = 1;
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) {
            static const char *names[] = { "title", "menu", "select", "arena", "vs", "options" };
            for (int k = 0; k < 6; k++) if (!strcmp(argv[i + 1], names[k])) screen = k;
            i++;
        }
    }
    md = pac_get(&g_pac, "fight/fighters/shadow0.dat", &sz);
    if (md) model_load_all(md, sz, &g_shadow, 1);
    md = pac_get(&g_pac, "fight/fighters/f0.ani", &sz);
    if (!md || !ani_load(md, sz, &g_generic)) fprintf(stderr, "f0.ani missing\n");
    if (!shot) {
        char *base = SDL_GetBasePath();
        char d[6][1100];
        snprintf(d[0], sizeof d[0], "%smusic/", base ? base : "");
        snprintf(d[1], sizeof d[1], "%s../music/", base ? base : "");
        snprintf(d[2], sizeof d[2], "%sport/music/", g_pac_dir);
        snprintf(d[3], sizeof d[3], "%smusic/", g_pac_dir);
        snprintf(d[4], sizeof d[4], "music/");
        snprintf(d[5], sizeof d[5], "../music/");
        const char *dirs[6] = { d[0], d[1], d[2], d[3], d[4], d[5] };
        audio_init(&g_pac, dirs, 6);
        if (base) SDL_free(base);
        cfg_load();
        cfg_apply();
        for (int i = 0; i < SDL_NumJoysticks(); i++) pad_added(i);
    }
    srand(shot ? 1234u : (unsigned)SDL_GetTicks() ^ 0x5eed);

    if (g_debug_fight) {
        if (p1 < 0) p1 = next_fighter(0, 1);
        if (p2 < 0) p2 = next_fighter(p1, 1);
        /* like the original: fight in the opponent's home arena (fighter n lives in arena n) */
        if (arena < 0) arena = (p2 >= 1 && p2 <= 11) ? p2 : 1;
        int keep_game = g_game;
        float yaw = g_yaw;
        begin_match(p1, p2, arena, demo, !two_players);
        if (!keep_game) { g_game = 0; g_yaw = yaw; g_pitch = 0.38f; g_dist = 5.0f; }
        if (cli_pitch > -90) g_pitch = cli_pitch;
        if (cli_dist > 0) g_dist = cli_dist;
        if (getenv("FX_KEEP_YAW")) g_yaw = yaw;
        g_two_players = two_players;
    } else set_state(ST_TITLE);

    if (shot) {
        if (screen >= 0) {                       /* front end screenshots */
            g_state = screen; g_two_players = 1;
            g_pick[0] = 4; g_pick[1] = screen == 2 ? 0 : 6; g_cur[0] = 3; g_cur[1] = 7; g_menu_sel = 0;
            g_arena_sel = 3; g_state_t = ticks ? ticks : 40;
        } else if (ticks < 0) {                  /* --ticks -N: run until a ring-out, then N more ticks */
            int t = 0, after = -1;
            while (t++ < 48 * 60 * 20 && after != 0) {
                fight_tick(&g_fight); match_tick(); camera_step(0);
                g_fight.nsnd = 0;
                if (after > 0) after--;
                if (after < 0 && g_fight.winner >= 0 && g_fight.ko_reason == 1) { after = -ticks; printf("ring out at tick %d\n", t); }
            }
        }
        if (screen < 0) for (int t = 0; t < ticks; t++) { fight_tick(&g_fight); match_tick(); camera_step(0); g_fight.nsnd = 0; }
        return screenshot_mode(shot, win);
    }
    update_title(win);

    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;
    int running = 1, dragging = 0, title_cnt = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_CONTROLLERDEVICEADDED) pad_added(e.cdevice.which);
            else if (e.type == SDL_CONTROLLERDEVICEREMOVED) pad_removed(e.cdevice.which);
            else if (e.type == SDL_CONTROLLERBUTTONDOWN) { int s = pad_slot(e.cbutton.which); if (s >= 0) pad_button(s, e.cbutton.button); }
            else if (e.type == SDL_CONTROLLERAXISMOTION) { int s = pad_slot(e.caxis.which); if (s >= 0) pad_axis(s, e.caxis.axis, e.caxis.value); }
            else if (e.type == SDL_MOUSEBUTTONDOWN) dragging = 1;
            else if (e.type == SDL_MOUSEBUTTONUP) dragging = 0;
            else if (e.type == SDL_MOUSEMOTION && dragging && g_state == ST_FIGHT) {
                if (g_game) g_yaw_ofs += e.motion.xrel * 0.01f; else g_yaw += e.motion.xrel * 0.01f;
                g_pitch += e.motion.yrel * 0.005f;
                if (g_pitch < -0.3f) g_pitch = -0.3f;
                if (g_pitch > 1.2f) g_pitch = 1.2f;
            } else if (e.type == SDL_MOUSEWHEEL && g_state == ST_FIGHT) {
                g_dist *= e.wheel.y > 0 ? 0.9f : 1.1f;
                if (g_dist < 0.8f) g_dist = 0.8f;
                if (g_dist > 12) g_dist = 12;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                Fighter *a = &g_f[0], *b = &g_f[1];
                SDL_Keycode k = e.key.keysym.sym;
                /* keys valid everywhere */
                if (k == SDLK_m) music_toggle();
                else if (k == SDLK_x && g_state != ST_FIGHT) sfx_toggle();
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) { if (g_cfg.music > 0) g_cfg.music--; cfg_apply(); cfg_save(); }
                else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) { if (g_cfg.music < 10) g_cfg.music++; cfg_apply(); cfg_save(); }
                else if (k == SDLK_F11 || (k == SDLK_RETURN && (SDL_GetModState() & KMOD_ALT))) {
                    g_cfg.fullscreen = !g_cfg.fullscreen; cfg_apply(); cfg_save();
                }
                else if (g_state != ST_FIGHT) { if (!front_key(k)) running = 0; }
                else if (k == SDLK_ESCAPE) { g_game = 1; g_debug_fight = 0; g_fight_paused = 0; set_state(ST_MENU); }
                else if (k == SDLK_p || k == SDLK_PAUSE) g_fight_paused = !g_fight_paused;
                else if (k == SDLK_x) sfx_toggle();
                else if (k == SDLK_F1) { g_game = !g_game; if (g_game) { g_yaw = -1.5708f; g_pitch = 0.28f; g_dist = 3.6f; } }
                else if (k == SDLK_n) {
                    arena_load(&g_arena, g_arena.id == 0 ? 1 : (g_arena.id == 11 ? 0 : g_arena.id + 1));
                    fighter_load(a, a->id, g_arena.pal);
                    fighter_load(b, b->id, g_arena.pal);
                    restart_fight();
                }
                else if (k == SDLK_b) g_show_bg = !g_show_bg;
                else if (g_game) {
                    int fine = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    int arrow = k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN;
                    if (k == SDLK_HOME || k == SDLK_INSERT || k == SDLK_DELETE || k == SDLK_PAGEUP || k == SDLK_PAGEDOWN ||
                        (arrow && g_fixed_view && g_fight.p[1].cpu))   /* arrows are player 2's keys otherwise */
                        fixed_view_key(k, fine);
                    switch (k) {
                    case SDLK_F3: g_fight.p[1].cpu = !g_fight.p[1].cpu; break;
                    case SDLK_F4: g_fight.p[0].cpu = !g_fight.p[0].cpu; break;
                    case SDLK_F5: g_round = 1; g_fight.p[0].wins = g_fight.p[1].wins = 0; g_match_over_t = 0; restart_fight(); break;
                    case SDLK_r: g_ringouts = !g_ringouts; g_fight.ringouts = g_ringouts; break;
                    case SDLK_1: fighter_load(a, next_fighter(a->id, -1), g_arena.pal); fighter_load(b, b->id, g_arena.pal); restart_fight(); break;
                    case SDLK_2: fighter_load(a, next_fighter(a->id, 1), g_arena.pal); fighter_load(b, b->id, g_arena.pal); restart_fight(); break;
                    case SDLK_9: fighter_load(b, next_fighter(b->id, -1), g_arena.pal); restart_fight(); break;
                    case SDLK_0: fighter_load(b, next_fighter(b->id, 1), g_arena.pal); restart_fight(); break;
                    default: break;
                    }
                } else {
                    switch (k) {
                    case SDLK_RIGHT: a->clip = (a->clip + 1) % a->anim.nclips; a->time = 0;
                                     b->clip = b->anim.nclips ? a->clip % b->anim.nclips : 0; b->time = 0; break;
                    case SDLK_LEFT:  a->clip = (a->clip + a->anim.nclips - 1) % a->anim.nclips; a->time = 0;
                                     b->clip = b->anim.nclips ? a->clip % b->anim.nclips : 0; b->time = 0; break;
                    case SDLK_UP:    fighter_load(a, next_fighter(a->id, 1), g_arena.pal); fighter_load(b, b->id, g_arena.pal); restart_fight(); break;
                    case SDLK_DOWN:  fighter_load(a, next_fighter(a->id, -1), g_arena.pal); fighter_load(b, b->id, g_arena.pal); restart_fight(); break;
                    case SDLK_PAGEUP:   fighter_load(b, next_fighter(b->id, 1), g_arena.pal); restart_fight(); break;
                    case SDLK_PAGEDOWN: fighter_load(b, next_fighter(b->id, -1), g_arena.pal); restart_fight(); break;
                    case SDLK_SPACE: g_paused = !g_paused; break;
                    case SDLK_PERIOD: a->time += 1.0f / FIGHT_HZ; b->time += 1.0f / FIGHT_HZ; break;
                    case SDLK_COMMA:  a->time -= 1.0f / FIGHT_HZ; b->time -= 1.0f / FIGHT_HZ;
                                      if (a->time < 0) a->time = 0;
                                      if (b->time < 0) b->time = 0;
                                      break;
                    default: break;
                    }
                }
                update_title(win);
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (getenv("FX_SPEED")) dt *= atof(getenv("FX_SPEED"));   /* testing: fast forward */
        acc += dt;
        if (g_state != ST_FIGHT) {
            while (acc >= 1.0 / FIGHT_HZ) { front_tick(); acc -= 1.0 / FIGHT_HZ; }
            if (++title_cnt % 30 == 0) SDL_SetWindowTitle(win, "FX Fighter Turbo (open port)");
        } else if (g_game) {
            if (g_fight_paused) acc = 0;
            while (acc >= 1.0 / FIGHT_HZ && g_state == ST_FIGHT) {
                read_input();
                fight_tick(&g_fight);
                pad_rumble_hits();
                for (int s = 0; s < g_fight.nsnd; s++) sfx_play(g_fight.snd[s].bank, g_fight.snd[s].index);
                g_fight.nsnd = 0;
                match_tick();
                camera_step(0);
                acc -= 1.0 / FIGHT_HZ;
            }
            if (++title_cnt % 15 == 0) update_title(win);
            update_camera((float)dt, 0);
        } else { acc = 0; if (!g_paused) { g_f[0].time += (float)dt; g_f[1].time += (float)dt; } update_camera((float)dt, 0); }
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
        render(w, h);
        SDL_GL_SwapWindow(win);
        if (!g_cfg.vsync) SDL_Delay(1);
    }
    for (int i = 0; i < 2; i++) if (g_pad[i]) SDL_GameControllerClose(g_pad[i]);
    audio_shutdown();
    fighter_free(&g_f[0]); fighter_free(&g_f[1]); arena_free(&g_arena);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    pac_close(&g_pac);
    return 0;
}
