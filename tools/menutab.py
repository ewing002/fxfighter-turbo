# Decode the table-driven menu panels of strat 82 (top box, state G69) and strat 87 (bottom box, state G72)
from stratdis import starts, MODELS, rd
import struct
def w(a): return struct.unpack('<h', rd(a, 2))[0]
PANELS = {
  'TOP strat82 (G69)': dict(base=starts[82], n=18, count=0x49e037, ccount=0x49e05b, model=0x49e07f, x=0x49e0a3, y=0x49e0c7,
       rx=0x49e0eb, ry=0x49e10f, rz=0x49e133, strat=0x49e157, scale=0x49e17b,
       a1x=0x49e19f, a1y=0x49e1c3, a2x=0x49e1e7, a2y=0x49e20b, arz=0x49e22f),
  'BOTTOM strat87 (G72)': dict(base=starts[87], n=21, count=0x49e83b, ccount=0x49e865, model=0x49e891, x=0x49e8bd, y=0x49e8e9,
       rx=0x49e915, ry=0x49e941, rz=0x49e96d, strat=0x49e999, scale=0x49e9c5,
       a1x=0x49e9f1, a1y=0x49ea1d, a2x=0x49ea49, a2y=0x49ea75, arz=0x49eaa1),
  'MIDDLE strat92 (G75)': dict(base=starts[92], n=5, count=0x49f9c8, ccount=0x49f9d2, model=0x49f9dc, x=0x49f9e6, y=0x49f9f0,
       rx=0x49f9fa, ry=0x49fa04, rz=0x49fa0e, strat=0x49fa18, scale=0x49fa22,
       a1x=0x49fa2c, a1y=0x49fa34, a2x=0x49fa3c, a2y=0x49fa44, arz=0x49fa4c),
}
def MN(m): return MODELS[m] if 0 <= m < len(MODELS) else str(m)
for name, T in PANELS.items():
    print('#####', name)
    B = T['base']
    for s in range(T['n']):
        cnt = w(T['count'] + 2 * s)
        cc = w(T['ccount'] + 2 * s)
        if cnt < 1:
            print('state %2d: (no items)' % s); continue
        offs = {k: w(T[k] + 2 * s) for k in ('model', 'x', 'y', 'rx', 'ry', 'rz', 'strat', 'scale')}
        items = []
        for i in range(cnt):
            v = {k: w(B + offs[k] + 2 * i) for k in offs}
            items.append('%s(x=%d,y=%d,r=%d/%d/%d,s=%d,strat=%d)' % (MN(v['model']), v['x'], v['y'], v['rx'], v['ry'], v['rz'], v['scale'], v['strat']))
        print('state %2d: %d items, %d cursor slots' % (s, cnt, cc))
        for it in items: print('      ', it)
        if cc > 0:
            co = {k: w(T[k] + 2 * s) for k in ('a1x', 'a1y', 'a2x', 'a2y', 'arz')}
            cur = []
            for i in range(cc):
                cur.append('(%d,%d)|(%d,%d) rz=%d' % tuple(w(B + co[k] + 2 * i) for k in ('a1x', 'a1y', 'a2x', 'a2y', 'arz')))
            print('     cursor:', '; '.join(cur))
