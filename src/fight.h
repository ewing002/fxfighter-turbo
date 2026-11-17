/*
 * FX Fighter Turbo - open reimplementation
 * Fight engine: move/state machine reconstructed from FIGHT.EXE
 *   StartMove   0x43e828   event processing  0x441c0b   human input  0x4427c4
 *   per-tick    0x44176b   hit test          0x4491f7   hit resolve  0x448cbb
 *   sidestep    0x43e424   line rotation     0x44998a   movement     0x440f42
 *   spacing     0x43fbca   ring edge         0x4408f9   ring-out     0x43f36e
 */
#ifndef FX_FIGHT_H
#define FX_FIGHT_H

#include "assets.h"

/* Input bits (FUN_00428036 / FUN_00426e78) */
enum { IN_FWD = 0x01, IN_BACK = 0x02, IN_DOWN = 0x04, IN_UP = 0x08, IN_PUNCH = 0x10, IN_KICK = 0x20 };

#define FIGHT_HZ 48            /* default game speed: 1193182 / 24850 PIT ticks */
#define MAX_HEALTH 120

typedef struct {
    char     name[21];
    uint8_t  limbs;    /* +0x14 index into LIMB_SETS (attacking body parts)          */
    uint8_t  stance;   /* +0x15 low 5 bits: stance id, 0xc0: attack type             */
    uint8_t  cls;      /* +0x16 class (3 = on the ground), selects body radius       */
    uint8_t  clip;     /* +0x17 anim: >=200 own .ani clip-200, <200 f0.ani, 255 keep */
    uint8_t  sound;    /* +0x18                                                       */
    uint8_t  flags;    /* +0x19                                                       */
    uint8_t  cam;      /* +0x21 camera preset while the move plays (0x80: focus on this fighter) */
    /* +0x19..+0x20 as read by the CPU opponent (0x425892), ai[k] = byte 0x19+k:
     *  ai[0]   = flags (bits 0-1: 1 attack, 2 block, 3 turn; 0x0c: ring-edge check; 0x40: high; 0x80: never chosen)
     *  ai[1..5] score per situation slot, ai[6] (0x1f) situation mask, ai[7] (0x20) frames needed before the opponent's hit */
    uint8_t  ai[8];
    uint16_t state;    /* +0x22 first record in .ste                                 */
} MoveDef;

/* .ste record, 12 bytes little-endian */
typedef struct {
    uint8_t  op;       /* 0 = input transition; 1..99 attack (damage); 100..199 self damage; 0xc9.. specials */
    uint8_t  fr;       /* frame at which the event fires / the input window closes */
    uint16_t val;      /* required value of ((in ^ prev) | in << 8) & mask        */
    uint16_t mask;
    uint8_t  cond;     /* opponent stance condition (input) / hit direction <<6 (attack) */
    uint8_t  minfr;    /* earliest frame for the transition                        */
    uint8_t  ctx;      /* high nibble == (low nibble & context bits)               */
    uint8_t  pad;
    int16_t  tgt;      /* next move                                                */
} StateRec;

typedef struct {
    MoveDef  *moves;  int nmoves;
    StateRec *st;     int nst;
} MoveSet;

int  moveset_load(MoveSet *ms, const uint8_t *hdr, size_t hsz, const uint8_t *ste, size_t ssz);
void moveset_free(MoveSet *ms);

/* fighters/fi%d.cmp: RLE + every byte +1. Fighter AND arena parameters (arena n uses fi%n). */
typedef struct {
    char  name[16], arena[16];
    float fall_depth;        /* 0x4c  floor height outside the ring (16.16)  */
    float ring_half;         /* 0x54  half size of the square ring           */
    float stun_k;            /* 0x58  stun per damage point                  */
    float stun_decay;        /* 0x5c  stun -= 5 * stun_decay per tick        */
    int   mash[3];           /* 0x60 0x64 0x68  input changes needed by ops 0xcc (half) / 0xcd / 0xce */
    float jump_v, jump_g, jump_s;   /* 0x6c 0x70 0x74 jump arc               */
    float body_r[6];         /* 0x78  body radius per move class             */
    float part_r[16];        /* 0x88  hit cube half-size per body part (/256) */
} FighterInfo;

int fighter_info_load(FighterInfo *fi, const uint8_t *cmp, size_t size);

