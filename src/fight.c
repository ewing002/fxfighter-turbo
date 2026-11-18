/*
 * FX Fighter Turbo - open reimplementation
 * Fight engine reconstructed from FIGHT.EXE (see fight.h for addresses).
 */
#include "fight.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define MOVE_FALLEN   116      /* 0x4d73e0 - 0x4d6390 / 36 */
#define MOVE_DEAD     0x61     /* forced when health < 1 */
#define MOVE_STAND    0
#define MOVE_VICTORY  1
#define MOVE_DEFEAT   2
#define MOVE_RO_STEP  6
#define MOVE_RO_BACK  7
#define MOVE_RO_HEAD  9
#define MOVE_RO_FEET  10
#define MOVE_DAZE     0x68     /* FLYING FALL 2 DAZE */
#define CUR(pl)       ((pl)->cur ? (pl)->cur : (pl)->ms)
#define TICK_T        (1.0f / 32.0f)   /* movement time step: (ticks/frame) / 32 */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static float rdf32le(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

/* ================================================================== */
/*  Data loading                                                       */
/* ================================================================== */
int moveset_load(MoveSet *ms, const uint8_t *h, size_t hsz, const uint8_t *s, size_t ssz)
{
    memset(ms, 0, sizeof *ms);
    if (hsz < 38 || ssz < 12) return 0;
    ms->nmoves = (int)((hsz - 2) / 36);
    ms->moves = calloc((size_t)ms->nmoves, sizeof(MoveDef));
    for (int i = 0; i < ms->nmoves; i++) {
        const uint8_t *r = h + 2 + i * 36;
        MoveDef *m = &ms->moves[i];
        memcpy(m->name, r, 20); m->name[20] = 0;
        m->limbs = r[20]; m->stance = r[21]; m->cls = r[22]; m->clip = r[23];
        m->sound = r[24]; m->flags = r[25]; m->cam = r[0x21]; m->state = rd16(r + 34);
        memcpy(m->ai, r + 0x19, 8);
    }
    ms->nst = (int)(ssz / 12);
    ms->st = calloc((size_t)ms->nst + 1, sizeof(StateRec));   /* +1: zeroed terminator guard */
    for (int i = 0; i < ms->nst; i++) {
        const uint8_t *r = s + i * 12;
        StateRec *t = &ms->st[i];
        t->op = r[0]; t->fr = r[1]; t->val = rd16(r + 2); t->mask = rd16(r + 4);
        t->cond = r[6]; t->minfr = r[7]; t->ctx = r[8]; t->pad = r[9]; t->tgt = (int16_t)rd16(r + 10);
    }
    return 1;
}

void moveset_free(MoveSet *ms) { free(ms->moves); free(ms->st); memset(ms, 0, sizeof *ms); }

int fighter_info_load(FighterInfo *fi, const uint8_t *src, size_t n)
{
    memset(fi, 0, sizeof *fi);
    size_t usz;
    uint8_t *d = cmp_decode(src, n, &usz);
    if (!d || usz < 0x98) { free(d); return 0; }
    for (size_t i = 0; i < usz; i++) d[i] = (uint8_t)(d[i] - 1);
    memcpy(fi->name, d, 15); memcpy(fi->arena, d + 16, 15);
    int32_t fd; memcpy(&fd, d + 0x4c, 4);
    fi->fall_depth = (float)fd / 65536.0f;
    fi->ring_half = rdf32le(d + 0x54);
    fi->stun_k = rdf32le(d + 0x58);
    fi->stun_decay = rdf32le(d + 0x5c);
    for (int k = 0; k < 3; k++) { int32_t v; memcpy(&v, d + 0x60 + 4 * k, 4); fi->mash[k] = v; }
    fi->jump_v = rdf32le(d + 0x6c); fi->jump_g = rdf32le(d + 0x70); fi->jump_s = rdf32le(d + 0x74);
    for (int k = 0; k < 6; k++) fi->body_r[k] = rdf32le(d + 0x78 + 4 * k);
    for (int k = 0; k < 16; k++) fi->part_r[k] = (float)d[0x88 + k] / 256.0f;
    if (usz >= 0x144)
        for (int j = 0; j < 18; j++) {
            fi->fa_type[j] = d[0xa0 + j]; fi->fa_speed[j] = rdf32le(d + 0xb4 + 4 * j); fi->fa_limit[j] = rdf32le(d + 0xfc + 4 * j);
        }
    free(d);
    return 1;
}

/* ================================================================== */
/*  Small helpers                                                      */
/* ================================================================== */
static int is_term(const StateRec *r) { return r->mask == 0 && r->ctx == 0; }

static const StateRec *rec(const Player *p, int k)
{
    const MoveSet *ms = CUR(p);
    if (k < 0 || k >= ms->nst) return &ms->st[ms->nst];   /* zero record = terminator */
    return &ms->st[k];
}

static const AniClip *cur_clip(const Player *p)
{
    if (!p->aset || p->clip < 0 || p->clip >= p->aset->nclips) return NULL;
    return &p->aset->clips[p->clip];
}

const AniFrame *player_frame(const Player *p)
{
    const AniClip *c = cur_clip(p);
    if (!c || !c->nframes) return NULL;
    int f = p->frame;
    if (f < 0) f = 0;
    if (f >= c->nframes) f = c->nframes - 1;
    return &c->frames[f];
}

const char *player_move_name(const Player *p)
{
    const MoveSet *ms = CUR(p);
    return (ms && p->move >= 0 && p->move < ms->nmoves) ? ms->moves[p->move].name : "?";
}

int player_move_cam(const Player *p)
{
    const MoveSet *ms = CUR(p);
    return (ms && p->move >= 0 && p->move < ms->nmoves) ? ms->moves[p->move].cam : 0;
}

static const MoveDef *cur_move(const Player *p) { return &CUR(p)->moves[p->move]; }

/* ================================================================== */
/*  Sound events (FUN_00435eb4)                                        */
/* ================================================================== */
static void emit_sound(Fight *f, int bank, int index)
{
    if (index <= 0 || f->nsnd >= 32) return;
    f->snd[f->nsnd].bank = (uint8_t)bank;
    f->snd[f->nsnd].index = (uint8_t)index;
    f->nsnd++;
}

/* fighter sound: < 0x80 the player's own bank (1-based), 0x80.. the arena bank (0-based), >= 0xf0 none */
/* 0x44c666: a sprite effect request; the queue holds 4 per frame */
static void emit_fx(Fight *f, int player, int group, int id)
{
    if (f->nfx >= 4 || id < 0) return;
    f->fx[f->nfx].player = (int8_t)(player & 1);
    f->fx[f->nfx].group = (int8_t)group;
    f->fx[f->nfx].id = (uint8_t)id;
    f->nfx++;
}

static void fighter_sound(Fight *f, int i, int s)
{
    if (s <= 0 || s >= 0xf0) return;
    if (s >= 0x80) emit_sound(f, 3, s - 0x80 + 1);
    else emit_sound(f, i + 1, s);
}

/* ================================================================== */
/*  Geometry: the fight line (a8684) and world placement               */
/* ================================================================== */
void fight_line_dir(const Fight *f, float *ux, float *uy) { *ux = cosf(f->ang); *uy = sinf(f->ang); }

static void floor_pos(const Fight *f, int i, float *x, float *y)
{
    float ux, uy; fight_line_dir(f, &ux, &uy);
    *x = f->ox + ux * f->p[i].s;
    *y = f->oy + uy * f->p[i].s;
}

/* yaw that turns the fighter-local -Y axis into the look direction face*u */
static float look_yaw(const Fight *f, int i)
{
    float ux, uy; fight_line_dir(f, &ux, &uy);
    float lx = ux * (float)f->p[i].face, ly = uy * (float)f->p[i].face;
    return atan2f(lx, -ly);
}

