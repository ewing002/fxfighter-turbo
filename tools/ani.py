import struct,sys
def load(f):
    d=open(f,'rb').read(); nb,na,nf=struct.unpack_from('<III',d,0); p=12; anims=[]
    for a in range(na):
        name=d[p:p+16].split(b'\0')[0].decode('latin1'); p+=16
        n=struct.unpack_from('<I',d,p)[0]; p+=4; fr=[]
        for i in range(n):
            a0,a1,a2=struct.unpack_from('<III',d,p); p+=12
            bones=struct.unpack_from('<%dh'%(3*nb),d,p); p+=6*nb
            fr.append((a0,a1,a2,bones))
        anims.append((name,fr))
    return nb,na,nf,anims,p,len(d)
if __name__=='__main__':
    for f in sys.argv[1:]:
        nb,na,nf,an,p,L=load(f)
        print(f.split('/')[-1],nb,na,nf,sum(len(x[1]) for x in an),p,L)
