import json,sys,numpy as np,math,re
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
sys.path.insert(0,".")
from heldout import DEV,TEST
exec(open("compare.py").read().split("rows=[json.loads")[0])   # enc(), tokenizer, weights
rows=[json.loads(l) for l in open("corpus.jsonl")]
keep=[r for r in rows if r["label"]!="multi"]
C=np.stack([enc(r["text"]) for r in keep])

# --- router, now indexing the SAME corpus (char n-gram hashing, 0 model bytes)
DIM=512
def hemb(t):
    t=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; t=re.sub(r"\s+"," ",t)
    v=np.zeros(DIM)
    for n in (3,4):
        for i in range(len(t)-n+1): v[hash(t[i:i+n])%DIM]+=1.0
    nrm=np.linalg.norm(v) or 1.0
    return v/nrm
Rc=np.stack([hemb(r["text"]) for r in keep])

def pick(Cmat,vec,th):
    s=Cmat@vec; j=int(s.argmax())
    p=keep[j]["label"] if s.max()>th else None
    return (None if p=="none" else p)

# thresholds: each system tuned on DEV only
def tune(Cmat,embf):
    best=(0.5,-1)
    for th in np.arange(0.20,0.95,0.01):
        ok=sum(pick(Cmat,embf(q),th)==g for q,g in DEV)
        if ok>best[1]: best=(th,ok)
    return best
th_e,de=tune(C,enc); th_r,dr=tune(Rc,hemb)
print("  encoder threshold %.2f (dev %d/10) | router threshold %.2f (dev %d/10)"%(th_e,de,th_r,dr))

clean=[(q,g) for q,g in TEST if (C@enc(q)).max()<=0.80]
print("  %d independent test queries\n"%len(clean))
print("  %-38s %-16s %-16s"%("query","router (0 KB)","encoder (610 KB)"))
ro=eo=0
for q,g in clean:
    rp=pick(Rc,hemb(q),th_r); ep=pick(C,enc(q),th_e)
    ro+= rp==g; eo+= ep==g
    mark="" if (rp==g)==(ep==g) else ("<- encoder" if ep==g else "<- router")
    print("  %-38s %-16s %-16s %s"%(q[:38],rp or "refuse",ep or "refuse",mark))
n=len(clean)
print()
print("  deterministic router : %d/%d (%.0f%%)   0 KB     same index"%(ro,n,100*ro/n))
print("  tinyenc encoder      : %d/%d (%.0f%%)   610 KB   same index"%(eo,n,100*eo/n))
d=100*(eo-ro)/n
print("  delta                : %+.0f points  (rule: >= +15 to keep the encoder)"%d)
print("  VERDICT              : %s"%("KEEP encoder" if d>=15 else "DELETE encoder"))