static float root_x(const Player *p) { const AniFrame *fr = player_frame(p); return fr ? (float)fr->root[0] * ANI_UNIT : 0; }
static float root_h(const Player *p) { const AniFrame *fr = player_frame(p); return fr ? (float)fr->root[1] * ANI_UNIT : 0; }
static int   root_fwd(const Player *p) { const AniFrame *fr = player_frame(p); return fr ? fr->root[2] : 0; }

void fight_place(const Fight *f, int i, float *wx, float *wy, float *wz, float *yaw_deg)
{
    floor_pos(f, i, wx, wy);
    *wz = f->p[i].z;
    *yaw_deg = look_yaw(f, i) * 57.29578f;
}

void fight_to_world(const Fight *f, int i, const float q[3], float out[3])
{
    const Player *p = &f->p[i];
    float x, y; floor_pos(f, i, &x, &y);
    float a = look_yaw(f, i), c = cosf(a), s = sinf(a);
    float lx = q[0] + root_x(p), ly = q[1];
    out[0] = x + lx * c - ly * s;
    out[1] = y + lx * s + ly * c;
    out[2] = q[2] + root_h(p) + p->z;
}

void fight_joint(const Fight *f, int i, int part, float out[3])
{
    const AniFrame *fr = player_frame(&f->p[i]);
    float m[4][3] = { { 0 } };
    if (fr) ani_bone_matrix(fr, part, m);
    fight_to_world(f, i, m[3], out);
}

void fight_bone(const Fight *f, int i, int part, float m[4][3])
{
    const AniFrame *fr = player_frame(&f->p[i]);
    float b[4][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 0 } };
    if (fr) ani_bone_matrix(fr, part, b);
    float a = look_yaw(f, i), c = cosf(a), s = sinf(a);
    for (int k = 0; k < 3; k++) {
        float x = b[k][0], y = b[k][1];
        m[k][0] = x * c - y * s; m[k][1] = x * s + y * c; m[k][2] = b[k][2];
    }
    fight_to_world(f, i, b[3], m[3]);
}

static float gap(const Fight *f, int i)          /* signed distance to the opponent along the line */
{
    return f->p[i ^ 1].s - f->p[i].s;
}

/* rotate the fight line by `a` radians around fighter k (0x44998a) */
static void rotate_line_around(Fight *f, int k, float a)
{
    float kx, ky; floor_pos(f, k, &kx, &ky);
    f->ang += a;
    float ux, uy; fight_line_dir(f, &ux, &uy);
    f->ox = kx - ux * f->p[k].s;
    f->oy = ky - uy * f->p[k].s;
}

/* Sidestep (0x43e424): at a move change the lateral root offset of the previous
 * move is turned into a rotation of the fight line around the opponent. */
static void bake_lateral(Fight *f, int i)
{
    Player *p = &f->p[i];
    if (!p->aset) return;
    float lat = root_x(p);
    float d = fabsf(gap(f, i));
    if (d < 0.05f || fabsf(atan2f(lat, d)) <= 0.02f) return;
    float c[3] = { 0, 0, 0 }, w[3];
    fight_to_world(f, i, c, w);                       /* includes the lateral root offset */
    float ox, oy; floor_pos(f, i ^ 1, &ox, &oy);
    float dx = w[0] - ox, dy = w[1] - oy, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float sg = (p->s - f->p[i ^ 1].s) >= 0 ? 1.f : -1.f;
    float ux = sg * dx / len, uy = sg * dy / len;
    f->ang = atan2f(uy, ux);
    p->s = f->p[i ^ 1].s + sg * len;
    f->ox = ox - ux * f->p[i ^ 1].s;
    f->oy = oy - uy * f->p[i ^ 1].s;
}

/* ================================================================== */
/*  StartMove (0x43e828)                                               */
/* ================================================================== */
static void set_mode_from_op(Fight *f, int i, unsigned op, int at_start)
{
    Player *p = &f->p[i];
    float d = fabsf(gap(f, i));
    switch (op) {
    case 0xd0: if (p->mode < 1 || p->mode > 3) p->jt = 0; p->mode = 1; break;
    case 0xd1: if (p->mode < 1 || p->mode > 3) p->jt = 0; if (p->mode != 2) p->vel = 2.f; p->mode = 2; break;
    case 0xd2: if (p->mode < 1 || p->mode > 3) p->jt = 0; if (p->mode != 3) p->vel = -2.f; p->mode = 3; break;
    case 0xd5: if (p->mode != 4) p->vel = d; p->mode = 4; break;
    case 0xd6: if (p->mode != 5) p->vel = d + 1.f; p->mode = 5; break;
    case 0xd7: if (p->mode != 6) p->vel = d - 1.f; p->mode = 6; break;
    default:
        if (at_start && op != 0xca) { p->mode = 0; p->vel = 0; }   /* height: ground() settles it */
        break;
    }
    if (p->vel > 2.f) p->vel = 2.f;
    if (p->vel < -2.f) p->vel = -2.f;
}

/* StartMove(player, move). `owner` selects whose move table / animations are used:
 * FIGHT.EXE encodes this as player+2 (throw victim plays the thrower's table) and
 * player^3 (terminator 0xcb: continue with the other player's table). */
static void start_move_t(Fight *f, int i, int m, int owner)
{
    Player *p = &f->p[i];
    const Player *o = &f->p[owner & 1];
    const MoveSet *ms = o->ms;
    if (m < 0 || m >= ms->nmoves) return;
    if (p->health < 1 && p->move >= 0 && owner == i && p->move < CUR(p)->nmoves &&
        (p->move == MOVE_FALLEN || cur_move(p)->cls == 3))
        m = MOVE_DEAD;
    bake_lateral(f, i);
    if (p->turn_flag) { p->face = -p->face; p->turn_flag = 0; }
    p->cur = ms;
    p->owner = owner & 1;
    p->move = m;
    p->sp = ms->moves[m].state;
    p->cont_hit = 0;
    p->solid = 1;
    if (p->have_prev_box) { p->have_prev_box = 0; memset(&p->prev_raw, 0, sizeof p->prev_raw); }
    p->ev_lock = 0;                                /* cb540 */
    p->edge_guard = 1;                             /* cb440 */
    uint8_t clip = ms->moves[m].clip;
    if (clip != 0xff) {
        if (clip >= 200) { p->aset = o->own; p->clip = clip - 200; }
        else             { p->aset = p->generic; p->clip = clip; }
        p->frame = rec(p, p->sp)->minfr;
    }
    p->last_fwd = root_fwd(p);
    set_mode_from_op(f, i, rec(p, p->sp)->op, 1);
    p->in_react = m < 128 && REACT_MOVE[m];
    unsigned op0 = rec(p, p->sp)->op;
    if (op0 < 0xcc || op0 > 0xce) p->mash = 0;     /* break-free count survives only into another 0xcc..0xce move */
    fighter_sound(f, i, ms->moves[m].sound);
}

static void start_move(Fight *f, int i, int m) { start_move_t(f, i, m, i); }

/* ================================================================== */
/*  Context bits (0x441b7e / 0x440cbb)                                 */
/* ================================================================== */
static int facing_away(const Fight *f, int i)
{
    return gap(f, i) * (float)f->p[i].face < 0;
}

static unsigned ctx_bits(const Fight *f, int i)
{
    float dist = fabsf(gap(f, i));
    unsigned c = dist < f->p[0].radius + f->p[1].radius + 0.29f ? 1u : 0u;
    if (facing_away(f, i)) c |= 2;
    if (facing_away(f, i ^ 1)) c |= 4;
    return c;
}

static int opp_stance(const Fight *f, int i)
{
    return cur_move(&f->p[i ^ 1])->stance & 0x1f;
}

/* ================================================================== */
/*  Hit test (0x4491f7)                                                */
/*  Body parts are cubes around their joint (FUN_004488ab): centre =   */
/*  translation of the part's world matrix, half size fi[0x88+part]/256 */
/* ================================================================== */
static void part_box(const Fight *f, int i, int part, HitBox *b)
{
    fight_joint(f, i, part, b->c);
    float h = f->p[i].fi ? f->p[i].fi->part_r[part & 15] : 0.14f;
    for (int k = 0; k < 3; k++) { b->mn[k] = b->c[k] - h; b->mx[k] = b->c[k] + h; }
}

