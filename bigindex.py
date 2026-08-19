"""Pull the data lever properly:
  + MASSIVE validation IoT into the index (half; other half tunes threshold)
  + CLINC (15k) and SNIPS (13k) as additional NEGATIVES
Nothing here is a new mechanism. Test touched once at the end."""
import sys,numpy as np,collections
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
from vocab2 import build, propose
rng=np.random.default_rng(0)
M=load_dataset('mteb/amazon_massive_intent','en')
def mrows(sp):
    return [{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in M[sp]]
tr=mrows("train"); va=mrows("validation"); te=mrows("test")
va_iot=[r for r in va if r["y"]!="none"]; va_non=[r for r in va if r["y"]=="none"]
perm=rng.permutation(len(va_iot))
va_idx=[va_iot[i] for i in perm[:len(perm)//2]]      # half into the index
va_tune=[va_iot[i] for i in perm[len(perm)//2:]]+va_non[:400]
print("  MASSIVE val iot: %d -> %d index / %d tune"%(len(va_iot),len(va_idx),len(va_tune)-400))
EXTRA=[]
for name,cfg,col in (("clinc_oos","plus","text"),("DeepPavlov/snips",None,"utterance")):
    try:
        d=load_dataset(name,cfg) if cfg else load_dataset(name)
        t=d["train"][col]; k=rng.permutation(len(t))[:12000]
        EXTRA += [{"t":t[i],"y":"none"} for i in k]
        print("  + %-22s %d negatives"%(name,len(k)))
    except Exception as e: print("  -- %s %s"%(name,str(e)[:40]))
CONFIGS=[("A baseline: 769 iot + 1500 neg",
          [r for r in tr if r["y"]!="none"]+[r for r in tr if r["y"]=="none"][:1500]),
         ("B + all MASSIVE negatives", tr),
         ("C + val iot in index", tr+va_idx),
         ("D + CLINC/SNIPS negatives", tr+va_idx+EXTRA)]
print()
print("  %-34s %-8s %-9s %-7s %-7s"%("index","size","iot acc","wrong","missed"))
for lab,IX in CONFIGS:
    XI=np.stack([embed(r["t"]) for r in IX]).astype(np.float32); YI=np.array([r["y"] for r in IX])
    mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
    SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS]).astype(np.float32)
    DEC=build([r["t"] for r in IX],[r["y"] for r in IX],8,0.20)
    def prep(R):
        X=np.stack([embed(r["t"]) for r in R]).astype(np.float32); S=X@XI.T
        SS=((X-mu)@SIG.T)/(np.linalg.norm(X-mu,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
        return S,S.argmax(1),SS,[r["t"] for r in R],[r["y"] for r in R]
    Sd,jd,SSd,Td,Gd=prep(va_tune); St,jt,SSt,Tt,Gt=prep(te)
    def pred(S,j,SS,T,th):
        out=[]
        for i in range(S.shape[0]):
            if S[i][j[i]]<=th: out.append("none"); continue
            l=apply_polarity(YI[j[i]],T[i])
            p=propose(DEC,T[i])
            if p is not None: l=apply_polarity(p,T[i])
            if family(CLS[int(SS[i].argmax())])!=family(l): out.append("none"); continue
            out.append(l)
        return out
    b=(0,1e9)
    for th in np.arange(0.30,0.85,0.01):
        t=taxonomy(pred(Sd,jd,SSd,Td,th),Gd); c=t["wrong_actions"]*3+t["missed"]
        if c<b[1]: b=(round(float(th),2),c)
    p=pred(St,jt,SSt,Tt,b[0]); t=taxonomy(p,Gt)
    iot=[i for i,y in enumerate(Gt) if y!="none"]
    ia=100*np.mean([p[i]==Gt[i] for i in iot])
    print("  %-34s %-8d %-9s %-7d %-7d"%(lab,len(IX),"%.1f%%"%ia,t["wrong_actions"],t["missed"]))
