"""FX Fighter .asf decoder (FIGHT.EXE FUN_0044f7b1 / 0x44f7e6 / 0x44fb1e).
Header (44 bytes): 'ASF\\0', ..., name[8]@0x10, u32 nblocks@0x18, u32 block_samples@0x1c (32),
u32 ?@0x20, u16 rate@0x24, u32 flags@0x28 (bit0 16-bit, bit1 stereo). Data @0x2c:
per block 1 control byte (hi nibble = shift, bit2 = 2nd-order predictor) + 16 bytes of 4-bit deltas (hi nibble first)."""
import struct, sys
def decode(d):
    nb, bs = struct.unpack_from('<II', d, 0x18)
    rate = struct.unpack_from('<H', d, 0x24)[0]
    flags = struct.unpack_from('<I', d, 0x28)[0]
    p = 0x2c; out = [0, 0]
    for b in range(nb):
        if p >= len(d): break
        hdr = d[p]; p += 1
        cl = (hdr >> 4) + 2; pred2 = hdr & 4
        for k in range(bs // 2):
            byte = d[p + k] if p + k < len(d) else 0
            for nib in ((byte & 0xf0), ((byte & 0x0f) << 4)):
                n = nib - 256 if nib >= 128 else nib           # signed nibble * 16
                prev, prev2 = out[-1], out[-2]
                if pred2:
                    ebx = (prev * 128) & 0xffffffff
                    eax = (-prev2 * 64) & 0xffffffff
                    edx = (n << cl) & 0xffffffff
                    s = eax + edx; carry = s >> 32; s &= 0xffffffff
                    s = (s + ebx + carry) & 0xffffffff
                else:
                    s = ((prev * 64) + (n << cl)) & 0xffffffff
                if s & 0x80000000: s -= 1 << 32
                s >>= 6
                s = ((s + 32768) & 0xffff) - 32768               # stored as 16-bit word
                out.append(s)
        p += bs // 2
    return rate, flags, out[2:]
def write_wav(path, rate, samples):
    import wave
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(struct.pack('<%dh' % len(samples), *samples)); w.close()
if __name__ == '__main__':
    for f in sys.argv[1:]:
        r, fl, s = decode(open(f, 'rb').read())
        print(f, r, fl, len(s), '%.2f s' % (len(s) / r))
