# FX Fighter Turbo front-end "strat" bytecode disassembler (read-only, reads FIGHT.EXE via pe.py)
# usage: py -3 stratdis.py [strat indices...]
from pe import *
import struct, sys

def u32(va): return struct.unpack('<I', rd(va, 4))[0]

NS = 93
STRAT_TAB = 0x49fb88
starts = [u32(STRAT_TAB + i * 4) for i in range(NS)]
END = STRAT_TAB
MODELS = []
for i in range(212):
    p = u32(0x4a2948 + i * 4)
    MODELS.append(rd(p, 32).split(b'\0')[0].decode('latin1'))

def MN(m):
    if m < len(MODELS): return MODELS[m]
    return {0xff: 'NULL', 0xfe: 'CAMERA'}.get(m, '?%d' % m)

FIELD = {0x0c: 'target', 0x10: 'vx', 0x14: 'vy', 0x18: 'vz', 0x1c: 'accel', 0x20: 'speed', 0x24: 'tspeed',
         0x28: 'id', 0x2c: 'serial', 0x30: 'rx', 0x34: 'ry', 0x38: 'rz', 0x3c: 'x', 0x40: 'y', 0x44: 'z',
         0x48: 'sx', 0x4c: 'sy', 0x50: 'sz', 0x58: 'colmask', 0x70: 'hit', 0xd4: 'type', 0xd8: 'frame',
         0xdc: 'flags', 0xe0: 'arg', 0xe4: 'world'}
def L(b): return FIELD.get(b, 'v%02x' % b)
def G(b): return 'G%d' % b
TRIG = {0: 'never', 1: 'always', 2: 'every2', 3: 'every4', 4: 'every8', 5: 'every16', 6: 'every32',
        7: 'every64', 8: 'every128', 9: 'hit', 10: 'arg<=0', 11: 'far_z', 12: 'in4a87c0&1', 13: 'in4a87c0&2',
        14: 'count==1', 15: 'key'}
KEYS = {1: 'ESC', 0x20: 'P1_RIGHT', 0x1f: 'P1_LEFT', 0x2c: 'P1_DOWN', 0x1e: 'P1_UP', 0x2d: 'P1_PUNCH',
        0x2e: 'P1_KICK', 0x23: 'P2_RIGHT', 0x24: 'P2_LEFT', 0x32: 'P2_DOWN', 0x25: 'P2_UP', 0x31: 'P2_PUNCH',
        0x30: 'P2_KICK'}

CMP = {}
for base, sym in ((0x44, '=='), (0x46, '!='), (0x4a, '>'), (0x4c, '<')):
    CMP[base] = (sym, 'LL'); CMP[base + 1] = (sym, 'LI')
for base, sym in ((0x8e, '=='), (0x92, '!='), (0x86, '>'), (0x8a, '<')):
    for k, kind in enumerate(('LG', 'GL', 'GG', 'GI')):
        CMP[base + k] = (sym, kind)
ARITH = {0x3e: ('/=', 'LL'), 0x40: ('/=', 'LG'), 0x41: ('/=', 'GL'), 0x43: ('/=', 'GG'),
         0x5b: ('*=', 'LL'), 0x5d: ('*=', 'LG'), 0x5e: ('*=', 'GL'), 0x60: ('*=', 'GG'),
         0x73: ('-=', 'LL'), 0x75: ('-=', 'LG'), 0x76: ('-=', 'GL'), 0x78: ('-=', 'GG'),
         0x0e: ('+=', 'LL'), 0x10: ('+=', 'LG'), 0x11: ('+=', 'GL'), 0x13: ('+=', 'GG'),
         0x08: ('=', 'LL'), 0x0a: ('=', 'LG'), 0x0b: ('=', 'GL'), 0x0d: ('=', 'GG')}
ARITHI = {0x3f: ('/=', 'L'), 0x42: ('/=', 'G'), 0x5c: ('*=', 'L'), 0x5f: ('*=', 'G'), 0x74: ('-=', 'L'),
          0x77: ('-=', 'G'), 0x0f: ('+=', 'L'), 0x12: ('+=', 'G'), 0x09: ('=', 'L'), 0x0c: ('=', 'G')}

