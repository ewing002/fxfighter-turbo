import struct,sys
def load(fid, base='/root/w/pac/fight/moves/'):
    h=open(base+'f%d.hdr'%fid,'rb').read(); s=open(base+'f%d.ste'%fid,'rb').read()
    n=(len(h)-2)//36
    moves=[]
    for i in range(n):
        r=h[2+i*36:2+i*36+36]
        moves.append(dict(name=r[:20].split(b'\0')[0].decode('latin1'), raw=r[20:], stance=r[21], cls=r[22], clip=r[23], snd=r[24], flags=r[25], state=struct.unpack_from('<H',r,34)[0]))
    states=[struct.unpack_from('<BBHHBBBBh',s,i*12) for i in range(len(s)//12)]
    return moves,states
def trans(moves,states,mi):
    out=[];k=moves[mi]['state']
    while k<len(states):
        st=states[k]; out.append(st)
        op,fr,val,mask,cond,mn,ctx,pad,tgt=st
        if mask==0 and ctx==0: break
        k+=1
    return out
if __name__=='__main__':
    fid=int(sys.argv[1]); mv,st=load(fid)
    for mi in map(int,sys.argv[2:]):
        m=mv[mi]; print(f"== {mi} {m['name']} stance={m['stance']:#x} cls={m['cls']} clip={m['clip']} snd={m['snd']} flags={m['flags']:#x} raw={m['raw'].hex(' ')}")
        for t in trans(mv,st,mi):
            op,fr,val,mask,cond,mn,ctx,pad,tgt=t
            print(f"   op={op:#04x} fr={fr:3d} val={val:#06x} mask={mask:#06x} cond={cond:#04x} minfr={mn:3d} ctx={ctx:#04x} -> {tgt} {mv[tgt]['name'] if 0<=tgt<len(mv) else ''}")