/* FUN_00448a69, as the original does it: union in x and y, but in z both the minimum AND the
 * maximum take the lower value (so a merged box only reaches up to its lowest part's top) */
static void box_merge(HitBox *a, const HitBox *b)
{
    for (int k = 0; k < 2; k++) {
        if (b->mn[k] < a->mn[k]) a->mn[k] = b->mn[k];
        if (b->mx[k] > a->mx[k]) a->mx[k] = b->mx[k];
    }
    if (b->mn[2] < a->mn[2]) a->mn[2] = b->mn[2];
    if (b->mx[2] < a->mx[2]) a->mx[2] = b->mx[2];
    for (int k = 0; k < 3; k++) a->c[k] = (a->c[k] + b->c[k]) * 0.5f;
}

static int boxes_overlap(const HitBox *a, const HitBox *b)       /* FUN_00448b84 */
{
    for (int k = 0; k < 3; k++) if (a->mx[k] <= b->mn[k] || b->mx[k] <= a->mn[k]) return 0;
    return 1;
}

static int hit_test(Fight *f, int i, int damage, int dir)
{
    Player *a = &f->p[i];
    if (f->hit_pending[i] || f->p[i ^ 1].ringout) return 0;
    const MoveDef *md = cur_move(a);
    int set = md->limbs < 24 ? md->limbs : 0;
    HitBox box, b;
    memset(&box, 0, sizeof box);
    for (int k = 1; k <= LIMB_SETS[set][0] && k < 17; k++) {
        part_box(f, i, LIMB_SETS[set][k], &b);
        if (k == 1) box = b; else box_merge(&box, &b);
    }
    /* swept test: merged with the unmerged box of the previous check of this move (a8268 / aa4c0).
     * The store is only written from the second check on and StartMove zeroes it, so the second
     * check of a move is merged with a zero box - kept as the original does it. */
    if (a->have_prev_box) { HitBox prev = a->prev_raw; a->prev_raw = box; box_merge(&box, &prev); }
    a->have_prev_box = 1;
    for (int k = 0; k < 9; k++) {
        part_box(f, i ^ 1, VICTIM_PARTS[k], &b);
        if (boxes_overlap(&box, &b)) {
            f->hit_pending[i] = 1;
            f->hit_damage[i] = damage;
            f->hit_dir[i] = dir & 3;
            /* zone from the CENTRE height of the victim's part, around 0x9999 (0.6):
             * below 0.4 low, below 1.0 mid, else high */
            f->hit_zone[i] = b.c[2] < 0.4f ? 2 : (b.c[2] < 1.0f ? 1 : 0);
            f->hit_part[i] = VICTIM_PARTS[k];
            if (getenv("FX_TRACE"))
                fprintf(stderr, "HIT P%d %s -> part %d zone %d dmg %d\n", i + 1, md->name, VICTIM_PARTS[k], f->hit_zone[i], damage);
            return 1;
        }
    }
    return 0;
}

/* damage factors (0x446690, set before every round): the attacker's "attack" times the victim's
 * "defence" (0x4c73c0 / 0x4c72d0). A CPU fighter gets both from the difficulty (0x4a5738 /
 * 0x4a5758), two humans the handicap as attack (0x4a4030); a human against the CPU 1.0 / 1.0 */
static const float CPU_ATK[8] = { 1.0f, 1.11f, 1.12f, 1.12f, 1.15f, 1.15f, 1.2f, 1.2f };
static const float CPU_DEF[8] = { 1.0f, 1.0f, 0.95f, 0.9f, 0.9f, 0.9f, 0.85f, 0.8f };
static const float HC_ATK[8]  = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f };

static int ai_level(const Fight *f);

static float atk_mul(const Fight *f, int i)
{
    if (f->p[i].cpu) return CPU_ATK[ai_level(f)];
    if (f->p[i ^ 1].cpu) return 1.0f;
    int h = f->handicap[i];
    return HC_ATK[h < 0 ? 0 : h > 7 ? 7 : h];
}

static float def_mul(const Fight *f, int i) { return f->p[i].cpu ? CPU_DEF[ai_level(f)] : 1.0f; }

/* op * fi[0x40] * fi[0x44] (all 1.0) * attack(attacker) * defence(victim), truncated (0x441865) */
static int damage_of(const Fight *f, int i, int op)
{
    return (int)((float)op * atk_mul(f, i) * def_mul(f, i ^ 1));
}

/* ================================================================== */
/*  Event processing (0x441c0b)                                        */
/* ================================================================== */
static void process_events(Fight *f, int i)
{
    Player *p = &f->p[i];
    for (int guard = 0; guard < 64; guard++) {
        const StateRec *r = rec(p, p->sp);
        if (is_term(r) || r->fr > p->frame) break;
        int move_before = p->move;
        const MoveSet *ms_before = CUR(p);
        if (r->cond == 8 && r->op != 0xe9) {
            /* throw: opponent plays the next record's target from OUR table, we play this one */
            const StateRec *n = rec(p, p->sp + 1);
            Player *o = &f->p[i ^ 1];
            if ((cur_move(o)->stance & 0x3f) != 8 && !o->ringout) {
                start_move_t(f, i ^ 1, n->tgt, i);
                start_move(f, i, r->tgt);
                o->in_react = 1;                  /* cb4c8 = -1: hits during the throw are combo hits */
                p->solid = 0;                     /* a8260 = 0: the victim is not pushed away from the thrower
                                                   * (otherwise a lifted fighter ends up in front, not overhead) */
                f->thrower = i + 1;               /* a8344 */
                return;
            }
        } else {
            unsigned op = r->op;
            /* cb540: after a hit check the events of the move are skipped until a record with
             * op 0 or >= 0xf1 */
            if (op == 0 || op >= 0xf1) p->ev_lock = 0;
            if (p->ev_lock || op == 0 || op >= 0xf1) { /* nothing to do */ }
            else if (op < 100) {
                if (p->cont_hit == 0) {
                    p->frame = r->fr;                 /* the check uses the pose of the event's frame */
                    hit_test(f, i, damage_of(f, i, (int)op), r->cond >> 6);
                    p->ev_lock = 1;
                }
                else p->cont_hit = (int)op;           /* armed by 0xd4: keep testing every tick */
                f->hit_dir[i] = (r->cond >> 6) & 3;
            }
            else if (op >= 100 && op < 150) { p->frame = r->fr; p->health -= (int)op - 100; f->cam_shake = 1; }   /* also shakes the camera */
            else if (op >= 150 && op < 200) { p->frame = r->fr; p->health -= (int)op - 150; }
            else if (op >= 0xd0 && op <= 0xd2) set_mode_from_op(f, i, op, 0);
            else if (op >= 0xcc && op <= 0xce) {      /* break free: enough input changes so far */
                int need = p->fi ? p->fi->mash[op - 0xcc] : 25;
                if (op == 0xcc) need /= 2;
                if (p->mash >= need) { p->mash = 0; start_move(f, i, r->tgt); return; }
            }
            else if (op == 0xd4) p->cont_hit = -1;
            else if (op == 0xd8) p->turn_flag ^= 1;
            else if (op == 0xd9) p->solid ^= 1;
            else if (op == 0xda) p->edge_guard ^= 1;
            else if (op == 0xde) fighter_sound(f, i, r->tgt);
            else if (op == 0xdc) { p->health += 30; if (p->health > MAX_HEALTH) p->health = MAX_HEALTH; }
            else if (op == 0xe0) p->frame = r->fr;
            else if (op == 0xe1) emit_fx(f, i, r->cond & 0x3f, r->tgt & 0xff);   /* sprite effect on a bone group */
            else if (op == 0xe8) {                   /* set fighter distance (throws) */
                float want = (float)((int)r->tgt << 9) / 65536.0f, g = gap(f, i);
                if (f->thrower == 2) want += 0.1f;    /* the last throw was player 2's (a8344 == 2) */
                float diff = (want - fabsf(g)) * 0.5f, sg = g >= 0 ? 1.f : -1.f;
                p->s -= sg * diff; f->p[i ^ 1].s += sg * diff;
            }
            if (p->health < 0) p->health = 0;
        }
        if (p->move != move_before || CUR(p) != ms_before) return;
        p->sp++;
    }
}

