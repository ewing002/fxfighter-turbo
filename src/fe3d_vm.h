/*
 * FX Fighter Turbo - open reimplementation
 * 3D front end: the "strat" script VM (FUN_0040c28c and friends in FIGHT.EXE), see docs/FRONTEND.md.
 * Runs the original bytecode read from the user's FIGHT.EXE (FeAssets.exe). No SDL/GL here.
 *
 * Usage: fe_vm_init() spawns the root strat; per rendered frame the host feeds input
 * (fe_vm_set_input / fe_vm_key_*), calls fe_vm_tick() N times (N = elapsed / 66.7 ms, at least 1),
 * builds the scene with fe_vm_scene() and reads the outputs (mode, sounds, music, bg, cfg).
 * When `mode` becomes >= 0 the original leaves the front end; modes 7/8/9 re-enter it with
 * fe_vm_restart(vm, 0/8/9) (G58 = entry type), keeping G[] and OPT[].
 */
#ifndef FX_FE3D_VM_H
#define FX_FE3D_VM_H

#include <stdint.h>
#include "fe3d.h"
#include "fe3d_gl.h"   /* FeScene / FeNode only */

#define FEVM_MAX_ACTORS 512
#define FEVM_MAX_TRIGS  2048
#define FEVM_STACK      64
#define FEVM_NG         256      /* G[] = DAT_004f3e20, byte index */
#define FEVM_NOPT       128      /* OPT[] = DAT_004f3870 */
#define FEVM_NSND       32
#define FEVM_CFG        0x2a     /* config block DAT_004a5878 (fight.prf) */
/* byte offsets in the config block: handicaps (4), CPU level (0x4a587c), round time (int16 seconds,
 * -1 = none), rounds to win, 12 key scan codes, game speed, sound / CD volume (1..8) */
enum { FECFG_HC = 0, FECFG_DIFF = 4, FECFG_RTIME = 6, FECFG_ROUNDS = 8, FECFG_DET = 0x0b, FECFG_KEYS = 0x12,
       FECFG_SPEED = 0x21, FECFG_SVOL = 0x22, FECFG_CDVOL = 0x23 };

/* trigger record (0x18 bytes in the original) */
typedef struct {
    int      used;
    int      type, count, key;
    uint32_t target;             /* bytecode address of the trigger body */
    int      next, prev;         /* per-actor list, -1 = end */
} FeTrig;

/* actor = the original 0x108-byte strat block */
typedef struct {
    int32_t  f[0x108 / 4];       /* raw block: bytecode field operands address it by byte offset
                                  * (0x10 vel, 0x1c accel, 0x20 speed, 0x24 tspeed, 0x28 child id,
                                  * 0x2c serial, 0x30 rot, 0x3c pos, 0x48 scale, 0x58 colmask,
                                  * 0x74..0xd0 locals, 0xd4 type<<16, 0xd8 ticks, 0xdc flags,
                                  * 0xe0 arg, 0xe4 world); all values 16.16 */
    int      live;
    int      next, prev;         /* global list DAT_0049ff04 (head insert) */
    int      parent, child, sib_next, sib_prev;   /* 0xf4 / 0xe8 / 0xec / 0xf0 */
    int      target, hit;        /* 0x0c / 0x70 */
    int      trig;               /* 0x100: first trigger */
    uint32_t ip;                 /* 0xf8 */
    int      sp;                 /* 0xfc: value stack */
    int32_t  stack[FEVM_STACK];
    /* BRender actor (+0x08) */
    int      brtype;             /* 0 empty (0xff), 1 model, 2 camera (0xfe) */
    int      model;              /* model type of a model actor */
    int      rstyle;             /* render style: 0 default, 1 none (op 0x61), 4 faces (op 0x62) */
    int      rworld;             /* world the BRender actor lives in: 0 main, 1 right, 2 left */
    int      rparent;            /* BRender parent actor, -1 = world root */
    int      strat;              /* strat number it was spawned with (debug) */
} FeActor;

typedef struct FeVm FeVm;
/* optional host hook for the system ops, called after the VM did its in-memory part:
 * 0xD8 load cfg from fight.prf, 0xD9/0xDD save cfg to fight.prf, 0xDC defaults applied,
 * 0xDE store high score names (OPT[0..19]), 0xDF/0xE0 sound/CD volume changed (cfg),
 * 0xE8 close network, 0xE9/0xEA music play/stop, 0xEC game speed changed, 0xED ladder
 * continue (G67), 0xD7 START_GAME (mode etc. already set) */
typedef void (*FeVmSysFn)(FeVm *vm, int op, void *user);

