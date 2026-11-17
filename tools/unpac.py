import struct, sys, os
src, dst = sys.argv[1], sys.argv[2]
d = open(src,'rb').read()
n = struct.unpack_from('<I', d, 0)[0]
ents = []
for i in range(n):
    off = struct.unpack_from('<I', d, 4+i*84)[0]
    name = d[8+i*84:4+(i+1)*84].split(b'\0')[0].decode('latin1')
    ents.append((off, name))
for i,(off,name) in enumerate(ents):
    if not name: continue
    end = ents[i+1][0] if i+1<n else len(d)
    p = os.path.join(dst, *name.replace('\\','/').lower().split('/'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p,'wb').write(d[off:end])
    print(f"{off:08x} {end-off:9d} {name}")