/* ================================================================== */
/*  Human / AI input transitions (0x4427c4)                            */
/* ================================================================== */
static int input_transitions(Fight *f, int i)
{
    Player *p = &f->p[i];
    if (p->ringout) return 0;
    unsigned in = p->in & 0x3f, word = ((in ^ (p->prev_in & 0x3f)) | (in << 8)) & 0xffff;
    if (p->pend) word |= p->pend | (p->pend << 8);   /* port: a buffered press counts as just pressed */
    unsigned ctx = ctx_bits(f, i);
    int ost = opp_stance(f, i);
    for (int k = p->sp, guard = 0; guard < 256; k++, guard++) {
        const StateRec *r = rec(p, k);
        if (is_term(r)) break;
        if (r->minfr > p->frame) continue;
        if (r->op == 0xdf) {
            if (in & (r->mask >> 8)) { start_move(f, i, r->tgt); return 1; }   /* any-key cancel: keeps the buffer */
            continue;
        }
        if ((word & r->mask) != r->val) continue;
        int c = r->cond;
        if (c && !(c & 0x10) && c != ost) continue;
        if (c && (c & 0x10) && (c & 0x0f) == ost) continue;
        if ((unsigned)(r->ctx >> 4) != (r->ctx & ctx)) continue;
        start_move(f, i, r->tgt);
        p->pend = 0;                                  /* the buffered press is used up */
        return 1;
    }
    return 0;
}

/* ================================================================== */
/*  End of move (part of 0x44176b)                                     */
/* ================================================================== */
static int check_end(Fight *f, int i)
{
    Player *p = &f->p[i];
    int k = p->sp;
    while (!is_term(rec(p, k)) && k - p->sp < 256) k++;
    const StateRec *t = rec(p, k);
    const AniClip *c = cur_clip(p);
    int len = c ? c->nframes : 1;
    int airborne = p->mode >= 1 && p->mode <= 3 && p->z > 0;
    if (t->op == 0xc9 && !airborne) {              /* landing terminator (cb4e0 == 0) */
        start_move(f, i, t->tgt);
        return 1;
    }
    if (t->fr <= p->frame || p->frame >= len - 1) {
        if (t->cond == 0x0a) p->face = -p->face;
        if (t->op == 0xcb) start_move_t(f, i, t->tgt, p->owner ^ 1);   /* player ^ 3 */
        else start_move(f, i, t->tgt);
        return 1;
    }
    return 0;
}

/* ================================================================== */
/*  Hit resolution (0x448cbb)                                          */
/* ================================================================== */
static void push_apart(Fight *f, int victim, float amount)
{
    float g = gap(f, victim);                      /* attacker relative position */
    f->p[victim].s += g > 0 ? -amount : amount;
}

static void resolve_hit(Fight *f, int i)
{
    if (!f->hit_pending[i]) return;
    f->hit_pending[i] = 0;
    Player *a = &f->p[i], *v = &f->p[i ^ 1];
    if ((cur_move(v)->stance & 0x3f) == 8) return;
    unsigned vop = rec(v, v->sp)->op;
    const short *table = REACT_NORMAL;
    if (vop == 0xff) table = REACT_BLOCK;
    else if (vop == 0xfd) table = REACT_BLOCK_ALL;
    else if (vop == 0xfe) return;                  /* invulnerable */
    int dmg = f->hit_damage[i];
    int dir = f->hit_dir[i];
    int type = cur_move(a)->stance >> 6;
    int idx = ((cur_move(a)->stance & 0xc0) >> 3) | (f->hit_zone[i] << 5) | dir;
    if (v->health <= dmg) idx |= 4;
    int react = table[idx & 127];
    if (!react) return;
    int was_react = v->in_react;                   /* local_10: already reeling from a hit = combo hit */
    int vcls = cur_move(v)->cls;
    start_move(f, i ^ 1, react);
    /* knock back 0.1 / 0.2 / 0.3 by hit direction */
    if (dir < 3) push_apart(f, i ^ 1, 0.1f * (float)(dir + 1));
    int wounded = rec(v, v->sp)->op < 0xfa;
    if (wounded) {
        float k = v->fi ? v->fi->stun_k : 3.f;
        if (!was_react) {                          /* clean hit: full damage and stun */
            v->combo = 0;
            v->stun += k * (float)dmg;
            v->health -= dmg;
        } else {                                   /* combo hit: scaled by 0x4aa470 */
            float sc = COMBO_SCALE[v->combo];
            v->stun += k * sc * (float)dmg;
            v->health -= (int)(sc * (float)dmg);
            if (++v->combo > 19) v->combo = 19;
        }
        if (v->health < 0) v->health = 0;
        v->hit_flash = 8;
    }
    /* blood 1..4 (0x17) on a hit, block sparks 1..4 (0x13) when the reaction blocks it, by the
     * attack's direction, on the victim's body part that was hit */
    emit_fx(f, i ^ 1, f->hit_part[i], (wounded ? 0x17 : 0x13) + dir);
    if (v->stun > 75.f && v->health > 0) {         /* dazed (0x496b98) */
        v->stun = 0;
        if (vcls != 3) start_move(f, i ^ 1, MOVE_DAZE);
    }
    /* swinging attacks (attack type 1 / 2) turn the fight line around the attacker */
    if (type == 1 || type == 2)
        rotate_line_around(f, i, -(float)(dmg << 7) * 6.2831853f / 65536.0f);
    process_events(f, i ^ 1);
}

/* ================================================================== */
/*  CPU opponent (0x425892) with its helpers: situation bits 0x424f80, */
/*  situation weight 0x4250cc, move scoring 0x425166, defence 0x4256cd, */
/*  ring-edge test 0x424d8f. The AI does not press keys: it picks one   */
/*  of the current move's transitions and starts its target directly.   */
/* ================================================================== */
static const float   AI_RANGE[2]   = { 0.8f, 1.0f };                  /* 0x4a5698: close / mid / far */
static const int     AI_MUL[5][6]  = {                                /* 0x4a56a8 */
    { 0, 1, 2, 2, 3, 3 }, { 0, 1, 2, 2, 2, 2 }, { 0, 1, 1, 1, 1, 1 },  /* by range */
    { 0, 1, 1, 2, 1, 2 }, { 0, 1, 2, 1, 2, 1 } };                      /* opponent high / low */
static const uint8_t AI_ATTACK_P[8] = { 3, 4, 5, 6, 7, 8, 9, 10 };     /* 0x4a5720: rand & 15 <= p */
static const uint8_t AI_DEFEND_P[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };/* 0x4a5728 */
static const uint8_t AI_SIT_W[7]    = { 3, 3, 5, 8, 3, 2, 2 };         /* 0x4a5778 */
static const uint8_t AI_MASH[8]     = { 1, 1, 1, 2, 2, 2, 3, 4 };      /* 0x4a5790: break-free count per tick */

static int ai_level(const Fight *f) { return f->ai_level < 0 ? 0 : f->ai_level > 7 ? 7 : f->ai_level; }

static int cond_ok(int c, int ost)
{
    return c == 0 || (!(c & 0x10) && c == ost) || ((c & 0x10) && (c & 0x0f) != ost);
}

/* target of a transition in the fighter's own table (0x4d6390 + player * 0x4800) */
static const MoveDef *ai_target(const Player *p, int tgt)
{
    return tgt >= 0 && tgt < p->ms->nmoves ? &p->ms->moves[tgt] : NULL;
}

