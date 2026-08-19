import json,sys,numpy as np
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
sys.path.insert(0,".")
from heldout import DEV,TEST
from needle.model.tokenizer import get_tokenizer
import router as R
V,D,F,L,H,S=8192,64,256,2,4,24
z=np.load("student.npz"); W={}
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

rows=[json.loads(l) for l in open("corpus.jsonl")]
keep=[r for r in rows if r["label"]!="multi"]
C=np.stack([enc(r["text"]) for r in keep])

print("=== 1. verify independence (the check I failed to do last time) ===")
CONTAM=0.80
clean=[]
for q,g in TEST:
    s=C@enc(q); j=int(s.argmax())
    if s.max()>CONTAM:
        print("  EXCLUDED %-38s (%.3f to '%s')" % (q[:38],s.max(),keep[j]["text"][:34]))
    else: clean.append((q,g))
print("  %d/%d test queries are independent (nearest-train cos <= %.2f)" % (len(clean),len(TEST),CONTAM))

print()
print("=== 2. threshold chosen on DEV only, never on TEST ===")
Cd=[(enc(q),g) for q,g in DEV]
best=(0.5,-1)
for th in np.arange(0.30,0.90,0.01):
    ok=0
    for v,g in Cd:
        s=C@v; j=int(s.argmax())
        pred=keep[j]["label"] if s.max()>th else None
        pred=None if pred=="none" else pred
        ok+= pred==g
    if ok>best[1]: best=(th,ok)
TH=best[0]; print("  encoder threshold = %.2f (dev %d/%d)" % (TH,best[1],len(DEV)))

print()
print("=== 3. head to head on the independent TEST set ===")
enc_ok=rt_ok=0
print("  %-40s %-16s %-16s" % ("query","router (0 KB)","encoder (610 KB)"))
for q,g in clean:
    v=enc(q); s=C@v; j=int(s.argmax())
    ep=keep[j]["label"] if s.max()>TH else None
    ep=None if ep=="none" else ep
    rr=R.route(q); rp=rr["calls"][0]["name"] if rr["calls"] else None
    enc_ok+= ep==g; rt_ok+= rp==g
    print("  %-40s %-16s %-16s %s" % (q[:40], rp or "refuse", ep or "refuse",
          ("" if (rp==g)==(ep==g) else ("<- encoder" if ep==g else "<- router"))))
n=len(clean)
print()
print("  deterministic router : %d/%d  (%.0f%%)   0 KB" % (rt_ok,n,100*rt_ok/n))
print("  tinyenc encoder      : %d/%d  (%.0f%%)   610 KB" % (enc_ok,n,100*enc_ok/n))
d=100*(enc_ok-rt_ok)/n
print("  delta                : %+.0f points   (pre-committed rule: >= +15 to keep the encoder)" % d)
print("  VERDICT              : %s" % ("KEEP encoder" if d>=15 else "DELETE encoder, ship deterministic"))