typedef struct {
    /* static data */
    const MoveSet *ms;               /* own move table */
    const MoveSet *cur;              /* table being executed (the thrower's during a throw) */
    const AniSet  *own, *generic, *cur_own;
    const FighterInfo *fi;
    /* dynamic */
    int    move, sp, frame;
    const AniSet *aset; int clip;
    int    owner;          /* player whose table is executed */
    float  s;              /* position along the fight line */
    int    face;           /* +1: looks along +u of the line, -1: along -u */
    int    turn_flag;      /* op 0xd8: turn around at next move start */
    int    health;
    unsigned in, prev_in;
    int    cpu;
    int    ai_wait, ai_armed;  /* a5788 / f1604: idle delay (8 - level ticks) in neutral moves */
    int    guard;          /* a8278: current guard op (>= 0xf1), 0 when not blocking */
    int    ai_def, ai_def_hi;  /* a5780 / a5784: keep defending this long, side (bit 6) of the attack */
    float  stun;           /* cb3f8: dizzy meter, 0..64 between hits, > 75 after a hit = dazed */
    int    in_react;       /* cb4c8: playing a hit reaction (next hit is a combo hit) */
    int    combo;          /* c6f08: combo hit counter of this victim (index into COMBO_SCALE) */
    int    mash;           /* cb460: input changes counted for ops 0xcc..0xce (break free) */
    int    last_fwd;
    int    hit_flash;
    int    wins;
    int    cont_hit;       /* cb418: -1 armed by op 0xd4, >0 damage of a continuous hit check */
    int    mode;           /* cb4f8: 1..3 jump, 4..6 lunge */
    float  vel;            /* ccd70: speed toward the opponent (line units per 32 ticks) */
    float  jt, z;          /* jump time / height offset */
    float  radius;         /* cb530: smoothed body radius */
    int    solid;          /* a8260 */
    int    ringout;        /* fell out of the ring */
    float  vz;             /* vertical speed while falling out of the ring */
    int    have_prev_box;  /* a8268: attack box of the previous hit check (swept test) */
    float  prev_box[6];
    const Model *mesh;     /* body parts (for keeping the feet on the floor), optional */
    int    nmesh;
} Player;

typedef struct {
    Player p[2];
    float  ox, oy, ang;    /* fight line: origin and direction (a8684) */
    float  ring_half, fall_depth;
    float  land_half;      /* half size of the lower ground at fall_depth (0 = unknown) */
    int    ringouts;       /* 1 = ring-outs enabled */
    int    round_timer;    /* ticks */
    int    over_timer;     /* >0 after KO */
    int    winner;         /* -1 none */
    int    ko_reason;      /* 0 KO, 1 ring out, 2 time */
    float  last_gap;
    int    round_age;      /* ticks since the round started */
    int    start_delay;    /* ticks of round intro before the fighters may act (front end: ROUND n / FIGHT!) */
    int    round_secs;     /* round length in seconds, 0 = default 60, <0 = no time limit */
    int    hold;           /* 1 = do not start the next round automatically (the front end runs the match) */
    int    ai_level;       /* CPU difficulty 0..7 (0x4a587c, default 4); the caller sets it */
    int    cam_shake;      /* set by self damage ops 100..149 (4a4bd4); the front end clears it */
    /* sound events for the front end: bank 0 generic, 1/2 player, 3 arena; 1-based index */
    int    nsnd;
    struct { uint8_t bank, index; } snd[32];
    /* pending hits produced during event processing */
    int    hit_pending[2], hit_damage[2], hit_zone[2], hit_dir[2];
} Fight;

void fight_init_player(Player *pl, const MoveSet *ms, const AniSet *own, const AniSet *generic,
                       const FighterInfo *fi);
void fight_set_arena(Fight *f, const FighterInfo *arena_fi);
void fight_reset(Fight *f);
void fight_tick(Fight *f);                         /* one FIGHT_HZ tick */
const AniFrame *player_frame(const Player *pl);
const char *player_move_name(const Player *pl);
int player_move_cam(const Player *pl);

/* world placement of fighter i: floor position, height and yaw (degrees) of the
 * fighter-local frame (models look down local -Y). */
void fight_place(const Fight *f, int i, float *wx, float *wy, float *wz, float *yaw_deg);
/* fighter-local point (after bone transform, before root offsets) -> world */
void fight_to_world(const Fight *f, int i, const float q[3], float out[3]);
/* world position of a body part joint (hit cube centre) */
void fight_joint(const Fight *f, int i, int part, float out[3]);
void fight_line_dir(const Fight *f, float *ux, float *uy);

#endif
