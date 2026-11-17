import struct,sys
import os
# FIGHT.EXE helye: FIGHT_EXE környezeti változó, alapból ../FIGHT.EXE (a port mappájából futtatva)
d=open(os.environ.get('FIGHT_EXE', '../FIGHT.EXE'),'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
ns=struct.unpack_from('<H',d,pe+6)[0]; osz=struct.unpack_from('<H',d,pe+20)[0]
ib=struct.unpack_from('<I',d,pe+52)[0]
secs=[]
for i in range(ns):
    o=pe+24+osz+i*40
    nm=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ra=struct.unpack_from('<IIII',d,o+8)
    secs.append((nm,ib+va,vs,ra,rs))
def rd(va,n):
    for nm,v,vs,ra,rs in secs:
        if v<=va<v+max(vs,rs):
            off=va-v
            return d[ra+off:ra+off+n] if off<rs else b'\0'*n
    return None
if __name__=='__main__':
    for s in secs: print(s[0],hex(s[1]),hex(s[2]),hex(s[3]),hex(s[4]))
