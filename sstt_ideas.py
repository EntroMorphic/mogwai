"""Two ideas lifted from SSTT:
  (1) decompose errors into RETRIEVAL failure vs RANKING failure (finding #2)
  (2) soft signature prior instead of hard veto (negative result #2)"""
import sys,numpy as np,collections
sys.path.insert(0,".")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
ds=load_dataset('mteb/amazon_massive_intent','en'); rng=np.random.default_rng(0)
def rows(sp,neg=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[sp]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    return iot+[non[i] for i in rng.permutation(len(non))[:neg]]
IX=rows("train",1500); TS=rows("test")
XI=np.stack([embed(r["t"]) for r in IX]); YI=np.array([r["y"] for r in IX])
XT=np.stack([embed(r["t"]) for r in TS]); YT=[r["y"] for r in TS]; TT=[r["t"] for r in TS]
S=XT@XI.T
iotix=[i for i,y in enumerate(YT) if y!="none"]
order=np.argsort(-S,axis=1)

print("=== 1. SSTT finding #2: is our ceiling retrieval or ranking? ===")
print("  (over the %d real IoT commands)"%len(iotix))
print("  %-8s %-14s %s"%("K","recall@K","interpretation"))
for K in (1,3,5,10,20,50,100):
    rec=np.mean([YT[i] in set(YI[order[i,:K]]) for i in iotix])
    tag="<- our 1-NN operating point" if K==1 else ""
    print("  %-8d %-14s %s"%(K,"%.1f%%"%(100*rec),tag))
rec50=np.mean([YT[i] in set(YI[order[i,:50]]) for i in iotix])
r1=np.mean([YT[i]==YI[order[i,0]] for i in iotix])
print("\n  retrieval ceiling %.1f%%   rank-1 %.1f%%   => %.1f pts are RANKING failures"
      %(100*rec50,100*r1,100*(rec50-r1)))

print("\n=== 2. SSTT negative result #2: soft prior vs hard veto ===")
mu=XI.mean(0); XIc=XI-mu; XTc=XT-mu
CLS=sorted(set(YI.tolist()))
SIG=np.stack([np.sign(XIc[YI==c].sum(0)) for c in CLS])
SS=(XTc@SIG.T)/(np.linalg.norm(XTc,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
sig_of={c:k for k,c in enumerate(CLS)}
def run(mode,th,K=20,w=0.0):
    pred=[]
    for i in range(len(YT)):
        if S[i][order[i,0]]<=th: pred.append("none"); continue
        if mode=="rank1":
            lab=YI[order[i,0]]
        else:
            # soft prior: per-class score = best neighbour sim + w * signature agreement
            best={}
            for j in order[i,:K]:
                c=YI[j]
                if c not in best or S[i][j]>best[c]: best[c]=S[i][j]
            lab=max(best, key=lambda c: best[c]+w*SS[i][sig_of[c]])
        lab=apply_polarity(lab,TT[i])
        if mode in ("veto","softveto"):
            sg=CLS[int(SS[i].argmax())]
            if family(sg)!=family(lab): pred.append("none"); continue
        pred.append(lab)
    return taxonomy(pred,YT)
print("  at matched missed counts, wrong actuations:")
print("  %-10s %-12s %-12s %s"%("missed~","rank1+veto","soft prior","soft+veto"))
def nearest(fn,target):
    cand=[]
    for th in np.arange(0.30,0.86,0.01):
        t=fn(th); cand.append((abs(t["missed"]-target),t["wrong_actions"],t["missed"]))
    cand.sort(); return cand[0]
for target in (20,25,30,40,60):
    a=nearest(lambda th: run("veto",th),target)
    b=nearest(lambda th: run("soft",th,K=20,w=0.35),target)
    c=nearest(lambda th: run("softveto",th,K=20,w=0.35),target)
    print("  %-10d %-12s %-12s %s"%(target,"%d (m=%d)"%(a[1],a[2]),
          "%d (m=%d)"%(b[1],b[2]),"%d (m=%d)"%(c[1],c[2])))
