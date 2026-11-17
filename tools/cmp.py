import struct,sys
def packbits(d):
    n=struct.unpack_from('<I',d,0)[0]; p=4; out=bytearray()
    while p<len(d) and len(out)<n:
        c=d[p]; p+=1
        if c<0x80: out+=d[p:p+c+1]; p+=c+1
        elif c>0x80: out+=bytes([d[p]])*(257-c); p+=1
    return n,out,p
def rle2(d):  # pairs value,count
    n=struct.unpack_from('<I',d,0)[0]; p=4; out=bytearray()
    while p+1<len(d) and len(out)<n:
        out+=bytes([d[p]])*d[p+1]; p+=2
    return n,out,p
if __name__=='__main__':
  for f in sys.argv[1:]:
    d=open(f,'rb').read()
    for fn in (packbits,rle2):
        n,o,p=fn(d); print(f, fn.__name__, n, len(o), p, len(d))
def fx(d):
    n=struct.unpack_from('<I',d,0)[0]; p=4; out=bytearray()
    while p<len(d) and len(out)<n:
        c=d[p]; p+=1
        if c<0x80:
            if p>=len(d): break
            out+=bytes([d[p]])*(c+2); p+=1
        else: k=256-c; out+=d[p:p+k]; p+=k
    return n,out,p
