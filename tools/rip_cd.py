# Rip the FX Fighter Turbo CD audio tracks (single BIN + CUE) to Ogg Vorbis or MP3 with ffmpeg.
# usage: python3 rip_cd.py "FX Fighter Turbo.cue" "FX Fighter Turbo.bin" outdir [ogg|mp3]
# The default is ogg (trackNN.ogg, needs ffmpeg with libvorbis); mp3 needs libmp3lame. The game plays either (ogg first).
# NOTE: this BIN stores audio big-endian (s16be).
import re, subprocess, sys, os
cue, binf, out = sys.argv[1], sys.argv[2], sys.argv[3]
fmt = sys.argv[4].lower() if len(sys.argv) > 4 else 'ogg'
if fmt not in ('ogg', 'mp3'): sys.exit('format must be ogg or mp3')
os.makedirs(out, exist_ok=True)
def msf(s): m,s_,f = map(int, s.split(':')); return (m*60+s_)*75+f
tracks=[]; cur=None
for line in open(cue):
    t=re.match(r'\s*TRACK (\d+) (\S+)', line)
    if t: cur={'n':int(t.group(1)),'type':t.group(2),'idx':{}}; tracks.append(cur); continue
    i=re.match(r'\s*INDEX (\d+) (\d+:\d+:\d+)', line)
    if i and cur: cur['idx'][int(i.group(1))]=msf(i.group(2))
total=os.path.getsize(binf)//2352
for k,t in enumerate(tracks):
    if t['type']!='AUDIO': continue
    start=t['idx'][1]
    nxt=tracks[k+1] if k+1<len(tracks) else None
    end=(nxt['idx'].get(0, nxt['idx'][1])) if nxt else total
    dst=os.path.join(out,'track%02d.%s'%(t['n'],fmt))
    codec=['-c:a','libvorbis','-q:a','6'] if fmt=='ogg' else ['-c:a','libmp3lame','-b:a','192k']
    print('track %02d sectors %d-%d (%.1f s)'%(t['n'],start,end,(end-start)/75), flush=True)
    p=subprocess.Popen(['ffmpeg','-y','-hide_banner','-loglevel','error','-f','s16be','-ar','44100','-ac','2','-i','pipe:0',
                        *codec,'-metadata','title=FX Fighter Turbo track %d'%t['n'],
                        '-metadata','album=FX Fighter Turbo','-metadata','track=%d'%t['n'],dst],stdin=subprocess.PIPE)
    with open(binf,'rb') as f:
        f.seek(start*2352); left=(end-start)*2352
        while left>0:
            b=f.read(min(left,2352*1000)); 
            if not b: break
            p.stdin.write(b); left-=len(b)
    p.stdin.close(); p.wait()
print('DONE', flush=True)