/* 0x424f80: what the opponent o is doing */
static unsigned ai_situation(const Fight *f, int o)
{
    const MoveDef *m = cur_move(&f->p[o]);
    unsigned s = 0;
    if (m->cls == 2) s |= 0x01;                   /* crouching */
    if (m->cls == 1) s |= 0x02;                   /* in the air / falling */
    if (m->cls == 3) s |= 0x04;                   /* on the floor */
    if (f->p[o].stun != 0) s |= 0x08;             /* dizzy meter not empty (cb3f8) */
    if (f->p[o].guard) s |= 0x10;                 /* blocking (a8278) */
    if ((m->ai[0] & 3) == 1) s |= (m->ai[0] & 0x40) ? 0x20 : 0x40;   /* attacking high / low */
    /* 0x80: 0x424d8f(o, 0) is always 0 */
    return s;
}

/* 0x4250cc */
static int ai_weight(unsigned sit, unsigned mask, int lv)
{
    int w = 0;
    for (int b = 0; b < 7; b++) if (sit & mask & (1u << b)) w += AI_SIT_W[b] + lv;
    return w ? w : 1;
}

/* 0x424d8f(kind, i): is the point 2 units from the fighter's followed pelvis position (a8330 / a8320) outside
 * the ring square (|x| > R || |y| > R, R = fi 0x54). fd = the direction toward the opponent along the line
 * (gap 0 counts as the minus side). kind 1: 2 units to the left of fd, 2: to the right, 3: 2 units behind
 * (away from the opponent), 0: never (situation bit 0x80 is always 0). */
static int ai_edge(const Fight *f, int i, int kind)
{
    if (!kind) return 0;
    float ux, uy;
    fight_line_dir(f, &ux, &uy);
    float sg = gap(f, i) > 0 ? 1.f : -1.f, fx = ux * sg, fy = uy * sg;
    float x = f->p[i].tx, y = f->p[i].ty;
    if (kind == 1) { x -= 2.f * fy; y += 2.f * fx; }
    else if (kind == 2) { x += 2.f * fy; y -= 2.f * fx; }
    else { x -= 2.f * fx; y -= 2.f * fy; }
    return fabsf(x) > f->ring_half || fabsf(y) > f->ring_half;
}

/* the opponent's attack: frame of its next hit event + 1 (100: continuous hit already
 * running, 0: none); *hi receives the attack's height bit (a5784) */
static int ai_threat(const Fight *f, int i, int *hi)
{
    const Player *o = &f->p[i ^ 1];
    const MoveDef *m = cur_move(o);
    if ((m->ai[0] & 3) != 1) return 0;
    int k = m->state, passed = 0;
    const StateRec *r = rec(o, k);
    int cont = r->op == 0xd4;
    for (; r->fr < o->frame && !is_term(r); r = rec(o, ++k)) {
        if (r->op && r->op < 0x78) passed = 1;
        if (r->op == 0) passed = 0;
    }
    for (; !passed && (r->op == 0 || r->op > 0x78) && !is_term(r); r = rec(o, ++k)) {}
    if (cont && passed) return 100;
    if (r->op == 0) return 0;
    *hi = (m->ai[0] >> 6) & 1;
    return r->fr + 1;
}

/* 0x4256cd: a blocking transition (target kind 2), preferably on the attack's side */
static int ai_defend(const Fight *f, int i, int threat, int nslots, int ohi, int *slot, int *score)
{
    const Player *p = &f->p[i], *o = &f->p[i ^ 1];
    int found = -1;
    *slot = 0; *score = 0;
    if (!(threat != o->frame && threat - o->frame >= 0)) return -1;
    if ((rand() & 15) > AI_DEFEND_P[ai_level(f)]) return -1;
    if (fabsf(gap(f, i)) >= 2.1f) return -1;
    for (int k = p->sp; k - p->sp < 256; k++) {
        const StateRec *r = rec(p, k);
        if (is_term(r)) break;
        const MoveDef *t = ai_target(p, r->tgt);
        if (!t || (t->ai[0] & 3) != 2) continue;
        *slot = nslots - 1 - ohi; *score = 5000; found = k;
        if (p->ai_def_hi == ((t->ai[0] >> 6) & 1)) break;
    }
    return found;
}

/* 0x425166: best scoring transition of the current move; returns its record index
 * (the terminator when nothing scores) */
static int ai_choose(const Fight *f, int i, int range, int nslots, int threat, unsigned sit,
                     int *slot, int *score)
{
    const Player *p = &f->p[i], *o = &f->p[i ^ 1];
    const MoveDef *om = cur_move(o);
    int lv = ai_level(f), ost = om->stance & 0x1f, low = !((om->ai[0] >> 6) & 1);
    unsigned ctx = ctx_bits(f, i);
    int best = -1, k = p->sp;
    *slot = 0; *score = 0;
    for (; k - p->sp < 256; k++) {
        const StateRec *r = rec(p, k);
        int cok = cond_ok(r->cond, ost), xok = (unsigned)(r->ctx >> 4) == (r->ctx & ctx);
        if (r->mask == 0 && r->ctx && xok && cok) {     /* context transition without input: always */
            *score = 999999; *slot = nslots;
            return k;
        }
        const MoveDef *t = ai_target(p, r->tgt);
        if (t && r->val != 0xff00 && r->mask != 0xff00 && cok && xok && r->minfr <= p->frame) {
            int w = ai_weight(sit, t->ai[6], lv);
            for (int s = 1; s < nslots; s++) {
                int v = t->ai[s] * AI_MUL[range][s] * AI_MUL[3 + low][s];
                if (w > 1) v *= w;
                v -= om->ai[s];
                if (v < *score || (rand() & 15) > AI_ATTACK_P[lv]) continue;
                if (ai_edge(f, i, (t->ai[0] >> 2) & 3) || (t->ai[0] & 0x80)) continue;
                if (t->ai[7] && threat && t->ai[7] > threat - o->frame) continue;
                *score = v; *slot = s; best = k;
            }
        }
        if (is_term(r)) break;
    }
    if (best < 0) { *score = 0; *slot = 0; best = k; }
    return best;
}

/* 0x425892: returns 1 when a move was started */
static int ai_think(Fight *f, int i)
{
    Player *p = &f->p[i], *o = &f->p[i ^ 1];
    if (p->ringout || f->winner >= 0 || f->round_age < f->start_delay || CUR(p) != p->ms) return 0;
    for (int k = 0; k < 2; k++) if ((cur_move(&f->p[k])->stance & 0x3f) == 8) return 0;   /* throw */
    int lv = ai_level(f);
    float d = fabsf(gap(f, i));
    int range = 0;
    while (range < 2 && AI_RANGE[range] <= d) range++;
    int nslots = 6 - 2 * range;
    if (facing_away(f, i)) {                      /* turn around first (target kind 3) */
        for (int k = p->sp; k - p->sp < 256; k++) {
            const StateRec *r = rec(p, k);
            if (is_term(r)) break;
            const MoveDef *t = ai_target(p, r->tgt);
            if (t && (t->ai[0] & 3) == 3) { start_move(f, i, r->tgt); return 1; }
        }
    }
    int threat = ai_threat(f, i, &p->ai_def_hi);
    unsigned sit = ai_situation(f, i ^ 1);
    int ohi = (cur_move(o)->ai[0] >> 6) & 1;
    unsigned cop = rec(p, p->sp)->op;             /* waiting on a break-free op: mash instead of acting */
    if (cop >= 0xcc && cop <= 0xce) { p->mash += AI_MASH[lv]; return 0; }
    if ((cur_move(p)->ai[0] & 3) == 0) {          /* neutral move: wait 8 - level ticks */
        if (!p->ai_armed && !p->ai_wait) { p->ai_wait = 8 - lv; p->ai_armed = 1; }
        if (p->ai_wait) { p->ai_wait--; return 0; }
    }
    int started = 0, prev = 0;
    for (int it = 0; it < 3; it++) {
        int dslot = 0, dscore = 0, didx = -1;
        if (threat || prev || p->ai_def) didx = ai_defend(f, i, threat, nslots, ohi, &dslot, &dscore);
        if (p->ai_def > 0) p->ai_def--;
        int slot, score, idx = ai_choose(f, i, range, nslots, threat, sit, &slot, &score);
        prev = idx - p->sp;
        int bslot = slot, bscore = score > 0 ? score : 0, bidx = idx;
        if (threat > 0 && bscore < 1000 && (rand() & 15) <= AI_DEFEND_P[lv]) {
            bscore = dscore; bslot = dslot; bidx = didx;
            p->ai_def = threat + 1;
        }
        if (bscore == 0 || bidx < 0) break;
        if ((int)cur_move(p)->ai[bslot] < bscore) {
            start_move(f, i, rec(p, bidx)->tgt);
            p->ai_armed = 0;
            started = 1;
            if (getenv("FX_AITRACE"))
                fprintf(stderr, "AI P%d range %d slot %d score %d -> %s\n", i + 1, range, bslot, bscore, player_move_name(p));
        }
    }
    return started;
}

