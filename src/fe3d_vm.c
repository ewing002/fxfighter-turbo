/*
 * FX Fighter Turbo - open reimplementation
 * 3D front end: the strat bytecode VM (see fe3d_vm.h, docs/FRONTEND.md).
 * Function names in comments are the FIGHT.EXE addresses the code follows.
 */
#include "fe3d_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FULLTURN   0xe100000            /* 3600 << 16 */
#define STRAT_TAB  0x49fb88u
#define KEYMODEL   0x49fd01u            /* scan code -> key model type (op E4/E5) */
#define WOBBLE_TAB 0x49ff18u            /* op 0x54/0x7e */
#define CFG_VA     0x4a5878u
#define RTIME_TAB  0x4a6fe0u
#define KEYDEF1    0x4a58a8u
#define KEYDEF2    0x4a58aeu
#define RAD2ANG    37549362.0           /* 1800*65536/pi, 0x496040 */

/* actor field offsets */
enum { O_VX = 0x10, O_VY = 0x14, O_VZ = 0x18, O_ACC = 0x1c, O_SPD = 0x20, O_TSPD = 0x24, O_ID = 0x28,
       O_SER = 0x2c, O_RX = 0x30, O_RY = 0x34, O_RZ = 0x38, O_X = 0x3c, O_Y = 0x40, O_Z = 0x44,
       O_SX = 0x48, O_SY = 0x4c, O_SZ = 0x50, O_CMASK = 0x54, O_COL = 0x58, O_ZOFS = 0x5c,
       O_WOB = 0x98, O_TYPE = 0xd4, O_TICK = 0xd8, O_FLAGS = 0xdc, O_ARG = 0xe0, O_WORLD = 0xe4 };

/* ------------------------------------------------------------------ */
/*  small helpers                                                      */
/* ------------------------------------------------------------------ */
static void vm_err(FeVm *vm, const FeActor *a, const char *msg)
{
    vm->errors++;
    if (vm->debug) fprintf(stderr, "fevm: strat %d ip %x: %s\n", a ? a->strat : -1, a ? a->ip : 0, msg);
}

static int32_t FL(const FeActor *a, int off) { int32_t v; memcpy(&v, (const uint8_t *)a->f + off, 4); return v; }
static void SF(FeActor *a, int off, int32_t v) { memcpy((uint8_t *)a->f + off, &v, 4); }
static void AF(FeActor *a, int off, int32_t v) { SF(a, off, (int32_t)((uint32_t)FL(a, off) + (uint32_t)v)); }
static int32_t fx(int v) { return (int32_t)((uint32_t)v << 16); }          /* int -> 16.16 */
static int32_t wrap_ang(int64_t v) { return (int32_t)(v % FULLTURN); }    /* C '%' like MSVC */
static int iabs32(int32_t v) { return v < 0 ? -v : v; }

static uint8_t rd8(const FeVm *vm, uint32_t va)
{
    const uint8_t *p = exe_ptr(&vm->fe->exe, va, 1);
    return p ? *p : 0;
}
static int16_t rd16(const FeVm *vm, uint32_t va) { return (int16_t)(rd8(vm, va) | rd8(vm, va + 1) << 8); }
static size_t rdlen(const FeVm *vm, uint32_t va) { size_t n = 0; while (n < 4096 && rd8(vm, va + (uint32_t)n)) n++; return n; }

static int msrand(FeVm *vm) { vm->rng = vm->rng * 214013u + 2531011u; return (int)((vm->rng >> 16) & 0x7fff); }

static int32_t fixmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static int32_t fixdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * 65536) / b); }

static uint32_t strat_addr(const FeVm *vm, int n)
{
    return (n >= 0 && n < FE_NUM_STRATS) ? vm->fe->strat[n] : 0;
}

/* ---------- value stack (FUN_00404dd4 / FUN_00404e9a) ---------- */
static void push(FeVm *vm, FeActor *a, int32_t v)
{
    if (a->sp >= FEVM_STACK) { vm_err(vm, a, "stack overflow"); return; }
    a->stack[a->sp++] = v;
}
static int32_t pop(FeVm *vm, FeActor *a)
{
    if (a->sp <= 0) { vm_err(vm, a, "stack underflow"); return 0; }
    return a->stack[--a->sp];
}

/* ------------------------------------------------------------------ */
/*  triggers                                                           */
/* ------------------------------------------------------------------ */
static int trig_find(FeVm *vm, FeActor *a, uint32_t target)      /* FUN_0040cb77 */
{
    for (int t = a->trig; t >= 0; t = vm->t[t].next) if (vm->t[t].target == target) return t;
    return -1;
}

static void trig_free(FeVm *vm, FeActor *a, int t)                /* FUN_0040caf8 */
{
    if (t < 0) return;
    FeTrig *r = &vm->t[t];
    if (r->prev < 0) a->trig = r->next; else vm->t[r->prev].next = r->next;
    if (r->next >= 0) vm->t[r->next].prev = r->prev;
    memset(r, 0, sizeof *r);
}

