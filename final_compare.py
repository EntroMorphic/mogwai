import json,sys,numpy as np,re
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from needle.model.tokenizer import get_tokenizer
V,D,F,L,H,S=8192,64,256,2,4,24
tok=get_tokenizer(V)
def load(p):
    z=np.load(p); W={}
    for n in ["emb","pos"]+[f"b{i}.{k}" for i in range(L) for k in ("q","k","v","o","f1","f2","g1","g2")]:
        a=np.asarray(z[n],np.float32); s=float(np.abs(a).max())/127.0 or 1.0
        W[n]=np.clip(np.round(a/s),-127,127).astype(np.int8).astype(np.float32)*s
    return W
def mk(W):
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
def evaluate(name,Ci,Qd,Qt,size):
    def pick(v,th):
        s=Ci@v; j=int(s.argmax()); return lbl[j] if s.max()>th else "none"
    best=(0.5,-1)
    for th in np.arange(0.15,0.98,0.01):
        ok=sum(pick(Qd[i],th)==sp["dev"][i]["y"] for i in range(len(Qd)))
        if ok>best[1]: best=(th,ok)
    th=best[0]; ok=io=ion=0
    for i,r in enumerate(sp["test"]):
        p=pick(Qt[i],th); ok+= p==r["y"]
        if r["y"]!="none": ion+=1; io+= p==r["y"]
    print("  %-38s %-9s %-9s %-8s th=%.2f" % (name,"%.1f%%"%(100*ok/len(sp['test'])),
          "%.1f%%"%(100*io/ion),size,th))
    return 100*io/ion
print("  %-38s %-9s %-9s %-8s" % ("system","overall","iot-only","size"))
R=[np.stack([hemb(r["t"]) for r in sp[k]]) for k in ("index","dev","test")]
r_iot=evaluate("char n-gram router (no training)",R[0],R[1],R[2],"0 KB")
for tag,path in (("tinyenc, distilled on MY templates","archive/encoder_path/student.npz"),
                 ("tinyenc, distilled on MASSIVE train","student_massive.npz")):
    e=mk(load(path)); Z=[np.stack([e(r["t"]) for r in sp[k]]) for k in ("index","dev","test")]
    evaluate(tag,Z[0],Z[1],Z[2],"610 KB")
T=[np.load(f"t_{k}.npy") for k in ("index","dev","test")]
evaluate("MiniLM-L6 teacher (ceiling, 384d)",T[0],T[1],T[2],"22 MB")