/* ================================================================== */
/*  Movement modes (0x440f42): jumps and lunges                        */
/* ================================================================== */
/* height of the lowest point of the fighter: the mesh when it is known (the soles of the
 * feet are below the foot joints), otherwise the joints */
static float lowest_joint(const Fight *f, int i)
{
    const Player *p = &f->p[i];
    const AniFrame *fr = player_frame(p);
    float mn = 1e9f;
    if (fr && p->mesh && p->nmesh > 0) {
        float base = root_h(p) + p->z;
        int nb = p->nmesh < ANI_MAX_BONES ? p->nmesh : ANI_MAX_BONES;
        for (int b = 0; b < nb; b++) {
            const Model *m = &p->mesh[b];
            if (!m->nverts) continue;
            float M[4][3]; ani_bone_matrix(fr, b, M);
            for (int k = 0; k < m->nverts; k++) {
                const Vec3 *v = &m->verts[k];
                /* the pelvis part also carries Venam's tail and Sheba's loincloth (up to 0.5 out): lying
                 * down they would hold the body above the floor; the original only aligns the feet */
                if (b == 7 && (v->y < -0.15f || v->z < -0.16f)) continue;   /* other pelvises: y, z >= -0.14 */
                float z = v->x * M[0][2] + v->y * M[1][2] + v->z * M[2][2] + M[3][2];
                if (z < mn) mn = z;
            }
        }
        return mn + base;
    }
    for (int b = 0; b < 16; b++) { float j[3]; fight_joint(f, i, b, j); if (j[2] < mn) mn = j[2]; }
    return mn;
}

static float outside(const float c[3], float half);

/* Keeps the fighter on the floor (the landing code around FUN_00440f42 / a8290 aligns the
 * lowest foot with the floor height under the fighter). Out of the ring: falls with gravity
 * onto the lower ground at the arena's fall depth, or further if there is nothing below. */
static void ground(Fight *f, int i)
{
    Player *p = &f->p[i];
    if (p->ringout) {
        float pel[3]; fight_joint(f, i, 7, pel);
        float floor = 0;
        if (outside(pel, f->ring_half) > 0) {
            floor = f->fall_depth;
            if (f->land_half > 0 && outside(pel, f->land_half - 0.2f) > 0) floor = -25.f;   /* nothing below */
        }
        float low = lowest_joint(f, i);
        if (low > floor + 0.002f || p->vz > 0) { p->vz -= 9.8f / FIGHT_HZ; p->z += p->vz / FIGHT_HZ; }
        low = lowest_joint(f, i);
        if (low < floor) { p->z += floor - low; p->vz = 0; }
        return;
    }
    if (p->mode >= 1 && p->mode <= 3) return;          /* jump arc: move_modes() */
    if (p->floor_h < 0) {                               /* past the edge without a ring-out (a KO, a fall
                                                         * over the edge): down onto the lower ground */
        float low = lowest_joint(f, i);
        if (low > p->floor_h + 0.002f || p->vz > 0) { p->vz -= 9.8f / FIGHT_HZ; p->z += p->vz / FIGHT_HZ; }
        low = lowest_joint(f, i);
        if (low < p->floor_h) { p->z += p->floor_h - low; p->vz = 0; }
        return;
    }
    p->vz = 0;
    float low = lowest_joint(f, i);
    if (low < 0) p->z -= low;
    else if (p->z > 0 && p->land_snap) p->z -= fminf(p->z, low);   /* after a jump: straight onto the floor */
    else if (p->z > 0) p->z -= fminf(fminf(p->z, low), 0.07f);
    if (p->land_snap) p->land_snap--;
}

static void move_modes(Fight *f, int i)
{
    Player *p = &f->p[i];
    float toward = gap(f, i) >= 0 ? 1.f : -1.f;
    if (p->mode >= 1 && p->mode <= 3) {
        const FighterInfo *fi = p->fi;
        float v0 = fi ? fi->jump_v : 0.7f, g = fi ? fi->jump_g : -1.4f, sc = fi ? fi->jump_s : 8.f;
        p->jt += TICK_T;
        p->z = (p->jt * v0 + p->jt * p->jt * g * 0.5f) * sc + 0.3f;   /* FUN_00440f42 */
        if (p->mode >= 2) p->s += toward * p->vel * TICK_T;
        float low = lowest_joint(f, i);
        if (low < 0) p->z -= low;                     /* feet never go through the floor */
        if (p->jt > 0.3f && (p->z <= 0.3f || low < 0)) {   /* landed */
            if (p->z < 0) p->z = 0;
            p->mode = 0; p->vel = 0;
            /* the landing code (0x43f36e region, cb4e0 = -1 -> 0) puts the lowest foot on the floor in one
             * step; the next move (JUMP LAND 3) has its own pelvis height, so a slow settle would pop it up */
            p->land_snap = 4;
        }
    } else if (p->mode >= 4) {
        p->s += toward * p->vel * TICK_T;
    }
}

/* ================================================================== */
/*  Spacing (0x43fbca): soft push by body radii                        */
/* ================================================================== */
static float body_height(const Fight *f, int i)  /* centre of the merged thorax (15) + left forearm (4) boxes */
{
    HitBox t, r;
    part_box(f, i, 15, &t);
    part_box(f, i, 4, &r);
    box_merge(&t, &r);
    return t.c[2];
}

static void spacing(Fight *f)
{
    for (int i = 0; i < 2; i++) {
        Player *p = &f->p[i];
        int cls = cur_move(p)->cls;
        float target = p->fi && cls < 6 ? p->fi->body_r[cls] : 0.3f;
        if (target <= 0) target = 0.3f;
        if (p->radius < target) { p->radius += 0.02f; if (p->radius > target) p->radius = target; }
        if (p->radius > target) { p->radius -= 0.02f; if (p->radius < target) p->radius = target; }
    }
    Player *a = &f->p[0], *b = &f->p[1];
    float g = b->s - a->s;                        /* local_74 */
    if (a->ringout || b->ringout) { f->last_gap = g; return; }
    float zd = fabsf(body_height(f, 0) - body_height(f, 1));
    int cross = (g < 0) != (f->last_gap < 0);     /* local_44: the sides swapped since the remembered gap */
    int ga = cur_move(a)->cls == 3, gb = cur_move(b)->cls == 3;   /* a fighter on the floor is not pushed */
    if (a->solid && b->solid && zd < 0.65f) {
        float d = fabsf(g) - (a->radius + b->radius);
        if (d < 0) {                              /* push apart by 0.3 of the overlap, towards the remembered sides */
            d *= 0.3f;
            if (f->last_gap <= 0) { if (ga) b->s += d; else a->s -= d; if (gb) a->s -= d; else b->s += d; }
            else                  { if (ga) b->s -= d; else a->s += d; if (gb) a->s += d; else b->s -= d; }
        }
        if (cross) {                              /* passed through each other: meet in the middle */
            float l = g * 0.5f;
            if (ga) b->s -= l; else a->s += l;
            if (gb) a->s += l; else b->s -= l;
        }
        if (f->last_gap != 0) g = f->last_gap;    /* a8264 keeps the side from before the contact */
    }
    if (f->no_floor) {                            /* floor detail 0: the gap is held at 8.0 (0x496a48) */
        float d = b->s - a->s, ex = fabsf(d) - 8.0f;
        if (ex > 0) { float h = d > 0 ? ex * 0.5f : -ex * 0.5f; a->s += h; b->s -= h; }
    }
    f->last_gap = g;
}