def dis(a, stop):
    out = []
    while a < stop:
        b = rd(a, 40); op = b[0]
        s16 = lambda i: struct.unpack_from('<h', b, i)[0]
        J = lambda i: '%x' % (a + s16(i))
        f = lambda k, i: L(b[i]) if k == 'L' else G(b[i])
        n = 1; t = None
        if op in (0, 0xac, 0xad): t = 'halt'
        elif op in (1, 0x38): t = ('jmp_yield ' if op == 1 else 'jmp ') + J(1); n = 3
        elif op in (2, 4, 6): t = '%s += %s' % (['rx', 'ry', 'rz'][(op - 2) // 2], L(b[1])); n = 2
        elif op in (3, 5, 7): t = '%s += %d' % (['rx', 'ry', 'rz'][(op - 3) // 2], s16(1)); n = 3
        elif op in ARITH: s, k = ARITH[op]; t = '%s %s %s' % (f(k[0], 1), s, f(k[1], 2)); n = 3
        elif op in ARITHI: s, k = ARITHI[op]; t = '%s %s %d' % (f(k, 1), s, s16(2)); n = 4
        elif op == 0x7f:
            st = rd(a + 1, 200).split(b'\0')[0]; t = 'dbgprint "%s"' % st.decode('latin1'); n = len(st) + 2
        elif op in (0x14, 0x3b):
            st = rd(a + 2, 200).split(b'\0')[0]; t = 'dbgprint %s "%s"' % (L(b[1]), st.decode('latin1')); n = len(st) + 3
        elif op == 0x15: t = 'push %s' % L(b[1]); n = 2
        elif op == 0x16: t = 'push %d' % s16(1); n = 3
        elif op == 0x17: t = 'pop %s' % L(b[1]); n = 2
        elif op == 0x18: t = 'loop %s times {' % L(b[1]); n = 2
        elif op == 0x19: t = 'loop %d times {' % s16(1); n = 3
        elif op == 0x1a: t = '} endloop (yield)'
        elif op == 0x39: t = '} endloop'
        elif op == 0x1b: t = 'call ' + J(1); n = 3
        elif op == 0x1c: t = 'ret'
        elif op == 0x1d: t = 'speed = %s' % L(b[1]); n = 2
        elif op == 0x1e: t = 'speed = %d' % s16(1); n = 3
        elif op == 0x1f: t = 'tspeed=%s accel=%s' % (L(b[1]), L(b[2])); n = 3
        elif op == 0x20: t = 'tspeed=%d accel=%d' % (s16(1), s16(3)); n = 5
        elif op == 0x21: t = 'flags |= 0x%x' % (s16(1) & 0xffff); n = 3
        elif op == 0x22: t = 'flags &= ~0x%x' % (s16(1) & 0xffff); n = 3
        elif op in (0x23, 0x24, 0xb2, 0xb3):
            ty = b[1]; tn = TRIG.get(ty, 't%d' % ty); pre = 'trigger' if op < 0xb0 else 'trigger_reset'
            if ty == 0 and op < 0xb0: t = 'trigger_remove %s' % J(2); n = 4
            elif op in (0x23, 0xb3): t = '%s %s count=%d -> %s' % (pre, tn, s16(4), J(2)); n = 6
            elif ty == 0xf: t = '%s key %s -> %s' % (pre, KEYS.get(b[4], '0x%x' % b[4]), J(2)); n = 5
            else: t = '%s %s -> %s' % (pre, tn, J(2)); n = 4
        elif op in (0x25, 0x26):
            p = [s16(1 + 2 * i) for i in range(6)]
            t = '%s pos=(%d,%d,%d) rot=(%d,%d,%d) scale=%d strat=%d model=%s arg=%d world=%d' % (
                'spawn_rel' if op == 0x25 else 'spawn_child', p[0], p[1], p[2], p[3], p[4], p[5],
                s16(13), s16(15), MN(b[17]), b[18], b[19])
            n = 0x14
            if op == 0x26: t += ' id=%d' % b[20]; n = 0x15
        elif op == 0x27: t = 'die'
        elif op == 0x28: t = '%s = rand(%d)' % (L(b[1]), s16(2)); n = 4
        elif op == 0x29: t = 'set_focus_self'
        elif op == 0x2a: t = 'attach_main_camera'
        elif op in (0x2b, 0x2d, 0x2f): t = '%s += %s' % (['x', 'y', 'z'][(op - 0x2b) // 2], L(b[1])); n = 2
        elif op in (0x2c, 0x2e, 0x30): t = '%s += %d' % (['x', 'y', 'z'][(op - 0x2c) // 2], s16(1)); n = 3
        elif op == 0x31: t = 'wait %s' % L(b[1]); n = 2
        elif op == 0x32: t = 'wait %d' % s16(1); n = 3
        elif op == 0x33: t = 'if rand100 < %s jmp %s' % (L(b[3]), J(1)); n = 4
        elif op == 0x34: t = 'if rand100 < %d jmp %s' % (s16(3), J(1)); n = 5
        elif op in (0x35, 0x36, 0x3a, 0x57, 0xc0, 0xc1):
            t = 'turn_to_focus%s max=%d' % (' (wait)' if op in (0x3a, 0x57) else '', b[1]); n = 2
        elif op == 0x37: t = 'spawn_here strat=%d world=%d model=%s' % (s16(3), b[5], MN(b[6])); n = 7
        elif op == 0x3c: t = '%s = -%s' % (L(b[1]), L(b[1])); n = 2
        elif op == 0x3d: t = '%s = -%s' % (G(b[1]), G(b[1])); n = 2
        elif op in CMP:
            sym, k = CMP[op]
            if k[1] == 'I': t = 'if %s %s %d jmp %s' % (f(k[0], 1), sym, s16(2), J(4)); n = 6
            else: t = 'if %s %s %s jmp %s' % (f(k[0], 1), sym, f(k[1], 2), J(3)); n = 5
        elif op == 0x48: t = 'if %d<=%s<=%d jmp %s' % (s16(2), L(b[1]), s16(4), J(6)); n = 8
        elif op == 0x49: t = 'if !(%d<=%s<=%d) jmp %s' % (s16(2), L(b[1]), s16(4), J(6)); n = 8
        elif op in (0x4e, 0x50): t = '%s approach %s%s' % (L(b[1]), L(b[2]), ' (wait)' if op == 0x50 else ''); n = 3
        elif op in (0x4f, 0x51): t = '%s approach %d%s' % (L(b[1]), s16(2), ' (wait)' if op == 0x51 else ''); n = 4
        elif op == 0x52: t = 'moveto rel(%d,%d,%d) speed=%d' % (s16(1), s16(3), s16(5), s16(7)); n = 9
        elif op == 0x72: t = 'moveto rel(%s,%s,%s) speed=%s' % (L(b[1]), L(b[2]), L(b[3]), L(b[4])); n = 5
        elif op == 0x53: t = 'nop3'; n = 3
        elif op == 0x54: t = 'wobble every %s' % L(b[1]); n = 2
        elif op == 0x7e: t = 'wobble every %d' % b[1]; n = 2
        elif op == 0x55: t = 'orbitY by %s' % L(b[1]); n = 2
        elif op == 0x56: t = 'orbitY by %d' % s16(1); n = 3
        elif op == 0x58: t = 'sound %d' % s16(1); n = 3
        elif op == 0x59: t = 'if dist_focus < %s jmp %s' % (L(b[1]), J(2)); n = 4
        elif op == 0x5a: t = 'if dist_focus < %d jmp %s' % (s16(1), J(3)); n = 5
        elif op == 0x61: t = 'hide'
        elif op == 0x62: t = 'show'
        elif op in (0x63, 0x64):
            t = 'spawn%s_vars pos=(%s,%s,%s) rot=(%s,%s,%s) scale=%s strat=%s model=%s arg=%d world=%d' % (
                '_child' if op == 0x64 else '_rel', L(b[1]), L(b[2]), L(b[3]), L(b[4]), L(b[5]), L(b[6]),
                L(b[7]), L(b[8]), L(b[9]), b[10], b[11]); n = 0xc
            if op == 0x64: t += ' id=%s' % L(b[12]); n = 0xd
        elif 0x65 <= op <= 0x70:
            fld = ['x', 'y', 'z', 'rx', 'ry', 'rz'][(op - 0x65) // 2]
            if (op - 0x65) % 2 == 0: t = '%s = %s' % (fld, L(b[1])); n = 2
            else: t = '%s = %d' % (fld, s16(1)); n = 3
        elif op == 0x71: t = 'yield'
        elif op in (0x79, 0x9f): t = 'with child %s {' % (L(b[1]) if op == 0x79 else 'id=%d' % b[1]); n = 2
        elif op == 0x7a: t = '} endwith'
        elif op == 0x7b: t = 'with last_spawned {'
        elif op == 0x7c: t = 'if rand100 < %s call %s' % (L(b[3]), J(1)); n = 4
        elif op == 0x7d: t = 'if rand100 < %d call %s' % (s16(3), J(1)); n = 5
        elif op == 0x80: t = 'state_goto %s' % J(1); n = 3
        elif op == 0x81: t = 'state_gosub %s' % J(1); n = 3
        elif op in (0x82, 0x83): t = 'HANG(bad op)'
        elif op == 0x84: t = 'scale = (%s,%s,%s)' % (L(b[1]), L(b[2]), L(b[3])); n = 4
        elif op == 0x85: t = 'scale = (%d,%d,%d)' % (s16(1), s16(3), s16(5)); n = 7
        elif op == 0x96: t = 'make_camera %s' % (['main', 'right', 'left'][b[1]] if b[1] < 3 else b[1]); n = 2
        elif op == 0x97: t = 'load_bg_scene %s,%s,%s' % (L(b[1]), L(b[2]), L(b[3])); n = 4
        elif op == 0x98: t = 'model = %s' % L(b[1]); n = 2
        elif op == 0xbf: t = 'model = %s' % MN(b[1]); n = 2
        elif 0x99 <= op <= 0x9e: t = '%s = G[%s]' % (['x', 'y', 'z', 'rx', 'ry', 'rz'][op - 0x99], L(b[1])); n = 2
        elif op == 0xa0: t = 'detach child %s' % L(b[1]); n = 2
        elif op == 0xa1: t = 'detach child id=%d' % b[1]; n = 2
        elif op == 0xa2: t = 'detach self'
        elif op == 0xa3: t = 'with hit {'
        elif op == 0xa4: t = 'with target {'
        elif op in (0xa5, 0xa6, 0xa7, 0xa8, 0xc4, 0xc5):
            t = 'turn_to_target%s max=%d' % (' (wait)' if op in (0xa7, 0xa8) else '', b[1]); n = 2
        elif op == 0xa9: t = 'detach all children'
        elif op == 0xaa: t = 'if flags&0x%x==all jmp %s' % (s16(1) & 0xffff, J(3)); n = 5
        elif op == 0xab: t = 'if flags&0x%x==0 jmp %s' % (s16(1) & 0xffff, J(3)); n = 5
        elif op == 0xae: t = 'orbitX by %s' % L(b[1]); n = 2
        elif op == 0xaf: t = 'orbitX by %d' % s16(1); n = 3
        elif op == 0xb0: t = 'orbitZ by %s' % L(b[1]); n = 2
        elif op == 0xb1: t = 'orbitZ by %d' % s16(1); n = 3
        elif op == 0xb4: t = 'if no child %s jmp %s' % (L(b[1]), J(2)); n = 4
        elif op == 0xb5: t = 'if no child id=%d jmp %s' % (b[1], J(2)); n = 4
        elif op in (0xb6, 0xb7, 0xbb, 0xbc, 0xc2, 0xc3):
            t = 'if not facing focus within %s jmp %s' % (L(b[1]) if op in (0xb6, 0xbb, 0xc2) else str(b[1]), J(2)); n = 4
        elif op == 0xb8: t = 'target = hit'
        elif op == 0xb9: t = 'if target dead jmp %s' % J(1); n = 3
        elif op == 0xba: t = 'target = find_nearest'
        elif op == 0xbd: t = 'material_xyz(4)'; n = 4
        elif op == 0xbe: t = 'material_xyz(7)'; n = 7
        elif op in (0xc6, 0xc7, 0xc8, 0xc9): n = {0xc6: 2, 0xc7: 3, 0xc8: 4, 0xc9: 7}[op]; t = 'nop%d' % n
        elif op == 0xca: t = 'if hit jmp %s' % J(1); n = 3
        elif op == 0xcb: t = 'font %d' % b[1]; n = 2
        elif op == 0xcc: t = 'textcolour %d' % b[1]; n = 2
        elif op == 0xcd:
            st = rd(a + 1, 200).split(b'\0')[0]; t = 'drawtext(buggy) "%s"' % st.decode('latin1'); n = len(st) + 6
        elif op == 0xce: t = '%s = word[%x + 2*%s]' % (L(b[4]), a + s16(1), L(b[3])); n = 5
        elif op == 0xcf: t = '%s = strat%d.word[%s + 2*%s]' % (L(b[5]), s16(1), L(b[3]), L(b[4])); n = 6
        elif op == 0xd0: t = 'call strat%d + word[%x + 2*%s]' % (s16(1), a + s16(3), L(b[5])); n = 6
        elif op == 0xd1: t = 'target = last_spawned'
        elif op == 0xd2: t = 'unpack4 %s -> %s,%s,%s,%s' % tuple(L(b[i]) for i in range(1, 6)); n = 6
        elif op == 0xd3: t = 'pack4 %s <- %s,%s,%s,%s' % tuple(L(b[i]) for i in range(1, 6)); n = 6
        elif op == 0xd4: t = 'OPT[%s] = %s' % (L(b[1]), L(b[2])); n = 3
        elif op == 0xd6: t = '%s = OPT[%s]' % (L(b[2]), L(b[1])); n = 3
        elif op == 0xd5: t = 'sound %s' % L(b[1]); n = 2
        elif op == 0xd7: t = 'START_GAME mode=%s a=%s b=%s c=%s d=%s e=%s' % tuple(L(b[i]) for i in range(1, 7)); n = 7
        elif op == 0xd8: t = 'sys_434439'
        elif op == 0xd9: t = 'sys_434001; sys_4343da'
        elif op == 0xda: t = 'sys_434001'
        elif op == 0xdb: t = 'sys_433c23'
        elif op == 0xdc: t = 'sys_4342c7'
        elif op == 0xdd: t = 'sys_4343da'
        elif op == 0xde: t = 'sys_43b5f0'
        elif op == 0xdf: t = '4a589a=G48; sys_4344a9'
        elif op == 0xe0: t = '4a589b=G49; sys_4344a9'
        elif op == 0xe1: t = '%s = f_4383e2()' % L(b[1]); n = 2
        elif op == 0xe2: t = '%s = f_4382ef()' % L(b[1]); n = 2
        elif op == 0xe3: t = 'f_4382c9(%s)' % L(b[1]); n = 2
        elif op == 0xe4: t = 'getkey scancode->%s char->%s' % (L(b[1]), L(b[2])); n = 3
        elif op == 0xe5: t = '%s = keymodel[%s]' % (L(b[2]), L(b[1])); n = 3
        elif op == 0xe6: t = 'sound %d; f_410047' % s16(1); n = 5
        elif op == 0xe7: t = 'f_41027e'; n = 3
        elif op == 0xe8: t = 'sys_438354'
        elif op == 0xe9: t = 'sys_43b077(2)'
        elif op == 0xea: t = 'sys_43b077(-2)'
        elif op == 0xeb: t = '%s = keyfree(%s)' % (L(b[2]), L(b[1])); n = 3
        elif op == 0xec: t = '4a5899=G51+1; sys_433f83'
        elif op == 0xed: t = 'sys_41a8ee'
        if t is None: t = '??? op %02x' % op
        out.append((a, rd(a, n).hex(), t))
        a += n
    return out

if __name__ == '__main__':
    sel = [int(x) for x in sys.argv[1:]] or range(NS)
    for i in sel:
        s = starts[i]; e = starts[i + 1] if i + 1 < NS else END
        print('==== strat %d @ %x (%d bytes)' % (i, s, e - s))
        for a, h, t in dis(s, e):
            print('  %x: %-26s %s' % (a, h[:26], t))
