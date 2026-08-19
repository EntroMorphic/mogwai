"""Head-to-head on REAL data: MASSIVE (Amazon, Alexa-derived).
Neither system is trained on it; both index the same train utterances.
Task: route to one of 9 iot_* intents, or refuse (non-iot)."""
import sys, numpy as np, re, collections, json
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from datasets import load_dataset
from needle.model.tokenizer import get_tokenizer
V,D,F,L,H,S=8192,64,256,2,4,24
z=np.load("archive/encoder_path/student.npz"); W={}
for n in ["emb","pos"]+[f"b{i}.{k}" for i in range(L) for k in ("q","k","v","o","f1","f2","g1","g2")]:
    a=np.asarray(z[n],np.float32); s=float(np.abs(a).max())/127.0 or 1.0
    W[n]=np.clip(np.round(a/s),-127,127).astype(np.int8).astype(np.float32)*s
tok=get_tokenizer(V)
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
DIM=512
def hemb(t):
    t=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; t=re.sub(r"\s+"," ",t)
    v=np.zeros(DIM)
    for n in (3,4):
        for i in range(len(t)-n+1): v[hash(t[i:i+n])%DIM]+=1.0
    return v/(np.linalg.norm(v) or 1.0)

ds=load_dataset('mteb/amazon_massive_intent','en')
rng=np.random.default_rng(0)
def prep(split,neg_n):
    rows=[{"t":r["text"],"y":r["label_text"]} for r in ds[split]]
    iot=[r for r in rows if r["y"].startswith("iot_")]
    non=[r for r in rows if not r["y"].startswith("iot_")]
    idx=rng.permutation(len(non))[:neg_n]
    for i in idx: non[i]["y"]="none"
    return iot+[non[i] for i in idx]
INDEX=prep("train",1500); TESTS=prep("test",400); DEVS=prep("validation",200)
print("  index %d (iot %d) | dev %d | test %d (iot %d)" %
      (len(INDEX),sum(1 for r in INDEX if r["y"]!="none"),len(DEVS),
       len(TESTS),sum(1 for r in TESTS if r["y"]!="none")))
lbl=[r["y"] for r in INDEX]
Ce=np.stack([enc(r["t"]) for r in INDEX]); Cr=np.stack([hemb(r["t"]) for r in INDEX])
def pick(C,v,th):
    s=C@v; j=int(s.argmax()); p=lbl[j] if s.max()>th else "none"
    return p
def tune(C,f,rows):
    best=(0.5,-1)
    Q=[(f(r["t"]),r["y"]) for r in rows]
    for th in np.arange(0.15,0.95,0.01):
        ok=sum(pick(C,v,th)==y for v,y in Q)
        if ok>best[1]: best=(th,ok)
    return best[0]
the=tune(Ce,enc,DEVS); thr=tune(Cr,hemb,DEVS)
def score(C,f,th):
    ok=iot_ok=iot_n=0
    for r in TESTS:
        p=pick(C,f(r["t"]),th); ok+= p==r["y"]
        if r["y"]!="none": iot_n+=1; iot_ok+= p==r["y"]
    return ok/len(TESTS), iot_ok/iot_n
ae,ie=score(Ce,enc,the); ar,ir=score(Cr,hemb,thr)
print()
print("  %-34s %-10s %-14s %s" % ("system","overall","iot-only","size"))
print("  %-34s %-10s %-14s %s" % ("char n-gram router","%.1f%%"%(100*ar),"%.1f%%"%(100*ir),"0 KB"))
print("  %-34s %-10s %-14s %s" % ("tinyenc encoder (out of domain)","%.1f%%"%(100*ae),"%.1f%%"%(100*ie),"610 KB"))
print("  thresholds: router %.2f  encoder %.2f (both tuned on validation split)" % (thr,the))
d=100*(ae-ar)
print("  delta %+.1f points overall, %+.1f on iot" % (d,100*(ie-ir)))
