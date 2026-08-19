"""TriX-style emergent routing applied to the parameter-free router.
signature = sign(sum of centred class members). No training, 2-bit storage."""
import json,numpy as np,re,collections
sp=json.load(open("massive_splits.json"))
DIM=512
def hemb(t):
    t=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; t=re.sub(r"\s+"," ",t)
    v=np.zeros(DIM)
    for n in (3,4):
        for i in range(len(t)-n+1): v[hash(t[i:i+n])%DIM]+=1.0
    return v/(np.linalg.norm(v) or 1.0)
X={k:np.stack([hemb(r["t"]) for r in sp[k]]) for k in ("index","dev","test")}
Y={k:[r["y"] for r in sp[k]] for k in ("index","dev","test")}
iot=[i for i,y in enumerate(Y["test"]) if y!="none"]
mu=X["index"].mean(0)                      # centre: makes sign() discriminative
Xc={k:X[k]-mu for k in X}
CLS=sorted(set(Y["index"]))

def sig_route(k_tiles,rng_seed=0):
    """k ternary signatures per class (k=1 is a plain class signature)."""
    rng=np.random.default_rng(rng_seed); sigs=[]; slab=[]
    for c in CLS:
        idx=[i for i,y in enumerate(Y["index"]) if y==c]
        if k_tiles==1: groups=[idx]
        else:
            p=rng.permutation(idx); groups=[g for g in np.array_split(p,min(k_tiles,len(idx))) if len(g)]
        for g in groups:
            s=np.sign(Xc["index"][list(g)].sum(0)); sigs.append(s); slab.append(c)
    return np.stack(sigs), slab

def tune_and_score(score_fn,labels):
    best=(0.0,-1)
    Sd=score_fn("dev")
    for th in np.arange(-0.5,1.0,0.01):
        ok=0
        for i in range(len(Y["dev"])):
            j=int(Sd[i].argmax()); p=labels[j] if Sd[i][j]>th else "none"
            ok+= p==Y["dev"][i]
        if ok>best[1]: best=(th,ok)
    th=best[0]; St=score_fn("test")
    corr=[]
    for i in iot:
        j=int(St[i].argmax()); p=labels[j] if St[i][j]>th else "none"
        corr.append(float(p==Y["test"][i]))
    return np.array(corr)

res={}
# baseline: float NN over all 2269
lab_nn=Y["index"]
res["NN over 2269 float vectors"]=(tune_and_score(lambda k: X[k]@X["index"].T, lab_nn),
                                   "%.0f KB"%(2269*DIM*4/1024))
# ternary NN: 2-bit index
Tq=np.sign(Xc["index"])
res["NN, ternary index (2-bit)"]=(tune_and_score(
    lambda k: (Xc[k]@Tq.T)/(np.linalg.norm(Xc[k],axis=1,keepdims=True)*np.sqrt(DIM)+1e-9), lab_nn),
    "%.0f KB"%(2269*DIM*2/8/1024))
# TriX signatures
for kt in (1,2,4,8,16,32):
    Sg,sl=sig_route(kt)
    res["TriX signatures, %d tile%s/class"%(kt,"" if kt==1 else "s")]=(tune_and_score(
        lambda k,Sg=Sg: (Xc[k]@Sg.T)/(np.linalg.norm(Xc[k],axis=1,keepdims=True)*np.sqrt(DIM)+1e-9), sl),
        "%.1f KB"%(len(Sg)*DIM*2/8/1024))
n=len(iot); rng=np.random.default_rng(0); B=10000; boot=rng.integers(0,n,(B,n))
base=res["NN over 2269 float vectors"][0]
print("  n=%d iot test examples\n"%n)
print("  %-34s %-8s %-18s %-9s %s"%("routing scheme","iot acc","95% CI","index","vs NN"))
for kname,(c,sz) in res.items():
    bs=c[boot].mean(1); d=(c-base)[boot].mean(1)
    lo,hi=100*np.percentile(d,2.5),100*np.percentile(d,97.5)
    tag="" if kname.startswith("NN over") else ("%+.1f [%+.1f,%+.1f]"%(100*(c.mean()-base.mean()),lo,hi))
    print("  %-34s %-8s [%.1f, %.1f]      %-9s %s"%(kname,"%.1f%%"%(100*c.mean()),
          100*np.percentile(bs,2.5),100*np.percentile(bs,97.5),sz,tag))