static int trig_new(FeVm *vm, FeActor *a)
{
    for (int t = 0; t < FEVM_MAX_TRIGS; t++) {
        if (vm->t[t].used) continue;
        FeTrig *r = &vm->t[t];
        memset(r, 0, sizeof *r);
        r->used = 1;
        r->prev = -1;
        r->next = a->trig;
        if (a->trig >= 0) vm->t[a->trig].prev = t;
        a->trig = t;
        return t;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  actors                                                             */
/* ------------------------------------------------------------------ */
static int actor_alive(const FeVm *vm, int i) { return i >= 0 && i < FEVM_MAX_ACTORS && vm->a[i].live; }

/* FUN_00404a96: x..rz are 16.16, scale an integer (256 = 1.0) */
static int spawn(FeVm *vm, int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz,
                 int scale, int model, int strat, int parent)
{
    int i;
    for (i = 0; i < FEVM_MAX_ACTORS; i++) if (!vm->a[i].live) break;
    if (i == FEVM_MAX_ACTORS) { vm_err(vm, NULL, "no memory for strat block"); return -1; }
    FeActor *a = &vm->a[i];
    memset(a, 0, sizeof *a);
    a->live = 1;
    vm->has_prev[i] = 0;
    a->parent = a->child = a->sib_next = a->sib_prev = -1;
    a->target = a->hit = a->trig = -1;
    a->strat = strat;
    model &= 0xff;
    a->brtype = model == 0xff ? 0 : model == 0xfe ? 2 : 1;
    a->model = model;
    a->rparent = parent;                                        /* FUN_00414a70 */
    a->rworld = parent >= 0 ? vm->a[parent].rworld : 0;
    SF(a, O_TYPE, fx(model));
    SF(a, O_X, x); SF(a, O_Y, y); SF(a, O_Z, z);
    SF(a, O_RX, rx); SF(a, O_RY, ry); SF(a, O_RZ, rz);
    SF(a, O_SX, fx(scale)); SF(a, O_SY, fx(scale)); SF(a, O_SZ, fx(scale));
    a->ip = strat_addr(vm, strat);
    SF(a, O_SER, vm->serial++);
    vm->last = i;
    if (parent >= 0) {
        FeActor *p = &vm->a[parent];
        a->parent = parent;
        if (p->child >= 0) vm->a[p->child].sib_prev = i;
        a->sib_next = p->child;
        p->child = i;
    }
    a->prev = -1;
    a->next = vm->head;
    if (vm->head >= 0) vm->a[vm->head].prev = i;
    vm->head = i;
    return i;
}

/* world field of a spawned actor: 1/2 moves the BRender actor into RIGHT/LEFT_WORLD */
static void set_world(FeActor *n, int w)
{
    SF(n, O_WORLD, fx(w));
    if (w == 1 || w == 2) { n->rworld = w; n->rparent = -1; }
}

static void kill(FeVm *vm, int i)                                   /* FUN_00404c3c */
{
    FeActor *a = &vm->a[i];
    if (!a->live) return;
    if (vm->iter_next == i) vm->iter_next = a->next;
    if (vm->focus == i) vm->focus = -1;
    if (a->prev < 0) vm->head = a->next; else vm->a[a->prev].next = a->next;
    if (a->next >= 0) vm->a[a->next].prev = a->prev;
    while (a->child >= 0) kill(vm, a->child);
    if (a->parent >= 0) {
        FeActor *p = &vm->a[a->parent];
        if (a->sib_prev < 0) p->child = a->sib_next; else vm->a[a->sib_prev].sib_next = a->sib_next;
        if (a->sib_next >= 0) vm->a[a->sib_next].sib_prev = a->sib_prev;
    }
    while (a->trig >= 0) trig_free(vm, a, a->trig);
    a->live = 0;
    /* the original keeps dangling pointers; slots are reused here, so drop them */
    if (vm->last == i) vm->last = -1;
    for (int k = 0; k < FEVM_MAX_ACTORS; k++) {
        if (!vm->a[k].live) continue;
        if (vm->a[k].target == i) vm->a[k].target = -1;
        if (vm->a[k].hit == i) vm->a[k].hit = -1;
        if (vm->a[k].rparent == i) vm->a[k].rparent = -1;
    }
}

/* ---------- transforms ---------- */
typedef double M43[4][3];
static void m_ident(M43 m) { memset(m, 0, sizeof(M43)); m[0][0] = m[1][1] = m[2][2] = 1; }
static void m_mul(M43 out, M43 a, M43 b)        /* out = a * b (apply a, then b) */
{
    M43 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
            if (i == 3) r[i][j] += b[3][j];
        }
    memcpy(out, r, sizeof r);
}
static void m_rot(M43 m, int axis, double ang)   /* BrMatrix34RotateX/Y/Z, radians */
{
    double c = cos(ang), s = sin(ang);
    m_ident(m);
    if (axis == 0) { m[1][1] = c; m[1][2] = s; m[2][1] = -s; m[2][2] = c; }
    if (axis == 1) { m[0][0] = c; m[0][2] = -s; m[2][0] = s; m[2][2] = c; }
    if (axis == 2) { m[0][0] = c; m[0][1] = s; m[1][0] = -s; m[1][1] = c; }
}
static double ang_rad(int32_t v) { return v / 65536.0 / 10.0 * M_PI / 180.0; }

/* FUN_00414900 + FUN_00414f36: S * RotY * RotX * RotZ, translation pos/1000 */
static void actor_matrix(const FeActor *a, M43 out, int with_scale)
{
    M43 ry, rx, rz, s;
    m_rot(ry, 1, ang_rad(FL(a, O_RY))); m_rot(rx, 0, ang_rad(FL(a, O_RX))); m_rot(rz, 2, ang_rad(FL(a, O_RZ)));
    m_mul(out, ry, rx);
    m_mul(out, out, rz);
    out[3][0] = FL(a, O_X) / 65536000.0; out[3][1] = FL(a, O_Y) / 65536000.0; out[3][2] = FL(a, O_Z) / 65536000.0;
    if (with_scale) {
        m_ident(s);
        s[0][0] = FL(a, O_SX) / 16777216.0; s[1][1] = FL(a, O_SY) / 16777216.0; s[2][2] = FL(a, O_SZ) / 16777216.0;
        m_mul(out, s, out);
    }
}

/* FUN_004154c2: position relative to the world root; note the original multiplies the BRender
 * fixed point translation by 100, i.e. the result is 1/10 of strat units */
static void world_pos(const FeVm *vm, int i, int32_t out[3])
{
    M43 m, pm;
    actor_matrix(&vm->a[i], m, 1);
    for (int p = vm->a[i].rparent, g = 0; p >= 0 && g < 64; p = vm->a[p].rparent, g++) {
        actor_matrix(&vm->a[p], pm, 1);
        m_mul(m, m, pm);
    }
    for (int k = 0; k < 3; k++) out[k] = (int32_t)(m[3][k] * 65536.0) * 100;
}

static void world_rot(const FeVm *vm, int i, int32_t out[3])        /* FUN_00415539 */
{
    int64_t r[3] = { FL(&vm->a[i], O_RX), FL(&vm->a[i], O_RY), FL(&vm->a[i], O_RZ) };
    for (int p = vm->a[i].parent, g = 0; p >= 0 && g < 64; p = vm->a[p].parent, g++) {
        r[0] += FL(&vm->a[p], O_RX); r[1] += FL(&vm->a[p], O_RY); r[2] += FL(&vm->a[p], O_RZ);
    }
    for (int k = 0; k < 3; k++) out[k] = wrap_ang(r[k]);
}

/* own position, or the world position when the actor has a parent */
static void spawn_base(const FeVm *vm, int i, int32_t pos[3], int32_t rot[3])
{
    const FeActor *a = &vm->a[i];
    if (a->parent < 0) {
        pos[0] = FL(a, O_X); pos[1] = FL(a, O_Y); pos[2] = FL(a, O_Z);
        if (rot) { rot[0] = FL(a, O_RX); rot[1] = FL(a, O_RY); rot[2] = FL(a, O_RZ); }
    } else {
        world_pos(vm, i, pos);
        if (rot) world_rot(vm, i, rot);
    }
}

/* FUN_0040cbff: turn towards a point by at most `max` (16.16 tenths of a degree);
 * axes bit 2 = pitch (rx), bit 1 = yaw (ry); returns the total turned.
 * NOTE: not used by the 93 front end scripts; angle conventions not verified. */
static int32_t turn_to(FeVm *vm, int i, int32_t tx, int32_t ty, int32_t tz, int32_t max, int axes)
{
    FeActor *a = &vm->a[i];
    int32_t pos[3], rot[3];
    spawn_base(vm, i, pos, rot);
    double dx = (pos[0] - tx) / 65536.0, dy = (pos[1] - ty) / 65536.0, dz = (pos[2] - tz) / 65536.0;
    int32_t total = 0;
    for (int k = 0; k < 2; k++) {
        if (!(axes & (k == 0 ? 2 : 1))) continue;
        double ang = k == 0 ? atan2(-dy, sqrt(dx * dx + dz * dz)) : atan2(dx, dz);
        int64_t d = (int64_t)(ang * RAD2ANG) - rot[k];
        d %= FULLTURN;
        if (d > 0x7080000) d -= FULLTURN;
        if (d < -0x7080000) d += FULLTURN;
        if (d > max) d = max;
        if (d < -max) d = -max;
        if (d) {
            total += (int32_t)(d < 0 ? -d : d);
            int off = k == 0 ? O_RX : O_RY;
            SF(a, off, wrap_ang((int64_t)FL(a, off) + FULLTURN + d));
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/*  config block <-> option globals                                    */
/* ------------------------------------------------------------------ */
/* cfg offsets (DAT_004a5878 + n) */
enum { C_HC = 0, C_DIFF = 4, C_RTIME = 6, C_ROUNDS = 8, C_JOY1 = 9, C_JOY2 = 10, C_DET = 11, C_KEYS = 0x12,
       C_AUTOSAVE = 0x1f, C_EXKEY = 0x20, C_SPEED = 0x21, C_SVOL = 0x22, C_CDVOL = 0x23, C_X24 = 0x24, C_X25 = 0x25 };
/* G36..G47 <-> key bytes (two pairs are swapped in the original) */
static const int key_g2c[12] = { 0, 1, 3, 2, 4, 5, 6, 7, 9, 8, 10, 11 };

void fe_vm_cfg_to_g(FeVm *vm)                                       /* FUN_00433c23 */
{
    uint8_t *c = vm->cfg;
    int16_t rt = (int16_t)(c[C_RTIME] | c[C_RTIME + 1] << 8);
    int k;
    for (k = 0; k < 4 && vm->round_time[k] != rt; k++) {}
    if (k == 4) k = 1;
    vm->G[20] = fx(k);
    vm->G[21] = fx(c[C_ROUNDS]);
    vm->G[22] = c[C_DIFF] == 0 ? 0 : c[C_DIFF] == 5 ? fx(2) : fx(1);
    for (k = 0; k < 4; k++) vm->G[23 + k] = fx(c[C_HC + k]);
    for (k = 0; k < 7; k++) vm->G[27 + k] = fx(c[C_DET + k]);
    vm->G[34] = fx(c[C_JOY1]);
    vm->G[57] = fx(c[C_JOY2]);
    vm->G[35] = fx(c[C_EXKEY]);
    for (k = 0; k < 12; k++) vm->G[36 + k] = fx(c[C_KEYS + key_g2c[k]]);
    vm->G[48] = fx(c[C_SVOL]);
    vm->G[49] = fx(c[C_CDVOL]);
    vm->G[51] = fx(c[C_SPEED] - 1);
    vm->G[52] = fx(c[C_AUTOSAVE]);
    c[C_X24] = 0;
    vm->G[53] = 0;
    for (k = 0; k < 3; k++) vm->G[54 + k] = fx(c[C_X25 + k]);
}

void fe_vm_g_to_cfg(FeVm *vm)                                       /* FUN_00434001 */
{
    uint8_t *c = vm->cfg;
    int16_t rt = (int16_t)vm->round_time[(vm->G[20] >> 16) & 3];
    c[C_RTIME] = (uint8_t)rt; c[C_RTIME + 1] = (uint8_t)(rt >> 8);
    c[C_ROUNDS] = (uint8_t)(vm->G[21] >> 16);
    int d = vm->G[22] >> 16;
    c[C_DIFF] = d == 0 ? 0 : d == 2 ? 5 : 7;
    int k;
    for (k = 0; k < 4; k++) c[C_HC + k] = (uint8_t)(vm->G[23 + k] >> 16);
    for (k = 0; k < 7; k++) c[C_DET + k] = (uint8_t)(vm->G[27 + k] >> 16);
    c[C_JOY1] = (uint8_t)(vm->G[34] >> 16);
    c[C_JOY2] = (uint8_t)(vm->G[57] >> 16);
    c[C_EXKEY] = (uint8_t)(vm->G[35] >> 16);
    for (k = 0; k < 12; k++) c[C_KEYS + key_g2c[k]] = (uint8_t)(vm->G[36 + k] >> 16);
    c[C_SVOL] = (uint8_t)(vm->G[48] >> 16);
    c[C_CDVOL] = (uint8_t)(vm->G[49] >> 16);
    c[C_SPEED] = (uint8_t)((vm->G[51] >> 16) + 1);
    c[C_AUTOSAVE] = (uint8_t)(vm->G[52] >> 16);
    vm->G[53] = 0;
    c[C_X24] = 0;
    for (k = 0; k < 3; k++) c[C_X25 + k] = (uint8_t)(vm->G[54 + k] >> 16);
}

void fe_vm_cfg_defaults(FeVm *vm)                                   /* FUN_004342c7 */
{
    uint8_t *c = vm->cfg;
    c[C_RTIME] = 30; c[C_RTIME + 1] = 0;
    c[C_ROUNDS] = 2; c[C_DIFF] = 2; c[0] = 2; c[1] = 2; c[2] = 0; c[3] = 0;
    c[C_SPEED] = 1; c[C_AUTOSAVE] = 0; c[0x26] = 0; c[C_X24] = 0; c[C_X25] = 1;
    c[C_CDVOL] = 3; c[C_SVOL] = 7; c[C_JOY1] = 1; c[C_JOY2] = 1; c[C_EXKEY] = 0x58;
    for (int k = 0; k < 6; k++) {
        c[C_KEYS + k] = rd8(vm, KEYDEF1 + k);
        c[C_KEYS + 6 + k] = rd8(vm, KEYDEF2 + k);
    }
    c[C_DET] = c[C_DET + 1] = c[C_DET + 2] = c[C_DET + 3] = 1;
    c[C_DET + 4] = 2; c[C_DET + 5] = 2; c[C_DET + 6] = 1;
    c[0x27] = 2;
}

static void sys_call(FeVm *vm, int op) { if (vm->sys) vm->sys(vm, op, vm->sys_user); }

static void sound(FeVm *vm, int idx)                               /* FUN_00435eb4(0, idx) */
{
    if (vm->nsnd < FEVM_NSND) vm->snd[vm->nsnd++] = idx;
}

/* ------------------------------------------------------------------ */
/*  input (FUN_004277fd, FUN_004279b8, FUN_004159f6, FUN_00427a67)     */
/* ------------------------------------------------------------------ */
static uint8_t key_update(uint8_t s, int down)
{
    if (!down) return s & 0x30;
    return (s & 0x40) ? s : 0xff;
}

void fe_vm_set_input(FeVm *vm, int player, unsigned bits)
{
    if (player < 0 || player > 1) return;
    /* slots: 0 right, 1 left, 2 down, 3 up, 4 punch, 5 kick */
    static const unsigned map[6] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20 };   /* IN_FWD, IN_BACK, ... */
    for (int s = 0; s < 6; s++) vm->pad[player][s] = key_update(vm->pad[player][s], (bits & map[s]) != 0);
}

void fe_vm_key_raw(FeVm *vm, int scancode, int down)
{
    if (scancode > 0 && scancode < 256) vm->keys[scancode] = key_update(vm->keys[scancode], down);
}
void fe_vm_key_esc(FeVm *vm, int down) { fe_vm_key_raw(vm, 1, down); }

static int raw_edge(FeVm *vm, int k)                                /* FUN_004159f6(k, 0x10) */
{
    if (!(vm->keys[k] & 0x80)) return 0;
    vm->keys[k] &= 0x70;
    return 1;
}

static int pad_key(FeVm *vm, int p, int s)                          /* FUN_004279b8 */
{
    uint8_t *k = &vm->pad[p][s];
    if (vm->G[66] == 0) {                                            /* G66 == 0: edge */
        if (!(*k & 0x80)) return 0;
        *k &= 0x70;
        return 1;
    }
    return (*k & 0x0f) != 0;                                         /* level */
}

static int key_trig(FeVm *vm, int code)                             /* FUN_00427a67 */
{
    switch (code) {
    case 1:    return raw_edge(vm, 1);
    case 0x20: return pad_key(vm, 0, 0);
    case 0x1f: return pad_key(vm, 0, 1);
    case 0x2c: return pad_key(vm, 0, 2);
    case 0x1e: return pad_key(vm, 0, 3);
    case 0x2d: return pad_key(vm, 0, 4);
    case 0x2e: return pad_key(vm, 0, 5);
    case 0x23: return pad_key(vm, 1, 0);
    case 0x24: return pad_key(vm, 1, 1);
    case 0x32: return pad_key(vm, 1, 2);
    case 0x25: return pad_key(vm, 1, 3);
    case 0x31: return pad_key(vm, 1, 4);
    case 0x30: return pad_key(vm, 1, 5);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  physics / update (FUN_0040c39e)                                    */
/* ------------------------------------------------------------------ */
static void update(FeVm *vm, int i)
{
    FeActor *a = &vm->a[i];
    AF(a, O_TICK, 1);
    int32_t acc = FL(a, O_ACC), sp = FL(a, O_SPD), ts = FL(a, O_TSPD);
    if (acc != 0 && ts != sp) {
        sp += acc;
        if ((acc > 0 && ts < sp) || (acc < 0 && sp < ts)) sp = ts;
        SF(a, O_SPD, sp);
        SF(a, O_FLAGS, FL(a, O_FLAGS) | 0x8000);
    }
    SF(a, O_RX, FL(a, O_RX) % FULLTURN);
    SF(a, O_RY, FL(a, O_RY) % FULLTURN);
    SF(a, O_RZ, FL(a, O_RZ) % FULLTURN);
    uint32_t fl = (uint32_t)FL(a, O_FLAGS);
    if (fl & 0x2000) {                                              /* bank: ry += rz/8 */
        int32_t rz = FL(a, O_RZ);
        if (rz > 0x707ffff) rz -= FULLTURN;
        AF(a, O_RY, rz / 8);
    }
    if (fl & 0xc000) {
        /* FUN_00414d24: velocity = model z axis * speed (scale of the original not verified) */
        M43 m;
        actor_matrix(a, m, 0);
        sp = FL(a, O_SPD);
        SF(a, O_VX, (int32_t)(m[2][0] * sp)); SF(a, O_VY, (int32_t)(m[2][1] * sp)); SF(a, O_VZ, (int32_t)(m[2][2] * sp));
        SF(a, O_FLAGS, (int32_t)((uint32_t)FL(a, O_FLAGS) & ~0x8000u));
    }
    if (fl & 0x800) AF(a, O_VY, -0x10000);
    if ((fl & 0x100) && vm->focus >= 0) AF(a, O_Z, FL(&vm->a[vm->focus], O_VZ));
    AF(a, O_X, FL(a, O_VX)); AF(a, O_Y, FL(a, O_VY)); AF(a, O_Z, FL(a, O_VZ));
    /* collision (FUN_004047a0) needs colmask 0x58, which no front end script sets: not emulated */
    a->hit = -1;
    if ((fl & 0x200) && vm->focus >= 0 &&
        (int64_t)FL(a, O_ZOFS) + FL(&vm->a[vm->focus], O_Z) + 0x640000 < FL(a, O_Z))
        SF(a, O_FLAGS, (int32_t)((uint32_t)FL(a, O_FLAGS) | 0x1000));
}

/* ------------------------------------------------------------------ */
/*  interpreter                                                        */
/* ------------------------------------------------------------------ */
static void run(FeVm *vm, int i);

/* trigger predicates (table 0x4a0408); `removed` is set when the count ran out (FUN_0040cab4) */
static int trig_pred(FeVm *vm, int i, int t)
{
    FeActor *a = &vm->a[i];
    FeTrig *r = &vm->t[t];
    int type = r->type, key = r->key, count;
    if (type == 0) return 0;
    if (r->count != 0) {
        r->count--;
        if (r->count < 1) { trig_free(vm, a, t); if (vm->trig_cur == t) vm->trig_cur = -1; }
    }
    count = vm->t[t].used ? vm->t[t].count : 0;
    switch (type) {
    case 1: return 1;
    case 2: return (vm->frame & 1) != 0;
    case 3: return (vm->frame & 3) == 0;
    case 4: return (vm->frame & 7) == 0;
    case 5: return (vm->frame & 15) == 0;
    case 6: return (vm->frame & 31) == 0;
    case 7: return (vm->frame & 63) == 0;
    case 8: return (vm->frame & 127) == 0;
    case 9: return a->hit >= 0 && FL(a, O_COL) != 0;
    case 10: return FL(a, O_ARG) < 1;
    case 11: return vm->focus >= 0 && (int64_t)FL(&vm->a[vm->focus], O_Z) + 0x320000 < FL(a, O_Z);
    case 12: case 13: return 0;           /* DAT_004a87c0 bits: never set in the front end */
    case 14: return count == 1;
    case 15: return key_trig(vm, key);
    }
    return 0;
}

static void triggers(FeVm *vm, int i)                               /* FUN_0040c9ba */
{
    FeActor *a = &vm->a[i];
    if (vm->in_trig != 0) { vm_err(vm, a, "trigger call in trigger"); return; }
    vm->trig_next = a->trig;
    while (vm->trig_next >= 0 && a->live) {
        int t = vm->trig_next;
        vm->trig_cur = t;
        uint32_t target = vm->t[t].target;
        vm->trig_next = vm->t[t].next;
        if (trig_pred(vm, i, t)) {
            push(vm, a, (int32_t)a->ip);
            a->ip = target;
            vm->in_trig = 1;
            run(vm, i);
            if (vm->in_trig < 0 && vm->trig_cur >= 0) trig_free(vm, a, vm->trig_cur);
            vm->in_trig = 0;
        }
    }
}

/* run code of another actor until it reaches the matching "endwith" (ops 79/9F/7B/A3/A4) */
static uint32_t with_run(FeVm *vm, FeActor *a, int other, uint32_t body)
{
    FeActor *o = &vm->a[other];
    vm->with_save = o->ip;
    o->ip = body;
    vm->with_ret = a->ip;            /* fallback if the body never reaches 0x7A */
    run(vm, other);
    a->ip = vm->with_ret;
    vm->yield = 0;
    return a->ip;
}

static int child_by_id(const FeVm *vm, const FeActor *a, int32_t id)
{
    for (int c = a->child; c >= 0; c = vm->a[c].sib_next) if (FL(&vm->a[c], O_ID) == id) return c;
    return -1;
}

/* FUN_00415385: detach from the parent, keep the world position */
static void detach(FeVm *vm, int i)
{
    FeActor *a = &vm->a[i];
    if (a->parent < 0) return;
    int32_t w[3];
    M43 m, pm;
    actor_matrix(a, m, 1);
    for (int p = a->rparent, g = 0; p >= 0 && g < 64; p = vm->a[p].rparent, g++) { actor_matrix(&vm->a[p], pm, 1); m_mul(m, m, pm); }
    for (int k = 0; k < 3; k++) w[k] = (int32_t)(m[3][k] * 65536000.0);
    FeActor *p = &vm->a[a->parent];
    if (p->child == i) p->child = a->sib_next;
    if (a->sib_next >= 0) vm->a[a->sib_next].sib_prev = a->sib_prev;
    if (a->sib_prev >= 0) vm->a[a->sib_prev].sib_next = a->sib_next;
    a->sib_next = a->sib_prev = a->parent = -1;
    a->rparent = -1;
    a->rworld = 0;
    SF(a, O_X, w[0]); SF(a, O_Y, w[1]); SF(a, O_Z, w[2]);
}

/* orbit about an axis (ops 55/56 Y, AE/AF X, B0/B1 Z): rotate the position, add to the angle */
static void orbit(FeActor *a, int32_t v, int o1, int o2, int orot)
{
    float ang = (float)((double)(float)v * 3.1415926532 / 117964800.0);
    double s = sin(ang), c = cos(ang);
    int32_t p1 = FL(a, o1), p2 = FL(a, o2);
    SF(a, o1, (int32_t)(s * p2 + c * p1));
    SF(a, o2, (int32_t)(-(s * p1) + c * p2));
    SF(a, orot, wrap_ang((int64_t)FL(a, orot) + v));
}

/* ops 25/26 (immediates) and 63/64 (locals) */
static void op_spawn(FeVm *vm, int i, int child, int32_t ofs[6], int scale, int model, int strat,
                     int arg, int world, int32_t id)
{
    FeActor *a = &vm->a[i];
    int32_t p[3], r[3];
    if (child) {
        memcpy(p, ofs, sizeof p); memcpy(r, ofs + 3, sizeof r);
    } else {
        spawn_base(vm, i, p, r);
        for (int k = 0; k < 3; k++) { p[k] += ofs[k]; r[k] += ofs[3 + k]; }
    }
    int n = spawn(vm, p[0], p[1], p[2], r[0], r[1], r[2], scale, model, strat, child ? i : -1);
    if (n < 0) return;
    FeActor *s = &vm->a[n];
    SF(s, O_ARG, arg);
    set_world(s, world);
    if (!child) { SF(s, O_VX, FL(a, O_VX)); SF(s, O_VY, FL(a, O_VY)); SF(s, O_VZ, FL(a, O_VZ)); }
    else SF(s, O_ID, id);
}

static int yield_or_return(FeVm *vm) { return vm->in_trig == 0 ? 1 : 2; }

/* one instruction; returns the next IP */
static uint32_t exec(FeVm *vm, int i)
{
    FeActor *a = &vm->a[i];
    const uint32_t ip = a->ip;
    const int op = rd8(vm, ip);
#define U8(k)   rd8(vm, ip + (k))
#define S16(k)  ((int32_t)rd16(vm, ip + (k)))
#define I16(k)  fx(S16(k))
#define LF(k)   FL(a, U8(k))
#define SL(k,v) SF(a, U8(k), (v))
#define GV(k)   vm->G[U8(k)]
#define JMP(k)  (ip + (uint32_t)S16(k))
    int32_t v, w;
    uint32_t fl = (uint32_t)FL(a, O_FLAGS);

    switch (op) {
    /* ---- flow ---- */
    case 0x00: case 0xac: case 0xad:                                     /* halt */
        vm->yield = yield_or_return(vm);
        return ip;
    case 0x01: vm->yield = 1; return JMP(1);                             /* jmp + yield */
    case 0x38: return JMP(1);
    case 0x1b: push(vm, a, (int32_t)(ip + 3)); return JMP(1);           /* call */
    case 0x1c:                                                           /* ret */
        a->ip = (uint32_t)pop(vm, a);
        if (vm->in_trig != 0) vm->yield = 2;
        return a->ip;
    case 0x71: vm->yield = 1; return ip;                                 /* stop (repeats every tick) */
    case 0x18: push(vm, a, (int32_t)(ip + 2)); push(vm, a, LF(1) >> 16); return ip + 2;
    case 0x19: push(vm, a, (int32_t)(ip + 3)); push(vm, a, S16(1)); return ip + 3;
    case 0x1a: case 0x39: {                                              /* endloop */
        int32_t cnt = pop(vm, a);
        uint32_t back = (uint32_t)pop(vm, a);
        if (cnt - 1 < 1) return ip + 1;
        push(vm, a, (int32_t)back);
        push(vm, a, cnt - 1);
        if (op == 0x1a) vm->yield = 1;
        return back;
    }
    case 0x31: case 0x32: {                                              /* wait */
        int imm = op == 0x32;
        if (!(fl & 0x400)) {
            push(vm, a, imm ? S16(1) : LF(1) >> 16);
            SF(a, O_FLAGS, (int32_t)(fl | 0x400));
            return ip;
        }
        v = pop(vm, a);
        vm->yield = 1;
        if (v - 1 < 1) { SF(a, O_FLAGS, (int32_t)(fl & ~0x400u)); return ip + 2 + imm; }
        push(vm, a, v - 1);
        return ip;
    }
    case 0x80: case 0x81:                                                /* deferred goto / gosub */
        vm->defer_ip = JMP(1);
        vm->defer = op == 0x81 ? 2 : 1;
        return ip + 3;
    case 0x27:                                                           /* die */
        SF(a, O_FLAGS, (int32_t)(fl | 0x1000));
        vm->yield = yield_or_return(vm);
        return ip;
    case 0x33: case 0x34: {                                              /* random jump */
        int imm = op == 0x34;
        int pct = imm ? S16(3) : LF(3) >> 16;
        if (msrand(vm) % 100 < pct) return JMP(1);
        return ip + 4 + imm;
    }
    case 0x7c: case 0x7d: {                                              /* random call */
        int imm = op == 0x7d;
        int pct = imm ? S16(3) : LF(3) >> 16;
        if (msrand(vm) % 100 < pct) { push(vm, a, (int32_t)(ip + 4 + imm)); return JMP(1); }
        return ip + 4 + imm;
    }
    case 0xd0: {                                                         /* call strat[I] + table[L] */
        uint32_t base = strat_addr(vm, S16(1));
        uint32_t tab = ip + (uint32_t)S16(3) + (uint32_t)((LF(5) >> 15) & 0xfffe);
        push(vm, a, (int32_t)(ip + 6));
        return base + (uint32_t)(int32_t)rd16(vm, tab);
    }

    /* ---- variables ---- */
    case 0x08: SL(1, LF(2)); return ip + 3;
    case 0x09: SL(1, I16(2)); return ip + 4;
    case 0x0a: SL(1, GV(2)); return ip + 3;
    case 0x0b: GV(1) = LF(2); return ip + 3;
    case 0x0c: GV(1) = I16(2); return ip + 4;
    case 0x0d: GV(1) = GV(2); return ip + 3;
    case 0x0e: SL(1, LF(1) + LF(2)); return ip + 3;
    case 0x0f: SL(1, LF(1) + I16(2)); return ip + 4;
    case 0x10: SL(1, LF(1) + GV(2)); return ip + 3;
    case 0x11: GV(1) += LF(2); return ip + 3;
    case 0x12: GV(1) += I16(2); return ip + 4;
    case 0x13: GV(1) += GV(2); return ip + 3;
    case 0x73: SL(1, LF(1) - LF(2)); return ip + 3;
    case 0x74: SL(1, LF(1) - I16(2)); return ip + 4;
    case 0x75: SL(1, LF(1) - GV(2)); return ip + 3;
    case 0x76: GV(1) -= LF(2); return ip + 3;
    case 0x77: GV(1) -= I16(2); return ip + 4;
    case 0x78: GV(1) -= GV(2); return ip + 3;
    case 0x3c: SL(1, -LF(1)); return ip + 2;
    case 0x3d: GV(1) = -GV(1); return ip + 2;
    case 0x3e: if (LF(2)) SL(1, fixdiv(LF(1), LF(2))); else vm_err(vm, a, "division by zero"); return ip + 3;
    case 0x3f: if (S16(2)) SL(1, fixdiv(LF(1), I16(2))); else vm_err(vm, a, "division by zero"); return ip + 4;
    case 0x40: if (GV(2)) SL(1, fixdiv(LF(1), GV(2))); else vm_err(vm, a, "division by zero"); return ip + 3;
    case 0x41: if (LF(2)) GV(1) = fixdiv(GV(1), LF(2)); else vm_err(vm, a, "division by zero"); return ip + 3;
    case 0x42: if (S16(2)) GV(1) = fixdiv(GV(1), I16(2)); else vm_err(vm, a, "division by zero"); return ip + 4;
    case 0x43: if (GV(2)) GV(1) = fixdiv(GV(1), GV(2)); else vm_err(vm, a, "division by zero"); return ip + 3;
    case 0x5b: SL(1, fixmul(LF(1), LF(2))); return ip + 3;
    case 0x5c: SL(1, fixmul(LF(1), I16(2))); return ip + 4;
    case 0x5d: SL(1, fixmul(LF(1), GV(2))); return ip + 3;
    case 0x5e: GV(1) = fixmul(GV(1), LF(2)); return ip + 3;
    case 0x5f: GV(1) = fixmul(GV(1), I16(2)); return ip + 4;
    case 0x60: GV(1) = fixmul(GV(1), GV(2)); return ip + 3;
    case 0x15: push(vm, a, LF(1)); return ip + 2;
    case 0x16: push(vm, a, I16(1)); return ip + 3;
    case 0x17: SL(1, pop(vm, a)); return ip + 2;
    case 0x28: {                                                         /* L = rand(I) */
        int n = S16(2);
        SL(1, n ? fx(msrand(vm) % n) : 0);
        return ip + 4;
    }
    case 0xce: {                                                         /* L = word table[IP+R + 2*L] */
        uint32_t ad = ip + (uint32_t)S16(1) + (uint32_t)((LF(3) >> 15) & 0xfffe);
        SL(4, fx(rd16(vm, ad)));
        return ip + 5;
    }
    case 0xcf: {                                                         /* L = word strat[I][L + 2*L] */
        uint32_t ad = strat_addr(vm, S16(1)) + (uint32_t)(LF(3) >> 16) + (uint32_t)((LF(4) >> 15) & 0xfffe);
        SL(5, fx(rd16(vm, ad)));
        return ip + 6;
    }
    case 0xd2: {                                                         /* unpack 4 bytes */
        uint32_t u = (uint32_t)LF(1);
        SL(2, fx((int)(u >> 24))); SL(3, fx((int)(u >> 16 & 0xff)));
        SL(4, fx((int)(u >> 8 & 0xff))); SL(5, fx((int)(u & 0xff)));
        return ip + 6;
    }
    case 0xd3:                                                           /* pack 4 bytes */
        SL(1, (int32_t)((((uint32_t)LF(5) >> 16) & 0xff) | ((((uint32_t)LF(4) >> 16) & 0xff) << 8) |
                        ((((uint32_t)LF(3) >> 16) & 0xff) << 16) | (((uint32_t)LF(2) & 0xffff0000u) << 8)));
        return ip + 6;
    case 0xd4: v = LF(1) >> 16; if (v >= 0 && v < FEVM_NOPT) vm->OPT[v] = LF(2); return ip + 3;
    case 0xd6: v = LF(1) >> 16; if (v >= 0 && v < FEVM_NOPT) SL(2, vm->OPT[v]); return ip + 3;

    /* ---- compare / jump ---- */
    case 0x44: case 0x45: case 0x8e: case 0x8f: case 0x90: case 0x91:   /* == */
    case 0x46: case 0x47: case 0x92: case 0x93: case 0x94: case 0x95:   /* != */
    case 0x4a: case 0x4b: case 0x86: case 0x87: case 0x88: case 0x89:   /* > */
    case 0x4c: case 0x4d: case 0x8a: case 0x8b: case 0x8c: case 0x8d: { /* < */
        int k, imm = 0, cond;
        switch (op) {
        case 0x44: case 0x46: case 0x4a: case 0x4c: v = LF(1); w = LF(2); break;
        case 0x45: case 0x47: case 0x4b: case 0x4d: v = LF(1); w = I16(2); imm = 1; break;
        default:
            k = (op >= 0x8e ? (op - 0x8e) % 4 : (op - 0x86) % 4);
            if (k == 0) { v = LF(1); w = GV(2); }
            else if (k == 1) { v = GV(1); w = LF(2); }
            else if (k == 2) { v = GV(1); w = GV(2); }
            else { v = GV(1); w = I16(2); imm = 1; }
        }
        if (op == 0x44 || op == 0x45 || (op >= 0x8e && op <= 0x91)) cond = v == w;
        else if (op == 0x46 || op == 0x47 || op >= 0x92) cond = v != w;
        else if (op == 0x4a || op == 0x4b || (op >= 0x86 && op <= 0x89)) cond = v > w;
        else cond = v < w;
        return cond ? JMP(3 + imm) : ip + 5 + imm;
    }
    case 0x48: case 0x49: {                                              /* range test */
        v = LF(1);
        int in = !(v < I16(2) || I16(4) < v);
        return (op == 0x48 ? in : !in) ? JMP(6) : ip + 8;
    }
    case 0xaa: v = S16(1); return ((int32_t)fl & v) == v ? JMP(3) : ip + 5;
    case 0xab: v = S16(1); return ((int32_t)fl & v) == 0 ? JMP(3) : ip + 5;
    case 0xca: return a->hit >= 0 ? JMP(1) : ip + 3;
    case 0xb4: case 0xb5:                                                /* if no child with id */
        return child_by_id(vm, a, op == 0xb5 ? fx(U8(1)) : LF(1)) < 0 ? JMP(2) : ip + 4;
    case 0xb9:                                                           /* if target dead */
        if (actor_alive(vm, a->target)) return ip + 3;
        a->target = -1;
        return JMP(1);
    case 0x59: case 0x5a: {                                              /* if distance to focus < */
        int imm = op == 0x5a;
        if (vm->focus < 0) return ip + 4 + imm;
        int32_t lim = imm ? I16(1) : LF(1);
        const FeActor *f = &vm->a[vm->focus];
        double dx = (FL(a, O_X) - FL(f, O_X)) >> 16, dy = (FL(a, O_Y) - FL(f, O_Y)) >> 16, dz = (FL(a, O_Z) - FL(f, O_Z)) >> 16;
        int32_t d = (int32_t)sqrt(dx * dx + dy * dy + dz * dz);
        return d < lim ? JMP(2 + imm) : ip + 4 + imm;
    }
    case 0xb6: case 0xb7: case 0xbb: case 0xbc: case 0xc2: case 0xc3: { /* if not facing focus */
        int axes = (op == 0xb6 || op == 0xb7) ? 3 : (op == 0xbb || op == 0xbc) ? 1 : 2;
        int32_t lim = (op == 0xb6 || op == 0xbb || op == 0xc2) ? LF(1) : fx(U8(1));
        if (vm->focus < 0) return ip + 4;
        int32_t pos[3], rot[3];
        const FeActor *f = &vm->a[vm->focus];
        spawn_base(vm, i, pos, rot);
        double dx = pos[0] - FL(f, O_X), dy = pos[1] - FL(f, O_Y), dz = pos[2] - FL(f, O_Z);
        for (int k = 0; k < 2; k++) {
            if (!(axes & (k == 0 ? 2 : 1))) continue;
            double ang = k == 0 ? atan2(-dy, sqrt(dx * dx + dz * dz)) : atan2(dx, dz);
            int64_t d = ((int64_t)(ang * RAD2ANG) - rot[k]) % FULLTURN;
            if (d > 0x7080000) d -= FULLTURN;
            if (d < -0x7080000) d += FULLTURN;
            if ((d < 0 ? -d : d) > lim) return JMP(2);
        }
        return ip + 4;
    }

    /* ---- transform ---- */
    case 0x02: SF(a, O_RX, wrap_ang((int64_t)FL(a, O_RX) + LF(1))); return ip + 2;
    case 0x03: SF(a, O_RX, wrap_ang((int64_t)FL(a, O_RX) + I16(1))); return ip + 3;
    case 0x04: AF(a, O_RY, LF(1)); return ip + 2;
    case 0x05: AF(a, O_RY, I16(1)); return ip + 3;
    case 0x06: AF(a, O_RZ, LF(1)); return ip + 2;
    case 0x07: AF(a, O_RZ, I16(1)); return ip + 3;
    case 0x2b: AF(a, O_X, LF(1)); return ip + 2;
    case 0x2c: AF(a, O_X, I16(1)); return ip + 3;
    case 0x2d: AF(a, O_Y, LF(1)); return ip + 2;
    case 0x2e: AF(a, O_Y, I16(1)); return ip + 3;
    case 0x2f: AF(a, O_Z, LF(1)); return ip + 2;
    case 0x30: AF(a, O_Z, I16(1)); return ip + 3;
    case 0x65: SF(a, O_X, LF(1)); return ip + 2;
    case 0x66: SF(a, O_X, I16(1)); return ip + 3;
    case 0x67: SF(a, O_Y, LF(1)); return ip + 2;
    case 0x68: SF(a, O_Y, I16(1)); return ip + 3;
    case 0x69: SF(a, O_Z, LF(1)); return ip + 2;
    case 0x6a: SF(a, O_Z, I16(1)); return ip + 3;
    case 0x6b: SF(a, O_RX, LF(1)); return ip + 2;
    case 0x6c: SF(a, O_RX, I16(1)); return ip + 3;
    case 0x6d: SF(a, O_RY, LF(1)); return ip + 2;
    case 0x6e: SF(a, O_RY, I16(1)); return ip + 3;
    case 0x6f: SF(a, O_RZ, LF(1)); return ip + 2;
    case 0x70: SF(a, O_RZ, I16(1)); return ip + 3;
    case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: { /* pos/rot = G[raw L] */
        static const int dst[6] = { O_X, O_Y, O_Z, O_RX, O_RY, O_RZ };
        v = LF(1);                                                       /* raw value used as index */
        if (v >= 0 && v < FEVM_NG) SF(a, dst[op - 0x99], vm->G[v]); else vm_err(vm, a, "G index");
        return ip + 2;
    }
    case 0x84: SF(a, O_SX, LF(1)); SF(a, O_SY, LF(2)); SF(a, O_SZ, LF(3)); return ip + 4;
    case 0x85: SF(a, O_SX, I16(1)); SF(a, O_SY, I16(3)); SF(a, O_SZ, I16(5)); return ip + 7;
    case 0x4e: case 0x50: case 0x4f: case 0x51: {                       /* approach: halve the distance */
        int imm = op == 0x4f || op == 0x51;
        int64_t d64 = (int64_t)LF(1) - (imm ? I16(2) : LF(2));
        int32_t d = (int32_t)d64;
        if (iabs32(d) > 0x8000) d >>= 1;
        SL(1, LF(1) - d);
        if ((op == 0x50 || op == 0x51) && d != 0) { vm->yield = 1; return ip; }
        return ip + 3 + imm;
    }
    case 0x1d: SF(a, O_SPD, LF(1)); SF(a, O_FLAGS, (int32_t)(fl | 0x8000)); return ip + 2;
    case 0x1e: SF(a, O_SPD, I16(1)); SF(a, O_FLAGS, (int32_t)(fl | 0x8000)); return ip + 3;
    case 0x1f: SF(a, O_TSPD, LF(1)); SF(a, O_ACC, LF(2)); SF(a, O_FLAGS, (int32_t)(fl | 0x8000)); return ip + 3;
    case 0x20: SF(a, O_TSPD, I16(1)); SF(a, O_ACC, I16(3)); SF(a, O_FLAGS, (int32_t)(fl | 0x8000)); return ip + 5;
    case 0x21: SF(a, O_FLAGS, (int32_t)(fl | (uint32_t)S16(1))); return ip + 3;
    case 0x22: SF(a, O_FLAGS, (int32_t)(fl & ~(uint32_t)S16(1))); return ip + 3;
    case 0x52: case 0x72: {                                              /* move to (rel) with speed */
        int r = op == 0x72;
        if (!(fl & 0x400)) {
            push(vm, a, (r ? LF(1) : I16(1)) + FL(a, O_X));
            push(vm, a, (r ? LF(2) : I16(3)) + FL(a, O_Y));
            push(vm, a, (r ? LF(3) : I16(5)) + FL(a, O_Z));
            SF(a, O_FLAGS, (int32_t)(fl | 0x400));
            vm->yield = 1;
            return ip;
        }
        int32_t tz = pop(vm, a), ty = pop(vm, a), tx = pop(vm, a);
        if ((fl & 0x100) && vm->focus >= 0) tz += FL(&vm->a[vm->focus], O_VZ);
        int32_t spd = r ? LF(4) : I16(7);
        double dx = (FL(a, O_X) - tx) >> 16, dy = (FL(a, O_Y) - ty) >> 16, dz = (FL(a, O_Z) - tz) >> 16;
        if (sqrt(dx * dx + dy * dy + dz * dz) * 65536.0 <= spd / 20) {
            SF(a, O_SPD, 0);
            SF(a, O_FLAGS, (int32_t)((fl | 0x8000) & ~0x400u));
            return ip + (r ? 5 : 9);
        }
        int32_t turned = turn_to(vm, i, tx, ty, tz, spd / 25, 3);
        if (turned < 0x10001 || FL(a, O_SPD) != spd) { if (turned < 0x10000) SF(a, O_SPD, spd); }
        else SF(a, O_FLAGS, (int32_t)((uint32_t)FL(a, O_FLAGS) | 0x8000));
        push(vm, a, tx); push(vm, a, ty); push(vm, a, tz);
        vm->yield = 1;
        return ip;
    }
    case 0x54: case 0x7e: {                                              /* wobble */
        int n = (op == 0x7e ? U8(1) : LF(1) >> 16) + 1;
        if (n != 0 && ((FL(a, O_SER) + FL(a, O_TICK)) % n) == 0) {
            uint32_t u = (uint32_t)((FL(a, O_WOB) >> 16) + 2) & 0x3e;
            AF(a, O_X, (int32_t)exe_u32(&vm->fe->exe, WOBBLE_TAB + u * 4));
            SF(a, O_RZ, wrap_ang((int64_t)(int32_t)exe_u32(&vm->fe->exe, WOBBLE_TAB + 4 + u * 4) + FL(a, O_RZ)));
            SF(a, O_WOB, (int32_t)(u << 16));
        }
        return ip + 2;
    }
    case 0x55: case 0x56: orbit(a, op == 0x56 ? I16(1) : LF(1), O_X, O_Z, O_RY); return ip + 2 + (op == 0x56);
    case 0xae: case 0xaf: orbit(a, op == 0xaf ? I16(1) : LF(1), O_Y, O_Z, O_RX); return ip + 2 + (op == 0xaf);
    case 0xb0: case 0xb1: orbit(a, op == 0xb1 ? I16(1) : LF(1), O_X, O_Y, O_RZ); return ip + 2 + (op == 0xb1);
    case 0x35: case 0x36: case 0x3a: case 0x57: case 0xc0: case 0xc1: { /* turn to focus */
        if (vm->focus < 0) return ip + 2;
        int axes = (op == 0x36 || op == 0x57) ? 1 : (op == 0xc0 || op == 0xc1) ? 2 : 3;
        const FeActor *f = &vm->a[vm->focus];
        int32_t t = turn_to(vm, i, FL(f, O_X), FL(f, O_Y), FL(f, O_Z), fx(U8(1)), axes);
        if ((op == 0x3a || op == 0x57) && t > 0x10000) { vm->yield = 1; return ip; }
        return ip + 2;
    }
    case 0xa5: case 0xa6: case 0xa7: case 0xa8: case 0xc4: case 0xc5: { /* turn to target */
        if (!actor_alive(vm, a->target)) return ip + 2;
        int axes = (op == 0xa6 || op == 0xa8) ? 1 : (op == 0xc4 || op == 0xc5) ? 2 : 3;
        int32_t p[3];
        const FeActor *t = &vm->a[a->target];
        if (t->parent < 0) { p[0] = FL(t, O_X); p[1] = FL(t, O_Y); p[2] = FL(t, O_Z); }
        else world_pos(vm, a->target, p);
        int32_t turned = turn_to(vm, i, p[0], p[1], p[2], fx(U8(1)), axes);
        if ((op == 0xa7 || op == 0xa8) && turned != 0) { vm->yield = 1; return ip; }
        return ip + 2;
    }

    /* ---- objects ---- */
    case 0x25: case 0x26: {                                              /* spawn (immediates) */
        int32_t o[6];
        for (int k = 0; k < 6; k++) o[k] = I16(1 + 2 * k);
        op_spawn(vm, i, op == 0x26, o, S16(13), U8(17), S16(15), fx(U8(18)), U8(19), fx(U8(20)));
        return ip + 0x14 + (op == 0x26);
    }
    case 0x63: case 0x64: {                                              /* spawn (locals) */
        int32_t o[6];
        for (int k = 0; k < 6; k++) o[k] = LF(1 + k);
        op_spawn(vm, i, op == 0x64, o, LF(7) >> 16, (int)(((uint32_t)LF(9) >> 16) & 0xff), LF(8) >> 16,
                 fx(U8(10)), U8(11), op == 0x64 ? LF(12) : 0);
        return ip + 0xc + (op == 0x64);
    }
    case 0x37: {                                                         /* spawn here (no parent) */
        int32_t p[3], r[3];
        spawn_base(vm, i, p, NULL);
        world_rot(vm, i, r);
        int n = spawn(vm, p[0], p[1], p[2], r[0], r[1], r[2], 0x100, U8(6), S16(3), -1);
        if (n >= 0) {
            FeActor *s = &vm->a[n];
            SF(s, O_ARG, 1);                                             /* raw 1, as in the original */
            SF(s, O_WORLD, fx(U8(5)));                                   /* no re-parenting here */
            SF(s, O_VX, FL(a, O_VX)); SF(s, O_VY, FL(a, O_VY)); SF(s, O_VZ, FL(a, O_VZ));
        }
        return ip + 7;
    }
    case 0x79: case 0x9f: {                                              /* with child id { */
        int c = child_by_id(vm, a, op == 0x9f ? fx(U8(1)) : LF(1));
        if (c < 0) return ip + 2;
        return with_run(vm, a, c, ip + 2);
    }
    case 0x7b:                                                           /* with last spawned { */
        if (!actor_alive(vm, vm->last)) return ip + 1;
        return with_run(vm, a, vm->last, ip + 1);
    case 0xa3:                                                           /* with hit { */
        if (!actor_alive(vm, a->hit)) return ip + 1;
        return with_run(vm, a, a->hit, ip + 1);
    case 0xa4:                                                           /* with target { */
        if (!actor_alive(vm, a->target)) return ip + 1;
        return with_run(vm, a, a->target, ip + 1);
    case 0x7a:                                                           /* } endwith */
        vm->with_ret = ip + 1;
        a->ip = vm->with_save;
        vm->yield = 2;
        return a->ip;
    case 0x29: vm->focus = i; return ip + 1;
    case 0x2a: return ip + 1;                                            /* debug chase camera */
    case 0x61: a->rstyle = 1; return ip + 1;                             /* hide */
    case 0x62: a->rstyle = 4; return ip + 1;                             /* show */
    case 0x98: case 0xbf:                                                /* set model type */
        v = op == 0x98 ? LF(1) >> 16 : U8(1);
        SF(a, O_TYPE, fx(v));
        if (a->brtype == 1) a->model = v & 0xff; else vm_err(vm, a, "object is not a model");
        return ip + 2;
    case 0xa0: case 0xa1: {                                              /* detach children with id */
        int32_t id = op == 0xa1 ? fx(U8(1)) : LF(1);
        for (int c = a->child; c >= 0;) {
            int nx = vm->a[c].sib_next;
            if (FL(&vm->a[c], O_ID) == id) detach(vm, c);
            c = nx;
        }
        return ip + 2;
    }
    case 0xa2: detach(vm, i); return ip + 1;
    case 0xa9: while (a->child >= 0) detach(vm, a->child); return ip + 1;
    case 0xb8: a->target = a->hit; return ip + 1;
    case 0xd1: a->target = vm->last; return ip + 1;
    case 0xba: {                                                         /* target = nearest in front */
        int best = -1;
        int64_t bz = -0x4e200000LL;
        for (int k = vm->head; k >= 0; k = vm->a[k].next) {
            int32_t p[3];
            if (vm->a[k].parent < 0) p[2] = FL(&vm->a[k], O_Z); else world_pos(vm, k, p);
            int64_t dz = (int64_t)p[2] - FL(a, O_Z);
            if ((FL(&vm->a[k], O_CMASK) & FL(a, O_COL)) && dz < -0x3e80000 && bz < dz) { bz = dz; best = k; }
        }
        if (best >= 0) a->target = best;
        return ip + 1;
    }
    case 0x23: case 0x24: case 0xb2: case 0xb3: {                       /* triggers */
        int type = U8(1), arm = op >= 0xb2;
        uint32_t tgt = JMP(2);
        int len = (op == 0x23 || op == 0xb3) ? 6 : type == 15 ? 5 : 4;
        int t;
        if (!arm && type == 0) {                                         /* remove */
            t = trig_find(vm, a, tgt);
            if (t < 0) vm_err(vm, a, "tried to free undefined trigger");
            else {
                if (t == vm->trig_next) vm->trig_next = vm->t[t].next;
                if (t == vm->trig_cur) vm->in_trig = -1;
                else trig_free(vm, a, t);
            }
            return ip + 4;
        }
        t = arm ? trig_find(vm, a, tgt) : trig_new(vm, a);
        if (t < 0) {
            vm_err(vm, a, arm ? "tried to reset undefined trigger" : "no memory for trigger");
            return ip + len;
        }
        FeTrig *r = &vm->t[t];
        r->type = type;
        r->target = tgt;
        if (op == 0x23 || op == 0xb3) r->count = S16(4);
        else if (type == 15) r->key = U8(4);
        else r->count = 0;
        return ip + len;
    }

    /* ---- system ---- */
    case 0x58: sound(vm, S16(1)); return ip + 3;
    case 0xd5: sound(vm, LF(1) >> 16); return ip + 2;
    case 0xe6: sound(vm, S16(1)); return ip + 5;                          /* + FUN_00410047 (unused) */
    case 0xe7: return ip + 3;                                            /* FUN_0041027e (unused) */
    case 0x96: return ip + 2;                                            /* make camera: fixed in the renderer */
    case 0x97: {                                                         /* material FRONTx colour map */
        int face = LF(1) >> 16, map = LF(2) >> 16, mode = LF(3) >> 16;
        if (face >= 0 && face < 4) { vm->bg[face].map = map; vm->bg[face].mode = mode; vm->bg[face].dirty = 1; }
        return ip + 4;
    }
    case 0xd7: {                                                         /* START_GAME */
        int m = LF(1) >> 16;
        uint32_t b = (uint32_t)LF(2), c = (uint32_t)LF(3), d = (uint32_t)LF(4);
        vm->mode = m;
        switch (m) {
        case 0: case 5:
            vm->p1 = (int32_t)(b >> 16 & 0xff); vm->p2 = (int32_t)(c >> 16 & 0xff); vm->arena = (int32_t)(d >> 16 & 0xff);
            vm->hc1 = vm->hc2 = 4; vm->music = -2; break;
        case 1:
            vm->p1 = (int32_t)(b >> 16 & 0xff); vm->p2 = (int32_t)(c >> 16 & 0xff); vm->arena = vm->p2;
            vm->hc1 = vm->hc2 = 4; vm->music = -2; break;
        case 2:
            vm->p1 = (int32_t)(b >> 16 & 0xff); vm->p2 = (int32_t)(c >> 16 & 0xff); vm->arena = (int32_t)(d >> 16 & 0xff);
            vm->hc1 = (LF(5) >> 16) & 0xff; vm->cfg[0] = (uint8_t)((LF(5) >> 16) - 1);
            vm->hc2 = (LF(6) >> 16) & 0xff; vm->cfg[1] = (uint8_t)((LF(6) >> 16) - 1);
            vm->music = -2; break;
        case 3:
            vm->p1 = (int32_t)b; vm->p2 = (int32_t)c; vm->arena = (int32_t)(d >> 16 & 0xff);
            vm->hc1 = vm->hc2 = 4; vm->music = -2; break;
        case 4:
            vm->p1 = (int32_t)b; vm->p2 = (int32_t)c; vm->arena = vm->p2;
            vm->hc1 = vm->hc2 = 4; vm->music = -2; break;
        }
        sys_call(vm, 0xd7);
        return ip + 7;
    }
    case 0xd8: sys_call(vm, 0xd8); return ip + 1;                         /* load fight.prf (host) */
    case 0xd9: fe_vm_g_to_cfg(vm); sys_call(vm, 0xd9); return ip + 1;      /* G -> cfg, save */
    case 0xda: fe_vm_g_to_cfg(vm); return ip + 1;
    case 0xdb: fe_vm_cfg_to_g(vm); return ip + 1;
    case 0xdc: fe_vm_cfg_defaults(vm); sys_call(vm, 0xdc); return ip + 1;
    case 0xdd: sys_call(vm, 0xdd); return ip + 1;                         /* save fight.prf */
    case 0xde: sys_call(vm, 0xde); return ip + 1;                         /* high score names */
    case 0xdf: vm->cfg[C_SVOL] = (uint8_t)(vm->G[48] >> 16); sys_call(vm, 0xdf); return ip + 1;
    case 0xe0: vm->cfg[C_CDVOL] = (uint8_t)(vm->G[49] >> 16); sys_call(vm, 0xe0); return ip + 1;
    case 0xe1: SL(1, 0); return ip + 2;                                   /* network available: no */
    case 0xe2: SL(1, fx(-1)); return ip + 2;                              /* network message: error */
    case 0xe3: return ip + 2;
    case 0xe8: sys_call(vm, 0xe8); return ip + 1;
    case 0xe9: vm->music = 2; sys_call(vm, 0xe9); return ip + 1;
    case 0xea: vm->music = -2; sys_call(vm, 0xea); return ip + 1;
    case 0xec: vm->cfg[C_SPEED] = (uint8_t)((vm->G[51] >> 16) + 1); sys_call(vm, 0xec); return ip + 1;
    case 0xed: sys_call(vm, 0xed); return ip + 1;                         /* ladder continue (G67): host */
    case 0xe4: {                                                         /* read a raw key */
        int k, excl = vm->cfg[C_EXKEY];
        for (k = 1; k < 255; k++) if (k != excl && raw_edge(vm, k)) break;
        if (k == 255) {
            /* the scripts busy-wait on this (strats 32/39 ...) and the original blocks until a key
             * arrives; here: yield and retry the op next tick */
            vm->yield = 1;
            return ip;
        }
        SL(1, fx(k));
        SL(2, fx(k ? rd8(vm, KEYMODEL + 2 * (uint32_t)k) : 0));
        return ip + 3;
    }
    case 0xe5: v = LF(1) >> 16; SL(2, fx(rd8(vm, KEYMODEL + 2 * (uint32_t)v))); return ip + 3;
    case 0xeb: {                                                         /* key not bound yet */
        int fr = 1;
        for (int k = 36; k <= 47; k++) if (vm->G[k] == LF(1)) fr = 0;
        SL(2, fx(fr));
        return ip + 3;
    }

    /* ---- debug / 2D text / no-ops ---- */
    case 0x14: case 0x3b:
        if (vm->debug) {
            char s[128]; size_t n = rdlen(vm, ip + 2);
            for (size_t k = 0; k < n && k < sizeof s - 1; k++) s[k] = (char)rd8(vm, ip + 2 + (uint32_t)k);
            s[n < sizeof s - 1 ? n : sizeof s - 1] = 0;
            fprintf(stderr, "fevm: [%d] %s %g\n", a->strat, s, LF(1) / 65536.0);
        }
        return ip + (uint32_t)rdlen(vm, ip + 2) + 3;
    case 0x7f: return ip + (uint32_t)rdlen(vm, ip + 1) + 2;
    case 0xcb: case 0xcc: return ip + 2;                                 /* font / colour of 2D text */
    case 0xcd: return ip + (uint32_t)rdlen(vm, ip + 1) + 6;              /* 2D text (buggy, unused) */
    case 0x53: return ip + 3;
    case 0xbd: return ip + 4;                                            /* material tweak (unused) */
    case 0xbe: return ip + 7;
    case 0xc6: return ip + 2;
    case 0xc7: return ip + 3;
    case 0xc8: return ip + 4;
    case 0xc9: return ip + 7;
    }
    /* 0x82/0x83 hang the original; anything above 0xED is outside the table */
    vm_err(vm, a, "bad opcode");
    vm->yield = yield_or_return(vm);
    return ip;
#undef U8
#undef S16
#undef I16
#undef LF
#undef SL
#undef GV
#undef JMP
}

static void run(FeVm *vm, int i)                                    /* FUN_0040c28c */
{
    FeActor *a = &vm->a[i];
    vm->yield = 0;
    int guard = 0;
    while (vm->yield == 0) {
        if (++guard > 20000) { vm_err(vm, a, "runaway script"); vm->yield = 1; break; }
        a->ip = exec(vm, i);
    }
    if (vm->defer) {
        SF(a, O_FLAGS, (int32_t)((uint32_t)FL(a, O_FLAGS) & ~0x400u));
        a->sp = 0;
        if (vm->defer == 2) push(vm, a, (int32_t)a->ip);
        a->ip = vm->defer_ip;
        vm->defer = 0;
    }
    if (vm->yield == 1 && a->live) {
        update(vm, i);
        triggers(vm, i);
        vm->trig_next = vm->trig_cur = -1;
        if (a->live && ((uint32_t)FL(a, O_FLAGS) & 0x1000)) kill(vm, i);
    }
}

/* ------------------------------------------------------------------ */
/*  public API                                                         */
/* ------------------------------------------------------------------ */
void fe_vm_seed(FeVm *vm, uint32_t seed) { vm->rng = seed; }

void fe_vm_tick(FeVm *vm)                                           /* FUN_00404902 */
{
    for (int k = 0; k < FEVM_MAX_ACTORS; k++) {
        vm->has_prev[k] = (uint8_t)vm->a[k].live;
        if (vm->a[k].live) memcpy(vm->prev[k], &vm->a[k].f[O_RX / 4], sizeof vm->prev[k]);
    }
    int i = vm->head;
    while (i >= 0) {
        vm->iter_next = vm->a[i].next;
        run(vm, i);
        i = vm->iter_next;
    }
    vm->frame++;
}

void fe_vm_restart(FeVm *vm, int entry)
{
    while (vm->head >= 0) kill(vm, vm->head);
    memset(vm->t, 0, sizeof vm->t);
    vm->focus = vm->last = -1;
    vm->yield = vm->in_trig = vm->defer = 0;
    vm->iter_next = vm->trig_cur = vm->trig_next = -1;
    vm->mode = -1;
    vm->nsnd = 0;
    vm->frame = 0;
    vm->G[58] = fx(entry);
    fe_vm_cfg_to_g(vm);                                              /* FUN_004135f1 on entry */
    vm->focus = spawn(vm, 0, 0, 0, 0, 0, 0, 0x100, 0xff, 0, -1);     /* FUN_00412a7a */
}

int fe_vm_init(FeVm *vm, const FeAssets *fe)
{
    memset(vm, 0, sizeof *vm);
    vm->fe = fe;
    vm->head = -1;
    vm->rng = 1;
    if (!exe_ptr(&fe->exe, STRAT_TAB, 4)) return 0;
    for (int k = 0; k < 4; k++) vm->round_time[k] = (int32_t)exe_u32(&fe->exe, RTIME_TAB + 4 * k);
    for (int k = 0; k < FEVM_CFG; k++) vm->cfg[k] = rd8(vm, CFG_VA + (uint32_t)k);
    if (!vm->cfg[C_SPEED]) fe_vm_cfg_defaults(vm);                   /* blank block: use defaults */
    fe_vm_restart(vm, 0);
    return vm->head >= 0;
}

int fe_vm_pop_sound(FeVm *vm)
{
    if (vm->nsnd <= 0) return -1;
    int s = vm->snd[0];
    memmove(vm->snd, vm->snd + 1, (size_t)(vm->nsnd - 1) * sizeof vm->snd[0]);
    vm->nsnd--;
    return s;
}

int fe_vm_live(const FeVm *vm)
{
    int n = 0;
    for (int i = vm->head; i >= 0; i = vm->a[i].next) n++;
    return n;
}

void fe_vm_scene(const FeVm *vm, FeScene *sc) { fe_vm_scene_lerp(vm, sc, 1.0f); }

#define LERP_MAX_ROT (1200 << 16)   /* 120 degrees per tick */
#define LERP_MAX_POS (400 << 16)    /* 0.4 world units per tick */

static double lerp_field(const FeVm *vm, int i, int k, float t)
{
    int32_t cur = vm->a[i].f[O_RX / 4 + k];
    if (t >= 1.0f || !vm->has_prev[i]) return cur;
    int64_t d = (int64_t)cur - vm->prev[i][k];
    if (k < 3) {                                    /* angle: the short way round */
        d %= FULLTURN;
        if (d > FULLTURN / 2) d -= FULLTURN;
        if (d < -FULLTURN / 2) d += FULLTURN;
        if (d > LERP_MAX_ROT || d < -LERP_MAX_ROT) return cur;
    } else if (k < 6 && (d > LERP_MAX_POS || d < -LERP_MAX_POS)) return cur;
    return (double)cur - (double)d * (1.0 - t);
}

void fe_vm_scene_lerp(const FeVm *vm, FeScene *sc, float t)
{
    static int node_of[FEVM_MAX_ACTORS];
    sc->count = 0;
    for (int i = 0; i < FEVM_MAX_ACTORS; i++) {
        node_of[i] = -1;
        if (!vm->a[i].live || sc->count >= FE_MAX_NODES) continue;
        node_of[i] = sc->count++;
    }
    for (int i = 0; i < FEVM_MAX_ACTORS; i++) {
        if (node_of[i] < 0) continue;
        const FeActor *a = &vm->a[i];
        FeNode *n = &sc->n[node_of[i]];
        n->type = (a->brtype == 1 && a->model < FE_NUM_TYPES) ? a->model : -1;
        for (int k = 0; k < 3; k++) {
            n->rot[k] = (float)(lerp_field(vm, i, k, t) / 655360.0);
            n->pos[k] = (float)(lerp_field(vm, i, 3 + k, t) / 65536000.0);
            n->scale[k] = (float)(lerp_field(vm, i, 6 + k, t) / 16777216.0);   /* y/z may differ (strat 91) */
        }
        n->parent = a->rparent >= 0 ? node_of[a->rparent] : -1;
        n->world = a->rworld;
        n->hidden = a->rstyle == 1;
    }
}
