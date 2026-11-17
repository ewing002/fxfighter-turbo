import struct
def load(path):
    """BRender pixelmap file -> list of [name, type, w, h, pixels, row_bytes, palette_entry_or_None].
    Chunks are stack based: OLD_PIXELMAP pushes, PIXELS fills the top, ADD_MAP (34) pops the top
    (a palette) and attaches it to the new top."""
    d=open(path,'rb').read(); p=0; out=[]; stack=[]
    while p+8<=len(d):
        cid,ln=struct.unpack_from('>II',d,p); q=p+8
        if cid==3:
            t=d[q]; rb,w,h=struct.unpack_from('>HHH',d,q+1); e=d.index(b'\0',q+11)
            cur=[d[q+11:e].decode('latin1'),t,w,h,None,rb,None]; stack.append(cur); p=e+1; continue
        if cid==33:
            c,es=struct.unpack_from('>II',d,q)
            if stack: stack[-1][4]=d[q+8:q+8+c*es]
            p=q+8+c*es; continue
        if cid==34 and len(stack)>=2:
            pal=stack.pop(); stack[-1][6]=pal
        if cid==0:
            while stack: out.append(stack.pop(0))
        p=q+ln
    out+=stack
    return out
def pal_from(entry):
    px=entry[4]; return [x for k in range(256) for x in px[k*4+1:k*4+4]]
