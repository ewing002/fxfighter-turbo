import struct
def names(d,p,count):
    out=[]
    for i in range(count):
        e=d.index(b'\0',p); out.append(d[p:e].decode('latin1')); p=e+1
    return out,p
def load_models(path):
    d=open(path,'rb').read(); p=0; models=[]; cur=None
    while p+8<=len(d):
        cid,ln=struct.unpack_from('>II',d,p); q=p+8
        if cid==18: p=q+ln; continue                # FILE_INFO
        if cid==0: p=q+ln; continue                 # END
        if cid==54:                                 # OLD_MODEL_2
            flags=struct.unpack_from('>H',d,q)[0]; nm,_=names(d,q+2,1)
            cur=dict(name=nm[0],flags=flags,v=[],uv=[],f=[],fm=[],mats=[]); models.append(cur); p=q+ln
        elif cid==23:
            n=struct.unpack_from('>I',d,q)[0]; cur['v']=[struct.unpack_from('>3f',d,q+4+i*12) for i in range(n)]; p=q+ln
        elif cid==24:
            n=struct.unpack_from('>I',d,q)[0]; cur['uv']=[struct.unpack_from('>2f',d,q+4+i*8) for i in range(n)]; p=q+ln
        elif cid==53:
            n=struct.unpack_from('>I',d,q)[0]; cur['f']=[struct.unpack_from('>4HB',d,q+4+i*9) for i in range(n)]; p=q+ln
        elif cid==22:
            n=struct.unpack_from('>I',d,q)[0]; cur['mats'],p=names(d,q+4,n)
        elif cid==26:
            n,sz=struct.unpack_from('>II',d,q); cur['fm']=list(struct.unpack_from('>%dH'%n,d,q+8)); p=q+8+n*2
        else:
            raise Exception("chunk %d at %x"%(cid,p))
    return models
