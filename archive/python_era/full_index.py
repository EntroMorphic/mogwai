"""The lever every LMM cycle pointed at: more index data, not more mechanism.
We have been indexing 769 iot + a 1500 SUBSAMPLE of negatives. MASSIVE train
has 10,745 negatives. Retrieval recall is already 99.5%, so this cannot help
accuracy - but it should help REFUSAL, which is where the wrong actuations are."""
import sys,numpy as np
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
from vocab2 import build, propose
ds=load_dataset('mteb/amazon_massive_intent','en')
def rows(sp,neg=None,rng=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[sp]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    return iot+[non[i] for i in rng.permutation(len(non))[:neg]]
DV=rows("validation",400,np.random.default_rng(0)); TS=rows("test")
for NEG,lab in ((1500,"1500 negatives (current)"),(None,"ALL negatives (10,745)")):
    rng=np.random.default_rng(0)
    IX=rows("train",NEG,rng) if NEG else rows("train")
    XI=np.stack([embed(r["t"]) for r in IX]); YI=np.array([r["y"] for r in IX])
    mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
    SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS])
    DEC=build([r["t"] for r in IX],[r["y"] for r in IX],8,0.20)
    def prep(R):
        X=np.stack([embed(r["t"]) for r in R]); S=X@XI.T
        SS=((X-mu)@SIG.T)/(np.linalg.norm(X-mu,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
        return S,np.argsort(-S,axis=1),SS,[r["t"] for r in R],[r["y"] for r in R]
    Sd,od,SSd,Td,Gd=prep(DV); St,ot,SSt,Tt,Gt=prep(TS)
    def pred(S,o,SS,T,th):
        out=[]
        for i in range(S.shape[0]):
            if S[i][o[i,0]]<=th: out.append("none"); continue
            l=apply_polarity(YI[o[i,0]],T[i])
            p=propose(DEC,T[i])
            if p is not None: l=apply_polarity(p,T[i])
            if family(CLS[int(SS[i].argmax())])!=family(l): out.append("none"); continue
            out.append(l)
        return out
    b=(0,1e9)
    for th in np.arange(0.30,0.85,0.01):
        t=taxonomy(pred(Sd,od,SSd,Td,th),Gd); c=t["wrong_actions"]*3+t["missed"]
        if c<b[1]: b=(round(float(th),2),c)
    p=pred(St,ot,SSt,Tt,b[0]); t=taxonomy(p,Gt)
    iot=[i for i,y in enumerate(Gt) if y!="none"]
    ia=100*np.mean([p[i]==Gt[i] for i in iot])
    print("  %-28s index=%-6d iot %.1f%%  wrong %-3d missed %-3d  (%.1f%% of ceiling)"
          %(lab,len(IX),ia,t["wrong_actions"],t["missed"],100*ia/96.8))
