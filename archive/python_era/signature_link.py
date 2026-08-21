"""Signatures as a LINKING mechanism, not a classifier.
Compared on the operating curve (wrong actions vs missed) - the metric with
n=2974 resolution, rather than iot accuracy with n=220."""
import sys,numpy as np,collections
sys.path.insert(0,".")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy
ds=load_dataset('mteb/amazon_massive_intent','en'); rng=np.random.default_rng(0)
def rows(split,neg=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[split]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    return iot+[non[i] for i in rng.permutation(len(non))[:neg]]
IX=rows("train",1500); TS=rows("test")
XI=np.stack([embed(r["t"]) for r in IX]); YI=[r["y"] for r in IX]
XT=np.stack([embed(r["t"]) for r in TS]); YT=[r["y"] for r in TS]; TT=[r["t"] for r in TS]
S=XT@XI.T
mu=XI.mean(0); XIc=XI-mu; XTc=XT-mu
CLS=sorted(set(YI))
SIG=np.stack([np.sign(XIc[[i for i,y in enumerate(YI) if y==c]].sum(0)) for c in CLS])
SS=(XTc@SIG.T)/(np.linalg.norm(XTc,axis=1,keepdims=True)*np.sqrt(XI.shape[1])+1e-9)
FAM=lambda l: "none" if l=="none" else ("light" if "hue" in l else ("wemo" if "wemo" in l else l))

def curve(mode,ths):
    out=[]
    for th in ths:
        pred=[]
        for i in range(len(YT)):
            j=int(S[i].argmax())
            if S[i][j]<=th: pred.append("none"); continue
            lab=apply_polarity(YI[j],TT[i])
            if mode=="veto":
                sg=CLS[int(SS[i].argmax())]
                if FAM(sg)!=FAM(lab): pred.append("none"); continue
            elif mode=="cascade":
                # signature picks top-2 families; NN must fall inside one, else abstain
                top2={FAM(CLS[k]) for k in np.argsort(-SS[i])[:2]}
                if FAM(lab) not in top2: pred.append("none"); continue
            pred.append(lab)
        t=taxonomy(pred,YT); out.append((t["wrong_actions"],t["missed"]))
    return out
ths=np.arange(0.30,0.86,0.01)
res={m:curve(m,ths) for m in ("plain","veto","cascade")}
print("  operating curves — at a MATCHED number of missed commands,")
print("  which mechanism yields fewer wrong actuations?\n")
print("  %-10s %s"%("missed","wrong actuations  (lower is better)"))
print("  %-10s %-12s %-12s %s"%("","threshold","+ sig veto","+ sig cascade"))
for target in (13,20,25,30,40,60,80):
    row=[]
    for m in ("plain","veto","cascade"):
        cand=[(abs(ms-target),wa,ms) for wa,ms in res[m]]
        cand.sort(); row.append(cand[0])
    print("  %-10d %-12s %-12s %s"%(target,
        "%d (m=%d)"%(row[0][1],row[0][2]),"%d (m=%d)"%(row[1][1],row[1][2]),
        "%d (m=%d)"%(row[2][1],row[2][2])))
print()
best={m:min(res[m],key=lambda x:x[0]+x[1]) for m in res}
for m,(wa,ms) in best.items():
    print("  %-9s best combined: wrong=%d missed=%d total=%d"%(m,wa,ms,wa+ms))

print()
print("  === significance: paired, at a fixed threshold ===")
def preds(mode,th):
    out=[]
    for i in range(len(YT)):
        j=int(S[i].argmax())
        if S[i][j]<=th: out.append("none"); continue
        lab=apply_polarity(YI[j],TT[i])
        if mode=="veto":
            sg=CLS[int(SS[i].argmax())]
            if FAM(sg)!=FAM(lab): out.append("none"); continue
        out.append(lab)
    return out
for th in (0.50,0.55,0.60):
    a=preds("plain",th); b=preds("veto",th)
    # McNemar on the "wrong action" event
    wa=lambda p,i: (YT[i]=="none" and p[i]!="none") or (YT[i]!="none" and p[i]!="none" and p[i]!=YT[i])
    b01=sum(1 for i in range(len(YT)) if wa(a,i) and not wa(b,i))   # veto fixed
    b10=sum(1 for i in range(len(YT)) if wa(b,i) and not wa(a,i))   # veto broke
    ms=lambda p,i: YT[i]!="none" and p[i]=="none"
    m01=sum(1 for i in range(len(YT)) if ms(b,i) and not ms(a,i))   # new misses
    from math import comb
    n=b01+b10
    p2=sum(comb(n,k) for k in range(min(b01,b10)+1))/2**n*2 if n else 1.0
    print("  th=%.2f  veto fixed %d wrong actions, broke %d, cost %d new misses  (exact p=%.4f)"
          %(th,b01,b10,m01,min(p2,1.0)))
