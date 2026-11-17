import struct
def cstr(d,p):
    e=d.index(b'\0',p); return d[p:e].decode('latin1'),e+1
def load_mats(path):
    d=open(path,'rb').read(); p=0; mats=[]; cur=None
    while p+8<=len(d):
        cid,ln=struct.unpack_from('>II',d,p); q=p+8
        if cid==4:
            col=(d[q],d[q+1],d[q+2]); op=d[q+3]; ka,kd,ks,pw=struct.unpack_from('>4f',d,q+4); fl=struct.unpack_from('>H',d,q+20)[0]
            mt=struct.unpack_from('>6f',d,q+22); ib,ir=d[q+46],d[q+47]; nm,e=cstr(d,q+48)
            cur=dict(name=nm,colour=col,opacity=op,ka=ka,kd=kd,ks=ks,power=pw,flags=fl,xf=mt,ibase=ib,irange=ir,refs={}); mats.append(cur); p=e
        elif cid in (28,29,30,31,32,59):
            nm,e=cstr(d,q); cur['refs'][cid]=nm; p=e
        elif cid in (0,18): p=q+ln
        else: raise Exception("chunk %d @%x"%(cid,p))
    return mats