struct FeVm {
    const FeAssets *fe;
    FeActor  a[FEVM_MAX_ACTORS];
    FeTrig   t[FEVM_MAX_TRIGS];
    int      head;               /* first live actor, -1 = none */
    int32_t  G[FEVM_NG];
    int32_t  OPT[FEVM_NOPT];
    int      focus;              /* DAT_0049ff00 */
    int      last;               /* DAT_004f3a10, last spawned */
    uint32_t frame;              /* DAT_004f299c, "every N frames" triggers */
    int32_t  serial;             /* DAT_004f4218 */
    uint32_t rng;                /* MSVC rand() state */
    /* interpreter state (globals of the original) */
    int      yield;              /* DAT_004f3a04: 0 run, 1 yield (physics + triggers), 2 return */
    int      in_trig;            /* DAT_004f3a14: 1 inside a trigger body, -1 = remove it after */
    uint32_t with_save, with_ret;/* DAT_004f3a08 / DAT_004f4210 */
    int      defer;              /* DAT_0049ff08: 1 goto, 2 gosub (ops 0x80/0x81) */
    uint32_t defer_ip;           /* DAT_004f3a0c */
    int      iter_next;          /* DAT_004f420c */
    int      trig_cur, trig_next;/* DAT_004f3868 / DAT_004f386c */
    /* input: key states like DAT_004f15c0 (pads) and DAT_004f1c60 (keyboard) */
    uint8_t  pad[2][6];
    uint8_t  keys[256];
    /* outputs */
    int      mode;               /* DAT_004a1df0, -1 = stay in the front end */
    int32_t  p1, p2, arena;      /* DAT_004f2998 / 2994 / 29f4 (modes 3/4: raw 16.16 values) */
    int      hc1, hc2;           /* DAT_004f29a4 / 29a0 handicaps */
    int      snd[FEVM_NSND], nsnd; /* sound requests, bank "frontend" */
    int      music;              /* last music request: 2 play (op E9), -2 stop (EA, D7), 0 none */
    struct { int map, mode, dirty; } bg[4]; /* op 0x97: material FRONTA..D gets colour map
                                  * map (0 FRONTBACKUP, 1..11 arenaN), transform mode */
    uint8_t  cfg[FEVM_CFG];      /* config block; G20..G57 <-> cfg via fe_vm_cfg_to_g/g_to_cfg */
    int32_t  round_time[4];      /* DAT_004a6fe0 */
    FeVmSysFn sys; void *sys_user;
    /* transforms (rot, pos, scale: fields 0x30..0x50) before the last tick, for drawing between
     * ticks (fe_vm_scene_lerp); has_prev = 0 for actors spawned in the last tick */
    int32_t  prev[FEVM_MAX_ACTORS][9];
    uint8_t  has_prev[FEVM_MAX_ACTORS];
    int      raw_wait;           /* op E4 is waiting for a key (see there) */
    int      debug;              /* 1: print debug strings and errors to stderr */
    int      errors;             /* count of script errors (bad ops, stack, pool full ...) */
};

/* returns 1 on success; G[]/OPT[] cleared, cfg from the EXE's initial data, root strat spawned */
int  fe_vm_init(FeVm *vm, const FeAssets *fe);
/* rebuild the front end (kill all actors, mode = -1, G58 = entry, cfg -> G, spawn root) */
void fe_vm_restart(FeVm *vm, int entry);
void fe_vm_seed(FeVm *vm, uint32_t seed);
void fe_vm_tick(FeVm *vm);
void fe_vm_scene(const FeVm *vm, FeScene *sc);
/* the scene at t (0..1) between the state before the last tick and now: the scripts run at
 * 15 Hz, the port draws every frame. Moves too big for one tick (jumps) are not smoothed. */
void fe_vm_scene_lerp(const FeVm *vm, FeScene *sc, float t);

/* port input bits (fight.h IN_*) of player 0/1; menu directions are screen directions:
 * IN_FWD = right, IN_BACK = left for both players */
void fe_vm_set_input(FeVm *vm, int player, unsigned bits);
void fe_vm_key_esc(FeVm *vm, int down);
void fe_vm_key_raw(FeVm *vm, int scancode, int down);   /* DirectInput scan code (op 0xE4) */

int  fe_vm_pop_sound(FeVm *vm);           /* next sound index or -1 */
int  fe_vm_live(const FeVm *vm);          /* number of live actors */

/* FUN_00433c23 (cfg -> G20..G57), FUN_00434001 (G -> cfg), FUN_004342c7 (defaults) */
void fe_vm_cfg_to_g(FeVm *vm);
void fe_vm_g_to_cfg(FeVm *vm);
void fe_vm_cfg_defaults(FeVm *vm);

#endif
