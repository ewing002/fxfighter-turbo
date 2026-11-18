# Copy the FMV files (FIGHT/VIDEO/*.BRP, *.CLT, *.PAL) out of the FX Fighter Turbo CD image.
# usage: python3 rip_video.py "FX Fighter Turbo.bin" outdir
# The BIN is MODE1/2352: 2048 user bytes per sector at offset 16; the data track is an ISO 9660 image.
import os, struct, sys

binf, out = sys.argv[1], sys.argv[2]
f = open(binf, 'rb')


def sector(n):
    f.seek(n * 2352 + 16)
    return f.read(2048)


def read_file(lba, size):
    data = bytearray()
    n = 0
    while len(data) < size:
        data += sector(lba + n)
        n += 1
    return bytes(data[:size])


def entries(lba, size):
    """the records of an ISO 9660 directory: (name, lba, size, is_dir)"""
    data = read_file(lba, size)
    o = 0
    while o < len(data):
        l = data[o]
        if l == 0:                                   # records do not cross sector boundaries
            o = (o // 2048 + 1) * 2048
            continue
        r = data[o:o + l]
        o += l
        name = r[33:33 + r[32]]
        if name in (b'\x00', b'\x01'):
            continue
        yield (name.decode('latin1').split(';')[0], struct.unpack_from('<I', r, 2)[0],
               struct.unpack_from('<I', r, 10)[0], bool(r[25] & 2))


pvd = sector(16)
if pvd[1:6] != b'CD001':
    sys.exit('not an ISO 9660 data track')
d_lba, d_size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
for part in ('FIGHT', 'VIDEO'):
    hit = [e for e in entries(d_lba, d_size) if e[0].upper() == part and e[3]]
    if not hit:
        sys.exit('FIGHT/VIDEO not found on the CD image')
    d_lba, d_size = hit[0][1], hit[0][2]
os.makedirs(out, exist_ok=True)
for name, lba, size, is_dir in entries(d_lba, d_size):
    if is_dir:
        continue
    dst = os.path.join(out, name.lower())
    with open(dst, 'wb') as o:
        o.write(read_file(lba, size))
    print('%-14s %9d bytes' % (name.lower(), size), flush=True)
print('DONE', flush=True)