/* ================================================================== */
/*  Ring edge (0x4408f9) and ring-out (0x43f36e)                       */
/* ================================================================== */
static float outside(const float c[3], float half) { return fmaxf(fabsf(c[0]), fabsf(c[1])) - half; }

/* The pelvis keeps `margin` (0.5, or 1.8 when ring-outs are off or the opponent's pelvis has
 * been out) from the walls at ring_half + 1.3, measured ALONG the fight line in both directions. */
static void ring_edge(Fight *f, int i)
{
    Player *p = &f->p[i];
    if (f->no_floor) return;                          /* 0x4408f9 runs only with a floor model */
    if (p->ringout || (cur_move(p)->stance & 0x3f) == 8) return;
    float W = f->ring_half + 1.3f;
    float margin = (f->p[i ^ 1].pelvis_out || (p->edge_guard && !f->ringouts)) ? 1.8f : 0.5f;
    float c[3], ux, uy;
    fight_joint(f, i, 7, c);
    fight_line_dir(f, &ux, &uy);
    float t[4] = { 0, 0, 0, 0 };                   /* (pelvis - wall) / direction for the 4 walls */
    if (ux != 0) { t[0] = (c[0] + W) / ux; t[1] = (c[0] - W) / ux; }
    if (uy != 0) { t[2] = (c[1] + W) / uy; t[3] = (c[1] - W) / uy; }
    float behind = 65535.f, ahead = -65535.f;
    for (int k = 0; k < 4; k++) {
        if (t[k] > 0 && t[k] < behind) behind = t[k];
        if (t[k] < 0 && t[k] > ahead) ahead = t[k];
    }
    int push_fwd = behind - margin < 0, push_back = margin + ahead > 0;
    if (push_fwd) p->s += margin - behind;
    if (push_back) p->s -= margin + ahead;
    /* port option (ring_guard): a human's own moves (not a hit reaction or a throw) keep the pelvis
     * and both feet on the ring, so he cannot step out by himself; hits still can throw him out */
    if (f->ring_guard && f->ringouts && !p->cpu && !p->in_react) {
        static const int J[3] = { 7, 1, 9 };
        static const float LIM[3] = { 0.15f, 0.02f, 0.02f };   /* inside the edge: pelvis, feet */
        for (int it = 0; it < 3; it++) {
            float best = 0;
            for (int k = 0; k < 3; k++) {
                float lim = f->ring_half - LIM[k], sh = 0;
                fight_joint(f, i, J[k], c);
                if (c[0] > lim && fabsf(ux) > 0.05f) sh = (lim - c[0]) / ux;
                else if (c[0] < -lim && fabsf(ux) > 0.05f) sh = (-lim - c[0]) / ux;
                if (c[1] > lim && fabsf(uy) > 0.05f) { float t2 = (lim - c[1]) / uy; if (fabsf(t2) > fabsf(sh)) sh = t2; }
                else if (c[1] < -lim && fabsf(uy) > 0.05f) { float t2 = (-lim - c[1]) / uy; if (fabsf(t2) > fabsf(sh)) sh = t2; }
                if (fabsf(sh) > fabsf(best)) best = sh;
            }
            if (best == 0) break;
            p->s += best;
        }
    }
    if (p->mode >= 1 && p->mode <= 3) {           /* own decision: a jump does not carry out of the ring */
        float lim = f->ring_half - 0.3f;
        for (int it = 0; it < 2; it++) {
            fight_joint(f, i, 7, c);
            float s = 0;
            if (c[0] > lim && fabsf(ux) > 0.05f) s = (lim - c[0]) / ux;
            else if (c[0] < -lim && fabsf(ux) > 0.05f) s = (-lim - c[0]) / ux;
            else if (c[1] > lim && fabsf(uy) > 0.05f) s = (lim - c[1]) / uy;
            else if (c[1] < -lim && fabsf(uy) > 0.05f) s = (-lim - c[1]) / uy;
            if (s == 0) break;
            p->s += s;
        }
    }
}

/* pelvis position followed with a 0.1 dead zone at 1/4 of the excess (end of 0x43fbca) */
static void track_pelvis(Fight *f, int i, int snap)
{
    Player *p = &f->p[i];
    float c[3];
    fight_joint(f, i, 7, c);
    if (snap) { p->tx = c[0]; p->ty = c[1]; return; }
    if (c[0] > p->tx + 0.1f) p->tx += (c[0] - (p->tx + 0.1f)) / 4.f;
    if (c[0] < p->tx - 0.1f) p->tx -= ((p->tx - 0.1f) - c[0]) / 4.f;
    if (c[1] > p->ty + 0.1f) p->ty += (c[1] - (p->ty + 0.1f)) / 4.f;
    if (c[1] < p->ty - 0.1f) p->ty -= ((p->ty - 0.1f) - c[1]) / 4.f;
}

/* 0x43f36e. `farther` (0x445002): only the fighter further from the centre can fall out. */
static void ring_out_check(Fight *f, int i, int farther)
{
    Player *p = &f->p[i];
    if (p->ringout) return;
    if (f->no_floor) { p->floor_h = 0; return; }    /* floor detail 0: cb498 = 0, nobody falls out */
    float pel[3], lf[3], rf[3];
    fight_joint(f, i, 7, pel); fight_joint(f, i, 1, lf); fight_joint(f, i, 9, rf);
    float h = f->ring_half;
    if (outside(pel, h - 0.1f) > 0) p->pelvis_out = 1;          /* df3c8, kept until the round ends */
    int feet_out = outside(lf, h) > 0 || outside(rf, h) > 0;
    /* cb498: the floor under the fighter, set every tick whether he falls out or not: the lower
     * ground once the pelvis (h - 0.1) or a foot (h) is past the edge; 0 with ring-outs off */
    p->floor_h = f->ringouts && (outside(pel, h - 0.1f) > 0 || feet_out) ? f->fall_depth : 0;
    if (!f->ringouts || f->winner >= 0 || farther != i) return;
    /* the fall is picked by facing (cb490) and whether the opponent's pelvis has been out */
    int same = !facing_away(f, i) == !f->p[i ^ 1].pelvis_out;
    int m = -1;
    if (pel[2] < 0.15f && p->pelvis_out) m = same ? MOVE_RO_HEAD : MOVE_RO_FEET;
    else if (pel[2] >= 0.15f && feet_out && (lf[2] < 0.2f || rf[2] < 0.2f)) m = same ? MOVE_RO_BACK : MOVE_RO_STEP;
    if (m < 0) return;
    start_move(f, i, m);
    p->ringout = 1;
    f->winner = i ^ 1;
    f->ko_reason = 1;
    f->p[f->winner].wins++;
    f->over_timer = 6 * FIGHT_HZ;
}

/* ================================================================== */
/*  Setup / per-tick update                                            */
/* ================================================================== */
void fight_init_player(Player *pl, const MoveSet *ms, const AniSet *own, const AniSet *generic,
                       const FighterInfo *fi)
{
    int wins = pl->wins, cpu = pl->cpu, nmesh = pl->nmesh;
    const Model *mesh = pl->mesh;
    memset(pl, 0, sizeof *pl);
    pl->wins = wins; pl->cpu = cpu; pl->mesh = mesh; pl->nmesh = nmesh;
    pl->ms = ms; pl->cur = ms; pl->own = own; pl->cur_own = own; pl->generic = generic; pl->fi = fi;
}

