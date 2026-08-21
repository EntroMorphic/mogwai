import json,sys,numpy as np,re
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from needle.model.tokenizer import get_tokenizer
tok=get_tokenizer(8192); S=24
def mk(path,D,F,L=2,H=4):
    z=np.load(path); W={}
    for n in ["emb","pos"]+[f"b{i}.{k}" for i in range(L) for k in ("q","k","v","o","f1","f2","g1","g2")]:
        a=np.asarray(z[n],np.float32); s=float(np.abs(a).max())/127.0 or 1.0
        W[n]=np.clip(np.round(a/s),-127,127).astype(np.int8).astype(np.float32)*s
    def enc(t):
        ids=tok.encode(t)[:S]; n=len(ids)
        if n==0: return np.zeros(D)
        x=W["emb"][ids]+W["pos"][:n]
        for l in range(L):
            h=W[f"b{l}.g1"]*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
            q=(h@W[f"b{l}.q"]).reshape(n,H,D//H);k=(h@W[f"b{l}.k"]).reshape(n,H,D//H)
            v=(h@W[f"b{l}.v"]).reshape(n,H,D//H)
            a=np.einsum('ihd,jhd->hij',q,k)/np.sqrt(D//H)
            a=np.exp(a-a.max(-1,keepdims=True)); a/=a.sum(-1,keepdims=True)
            x=x+np.einsum('hij,jhd->ihd',a,v).reshape(n,D)@W[f"b{l}.o"]
            h=W[f"b{l}.g2"]*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
            x=x+np.maximum(h@W[f"b{l}.f1"],0)@W[f"b{l}.f2"]
        e=x.mean(0); return e/(np.linalg.norm(e)+1e-9)
    return enc
DIM=512
def hemb(t):
    t=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; t=re.sub(r"\s+"," ",t)
    v=np.zeros(DIM)
    for n in (3,4):
        for i in range(len(t)-n+1): v[hash(t[i:i+n])%DIM]+=1.0
    return v/(np.linalg.norm(v) or 1.0)
sp=json.load(open("massive_splits.json")); lbl=[r["y"] for r in sp["index"]]
iot_idx=[i for i,r in enumerate(sp["test"]) if r["y"]!="none"]
def correctness(f):
    Z={k:np.stack([f(r["t"]) for r in sp[k]]) for k in ("index","dev","test")}
    def pick(v,th):
        s=Z["index"]@v; j=int(s.argmax()); return lbl[j] if s.max()>th else "none"
    best=(0.5,-1)
    for th in np.arange(0.10,0.99,0.01):
        ok=sum(pick(Z["dev"][i],th)==sp["dev"][i]["y"] for i in range(len(sp["dev"])))
        if ok>best[1]: best=(th,ok)
    th=best[0]
    return np.array([pick(Z["test"][i],th)==sp["test"][i]["y"] for i in iot_idx],float)
SYS=[("char n-gram router",hemb,"0 KB"),
     ("student d=32",mk("student_massive32.npz",32,128),"281 KB"),
     ("student d=64",mk("student_massive.npz",64,256),"610 KB"),
     ("student d=128",mk("student_massive128.npz",128,512),"1.4 MB")]
res={}
Tt=np.load("t_test.npy"); Ti=np.load("t_index.npy"); Td=np.load("t_dev.npy")
for name,f,sz in SYS: res[name]=(correctness(f),sz)
def teach():
    def pick(v,th):
        s=Ti@v; j=int(s.argmax()); return lbl[j] if s.max()>th else "none"
    best=(0.5,-1)
    for th in np.arange(0.10,0.99,0.01):
        ok=sum(pick(Td[i],th)==sp["dev"][i]["y"] for i in range(len(sp["dev"])))
        if ok>best[1]: best=(th,ok)
    th=best[0]
    return np.array([pick(Tt[i],th)==sp["test"][i]["y"] for i in iot_idx],float)
res["MiniLM teacher 384d"]=(teach(),"22 MB")
n=len(iot_idx); rng=np.random.default_rng(0); B=10000
boot=rng.integers(0,n,(B,n))
print("  n = %d iot test examples\n" % n)
print("  %-24s %-8s %-18s %s" % ("system","iot acc","95%% CI","size"))
for k,(c,sz) in res.items():
    bs=c[boot].mean(1)
    print("  %-24s %-8s [%.1f, %.1f]      %s" % (k,"%.1f%%"%(100*c.mean()),
          100*np.percentile(bs,2.5),100*np.percentile(bs,97.5),sz))
print()
print("  paired bootstrap vs the router (positive = system beats router)")
base=res["char n-gram router"][0]
for k,(c,sz) in res.items():
    if k=="char n-gram router": continue
    d=(c-base)[boot].mean(1)
    lo,hi=100*np.percentile(d,2.5),100*np.percentile(d,97.5)
    sig="SIGNIFICANT" if lo>0 else ("significant (worse)" if hi<0 else "not significant")
    print("  %-24s %+5.1f pts   [%+.1f, %+.1f]   %s" % (k,100*(c.mean()-base.mean()),lo,hi,sig))
