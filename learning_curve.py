"""How much IoT data does 96.8% actually require? Subsample the iot index and
measure. Turns 'unreachable' into a quantity."""
import sys,numpy as np
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
from vocab2 import build, propose
M=load_dataset('mteb/amazon_massive_intent','en')
def mrows(sp): return [{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in M[sp]]
tr=mrows("train"); va=mrows("validation"); te=mrows("test")
iot_all=[r for r in tr if r["y"]!="none"]+[r for r in va if r["y"]!="none"]
neg=[r for r in tr if r["y"]=="none"]
tune=[r for r in va if r["y"]=="none"][:400]
res=[]
for frac in (0.125,0.25,0.5,0.75,1.0):
    rng=np.random.default_rng(0)
    k=int(len(iot_all)*frac); sel=rng.permutation(len(iot_all))[:k]
    IX=[iot_all[i] for i in sel]+neg
    # tune threshold on a held-out slice of the SELECTED iot + negatives
    h=max(20,k//5); tn=[iot_all[i] for i in sel[:h]]+tune
    XI=np.stack([embed(r["t"]) for r in IX]).astype(np.float32); YI=np.array([r["y"] for r in IX])
    mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
    SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS]).astype(np.float32)
    DEC=build([r["t"] for r in IX],[r["y"] for r in IX],8,0.20)
    def prep(R):
        X=np.stack([embed(r["t"]) for r in R]).astype(np.float32); S=X@XI.T
        SS=((X-mu)@SIG.T)/(np.linalg.norm(X-mu,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
        return S,S.argmax(1),SS,[r["t"] for r in R],[r["y"] for r in R]
    Sd,jd,SSd,Td,Gd=prep(tn); St,jt,SSt,Tt,Gt=prep(te)
    def pred(S,j,SS,T,th):
        o=[]
        for i in range(S.shape[0]):
            if S[i][j[i]]<=th: o.append("none"); continue
            l=apply_polarity(YI[j[i]],T[i])
            q=propose(DEC,T[i])
            if q is not None: l=apply_polarity(q,T[i])
            if family(CLS[int(SS[i].argmax())])!=family(l): o.append("none"); continue
            o.append(l)
        return o
    b=(0,1e9)
    for th in np.arange(0.30,0.85,0.01):
        t=taxonomy(pred(Sd,jd,SSd,Td,th),Gd); c=t["wrong_actions"]*3+t["missed"]
        if c<b[1]: b=(round(float(th),2),c)
    p=pred(St,jt,SSt,Tt,b[0]); t=taxonomy(p,Gt)
    ii=[i for i,y in enumerate(Gt) if y!="none"]
    ia=100*np.mean([p[i]==Gt[i] for i in ii])
    # retrieval ceiling at this index size
    o50=np.argsort(-St,axis=1)[:,:50]
    rec=100*np.mean([Gt[i] in set(YI[o50[i]]) for i in ii])
    res.append((k,ia,rec,t["wrong_actions"]))
    print("  iot index %-5d  acc %.1f%%   recall@50 %.1f%%   wrong %d"%(k,ia,rec,t["wrong_actions"]))
x=np.log([r[0] for r in res]); y=[r[1] for r in res]
A=np.polyfit(x,y,1)
print("\n  fit: acc ~ %.2f * ln(n_iot) + %.2f"%(A[0],A[1]))
for target in (92,95,96.8):
    need=np.exp((target-A[1])/A[0])
    print("  to reach %.1f%%: ~%,d iot utterances (%.1fx current)".replace(",","") % (target,int(need),need/res[-1][0]))
