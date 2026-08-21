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
sp=json.load(open("massive_splits.json")); lbl=[r["y"] for r in sp["index"]]
def ev(name,f,size):
    Z={k:np.stack([f(r["t"]) for r in sp[k]]) for k in ("index","dev","test")}
    def pick(v,th):
        s=Z["index"]@v; j=int(s.argmax()); return lbl[j] if s.max()>th else "none"
    best=(0.5,-1)
    for th in np.arange(0.10,0.99,0.01):
        ok=sum(pick(Z["dev"][i],th)==sp["dev"][i]["y"] for i in range(len(sp["dev"])))
        if ok>best[1]: best=(th,ok)
    th=best[0]; ok=io=ion=0
    for i,r in enumerate(sp["test"]):
        p=pick(Z["test"][i],th); ok+= p==r["y"]
        if r["y"]!="none": ion+=1; io+= p==r["y"]
    print("  %-38s %-9s %-9s %s" % (name,"%.1f%%"%(100*ok/len(sp['test'])),"%.1f%%"%(100*io/ion),size))
    return 100*io/ion
DIM=512
def hemb(t):
    t=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; t=re.sub(r"\s+"," ",t)
    v=np.zeros(DIM)
    for n in (3,4):
        for i in range(len(t)-n+1): v[hash(t[i:i+n])%DIM]+=1.0
    return v/(np.linalg.norm(v) or 1.0)
print("  %-38s %-9s %-9s %s" % ("system","overall","iot-only","size"))
r=ev("char n-gram router",hemb,"0 KB")
a=ev("student d=64  (MASSIVE)",mk("student_massive.npz",64,256),"610 KB")
b=ev("student d=128 (MASSIVE)",mk("student_massive128.npz",128,512),"1.4 MB")
print()
print("  prediction was 89.1%%  -> actual %.1f%%  (%s)" % (b,"within 1 pt" if abs(b-89.1)<1 else "off by %.1f"%(b-89.1)))
print("  d=128 vs router: %+.1f pts   (rule: >= +15 to justify shipping it)" % (b-r))