void fight_set_arena(Fight *f, const FighterInfo *afi)
{
    f->ring_half = afi && afi->ring_half > 0.5f ? afi->ring_half : 5.04f;
    f->fall_depth = afi ? afi->fall_depth : -1.0f;
}

void fight_reset(Fight *f)
{
    f->round_timer = (f->round_secs > 0 ? f->round_secs : 60) * FIGHT_HZ;
    f->clock_acc = 0;
    f->over_timer = 0;
    f->winner = -1;
    f->ko_reason = 0;
    f->round_age = 0;
    f->ox = 0; f->oy = 0; f->ang = 1.5707963f;      /* line along world +Y, a8280/4 = -1 / +1 */
    if (f->ring_half <= 0) f->ring_half = 5.04f;
    for (int i = 0; i < 2; i++) {
        Player *p = &f->p[i];
        p->s = i ? 1.0f : -1.0f;
        p->face = i ? -1 : 1;
        p->turn_flag = 0;
        p->health = MAX_HEALTH;
        p->in = p->prev_in = 0;
        p->move = -1;
        p->aset = NULL;
        p->cur = p->ms;
        p->owner = i;
        p->z = 0; p->mode = 0; p->vel = 0; p->jt = 0; p->land_snap = 0;
        p->radius = 0.3f;
        p->ringout = 0; p->vz = 0; p->floor_h = 0; p->have_prev_box = 0;
        p->stun = 0; p->combo = 0; p->mash = 0; p->in_react = 0; p->pend = 0; p->pend_t = 0;
        p->pelvis_out = 0;
        f->hit_pending[i] = 0;
    }
    for (int i = 0; i < 2; i++) start_move(f, i, MOVE_STAND);
    f->last_gap = f->p[1].s - f->p[0].s;
    for (int i = 0; i < 2; i++) track_pelvis(f, i, 1);
}

void fight_tick(Fight *f)
{
    if (f->winner >= 0 || f->round_age < f->start_delay) { f->p[0].in = f->p[1].in = 0; }

    {   /* 0x445002: player 2 is checked when he is at least as far from the centre */
        int farther = f->p[0].tx * f->p[0].tx + f->p[0].ty * f->p[0].ty <=
                      f->p[1].tx * f->p[1].tx + f->p[1].ty * f->p[1].ty;
        for (int i = 0; i < 2; i++) ring_out_check(f, i, farther);
    }
    for (int i = 0; i < 2; i++) {
        Player *p = &f->p[i];
        if (p->in != p->prev_in) p->mash++;       /* 0x44176b: every input change counts for breaking free */
        if (p->cpu || f->input_buffer <= 0) p->pend = 0;
        else {                                    /* port: punch / kick presses wait a few ticks for a move */
            if (p->pend && --p->pend_t <= 0) p->pend = 0;
            unsigned press = p->in & ~p->prev_in & (IN_PUNCH | IN_KICK);
            if (press) { p->pend |= press; p->pend_t = f->input_buffer; }
        }
        process_events(f, i);
        if (p->cont_hit > 0 && hit_test(f, i, damage_of(f, i, p->cont_hit), f->hit_dir[i])) p->cont_hit = 0;
        unsigned op = rec(p, p->sp)->op;          /* a8278: guard state seen by the CPU opponent */
        if (op >= 0xf1) p->guard = (int)op; else if (op < 200) p->guard = 0;
        if (p->cpu ? ai_think(f, i) : input_transitions(f, i)) process_events(f, i);
        if (check_end(f, i)) {
            process_events(f, i);
            /* 0x44176b calls the AI again only when the new move's first record fires at frame 0 */
            if (p->cpu ? rec(p, p->sp)->fr == 0 && ai_think(f, i) : input_transitions(f, i)) process_events(f, i);
        }
        p->prev_in = p->in;
    }
    resolve_hit(f, 0);
    resolve_hit(f, 1);

    /* advance animation + root motion (0x445f16) */
    for (int i = 0; i < 2; i++) {
        Player *p = &f->p[i];
        p->frame++;
        const AniClip *c = cur_clip(p);
        if (c && p->frame >= c->nframes) p->frame = c->nframes - 1;
        int fwd = root_fwd(p);
        p->s -= (float)p->face * (float)(fwd - p->last_fwd) * ANI_UNIT;   /* anim forward is local -Y */
        p->last_fwd = fwd;
        if (p->hit_flash > 0) p->hit_flash--;
        move_modes(f, i);
        ground(f, i);
    }
    spacing(f);
    for (int i = 0; i < 2; i++) ring_edge(f, i);
    for (int i = 0; i < 2; i++) track_pelvis(f, i, 0);
    for (int i = 0; i < 2; i++) ring_edge(f, i);   /* 0x43fbca runs it twice */
    for (int i = 0; i < 2; i++) {                 /* dizzy meter recovers (around 0x4f1b..., per frame) */
        Player *p = &f->p[i];
        p->stun -= 5.f * (p->fi ? p->fi->stun_decay : 0.4f);
        if (p->stun > 64.f) p->stun = 64.f;
        if (p->stun < 0.f) p->stun = 0.f;
    }

    /* round logic (announcer: generic.fxl, FUN_00444b60 area) */
    f->round_age++;
    if (f->round_age == FIGHT_HZ) emit_sound(f, 0, 19);                 /* beam-in */
    if (f->round_age == FIGHT_HZ * 22 / 10) emit_sound(f, 0, 1);       /* "FIGHT!" */
    if (f->winner < 0) {
        /* the round clock runs in real time (0x445652: end time - timer), the fight at tick_pit;
         * round_timer counts FIGHT_HZ steps per second */
        if (f->round_secs >= 0) f->clock_acc += f->tick_pit > 0 ? f->tick_pit : 24850;
        while (f->clock_acc >= 24850 && f->round_timer > 0) {
            f->clock_acc -= 24850;
            f->round_timer--;
            if (f->round_timer > 0 && f->round_timer < 6 * FIGHT_HZ && f->round_timer % FIGHT_HZ == 0)
                emit_sound(f, 0, 17);                                  /* countdown tick */
        }
        int ko0 = f->p[0].health <= 0, ko1 = f->p[1].health <= 0;
        if (ko0 || ko1 || f->round_timer == 0) {
            if (ko0 && !ko1) f->winner = 1;
            else if (ko1 && !ko0) f->winner = 0;
            else f->winner = f->p[0].health >= f->p[1].health ? 0 : 1;
            f->ko_reason = (ko0 || ko1) ? 0 : 2;
            f->p[f->winner].wins++;
            f->over_timer = 6 * FIGHT_HZ;
            if (f->ko_reason == 2) emit_sound(f, 0, 4);                /* "time" */
        }
    } else {
        f->over_timer--;
        if (f->over_timer == 5 * FIGHT_HZ) {
            /* winner announcement: vs CPU "you win" / "you lose", otherwise "player 1/2 wins" */
            int h0 = !f->p[0].cpu, h1 = !f->p[1].cpu;
            if (f->name_snd[f->winner] > 0) emit_sound(f, 0, f->name_snd[f->winner]);   /* tournament */
            else if (h0 != h1) emit_sound(f, 0, (h0 ? f->winner == 0 : f->winner == 1) ? 2 : 3);
            else emit_sound(f, 0, f->winner == 0 ? 14 : 15);
            if (f->p[f->winner].health == MAX_HEALTH) emit_sound(f, 0, 7);
        }
        if (f->over_timer == 4 * FIGHT_HZ) {
            Player *w = &f->p[f->winner], *l = &f->p[f->winner ^ 1];
            if (w->owner == f->winner) start_move(f, f->winner, MOVE_VICTORY);
            if (l->health > 0 && !l->ringout && l->owner == (f->winner ^ 1)) start_move(f, f->winner ^ 1, MOVE_DEFEAT);
        }
        if (f->over_timer <= 0) { if (f->hold) f->over_timer = 0; else fight_reset(f); }
    }
}
