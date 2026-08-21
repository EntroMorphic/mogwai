"""Honest protocol: tune EVERYTHING on dev, touch test once."""
import sys,numpy as np,itertools
sys.path.insert(0,".")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
from ranker_auto import AutoStruct, toks
from ranker import rank as rank_hand
ds=load_dataset('mteb/amazon_massive_intent','en'); rng=np.random.default_rng(0)
def rows(sp,neg=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[sp]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    return iot+[non[i] for i in rng.permutation(len(non))[:neg]]
IX=rows("train",1500); DV=rows("validation",400); TS=rows("test")
XI=np.stack([embed(r["t"]) for r in IX]); YI=np.array([r["y"] for r in IX])
mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS])
AS=AutoStruct([r["t"] for r in IX],[r["y"] for r in IX])
def prep(R):
    X=np.stack([embed(r["t"]) for r in R])
    S=X@XI.T; o=np.argsort(-S,axis=1)
    SS=((X-mu)@SIG.T)/(np.linalg.norm(X-mu,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
    return S,o,SS,[r["t"] for r in R],[r["y"] for r in R]
Sd,od,SSd,Td,Gd=prep(DV); St,ot,SSt,Tt,Gt=prep(TS)
def predict(S,o,SS,T,th,mode,K,alpha,veto):
    out=[]
    for i in range(S.shape[0]):
        if S[i][o[i,0]]<=th: out.append("none"); continue
        raw=YI[o[i,0]]
        if mode=="rank1": lab=raw
        elif mode=="auto": lab=AS.rank(list(YI[o[i,:K]]),[S[i][j] for j in o[i,:K]],T[i],alpha)
        else: lab=rank_hand(list(YI[o[i,:K]]),[S[i][j] for j in o[i,:K]],T[i],alpha)
        lab=apply_polarity(lab,T[i])
        sg=family(CLS[int(SS[i].argmax())])
        if veto=="post" and sg!=family(lab): out.append("none"); continue
        if veto=="both" and (sg!=family(lab) or sg!=family(apply_polarity(raw,T[i]))):
            out.append("none"); continue
        out.append(lab)
    return out
def cost(p,g,cw=3,cm=1):
    t=taxonomy(p,g); return t["wrong_actions"]*cw+t["missed"]*cm
print("  === tuning on DEV only (cost 3:1), then ONE test evaluation ===")
best={}
for mode in ("rank1","auto","hand"):
    b=(None,1e9)
    grid=itertools.product(np.arange(0.35,0.80,0.025),
                           ([20] if mode=="rank1" else [5,10,20,50]),
                           ([0.0] if mode=="rank1" else [0.1,0.2,0.3,0.5,0.8]),
                           ("post","both"))
    for th,K,al,vt in grid:
        c=cost(predict(Sd,od,SSd,Td,th,mode,K,al,vt),Gd)
        if c<b[1]: b=((round(float(th),3),K,al,vt),c)
    best[mode]=b[0]
    print("  %-6s dev-optimal: th=%.3f K=%d alpha=%.1f veto=%s"%(mode,*b[0]))
print()
iot=[i for i,y in enumerate(Gt) if y!="none"]
print("  %-32s %-9s %-8s %-8s"%("system (dev-tuned, test-once)","iot acc","wrong","missed"))
for mode,lab in (("rank1","1-NN + veto (baseline)"),("hand","hand lexicon (LEAKED)"),
                 ("auto","auto lexicon from train")):
    th,K,al,vt=best[mode]
    p=predict(St,ot,SSt,Tt,th,mode,K,al,vt); t=taxonomy(p,Gt)
    ia=100*np.mean([p[i]==Gt[i] for i in iot])
    print("  %-32s %-9s %-8d %-8d"%(lab,"%.1f%%"%ia,t["wrong_actions"],t["missed"]))
