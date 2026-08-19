import sys,numpy as np,collections
sys.path.insert(0,".")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
from ranker import rank, structure, struct_score, SPEC
ds=load_dataset('mteb/amazon_massive_intent','en'); rng=np.random.default_rng(0)
def rows(sp,neg=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[sp]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    return iot+[non[i] for i in rng.permutation(len(non))[:neg]]
IX=rows("train",1500); TS=rows("test")
XI=np.stack([embed(r["t"]) for r in IX]); YI=np.array([r["y"] for r in IX])
XT=np.stack([embed(r["t"]) for r in TS]); YT=[r["y"] for r in TS]; TT=[r["t"] for r in TS]
S=XT@XI.T; order=np.argsort(-S,axis=1)
mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS])
SS=((XT-mu)@SIG.T)/(np.linalg.norm(XT-mu,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
iotix=[i for i,y in enumerate(YT) if y!="none"]
def predict(th,mode,K=20,alpha=0.10,veto=True):
    out=[]
    for i in range(len(YT)):
        if S[i][order[i,0]]<=th: out.append("none"); continue
        if mode=="rank1": lab=YI[order[i,0]]
        else: lab=rank(list(YI[order[i,:K]]),[S[i][j] for j in order[i,:K]],TT[i],alpha)
        lab=apply_polarity(lab,TT[i])
        if veto:
            sg=CLS[int(SS[i].argmax())]
            if family(sg)!=family(lab): out.append("none"); continue
        out.append(lab)
    return out
def report(name,mode,**kw):
    rowsout=[]
    for th in np.arange(0.30,0.86,0.01):
        p=predict(th,mode,**kw); t=taxonomy(p,YT)
        rowsout.append((t["missed"],t["wrong_actions"],th))
    return rowsout
A=report("rank1","rank1"); B=report("struct","struct")
print("  wrong actuations at matched missed counts:")
print("  %-10s %-16s %s"%("missed~","1-NN (current)","structural ranker"))
for tg in (20,25,30,40,60):
    a=sorted(A,key=lambda r:abs(r[0]-tg))[0]; b=sorted(B,key=lambda r:abs(r[0]-tg))[0]
    print("  %-10d %-16s %s"%(tg,"%d (m=%d)"%(a[1],a[0]),"%d (m=%d)"%(b[1],b[0])))
print()
for mode,lab in (("rank1","1-NN"),("struct","structural")):
    p=predict(0.55,mode); ia=100*np.mean([p[i]==YT[i] for i in iotix]); t=taxonomy(p,YT)
    print("  th=0.55 %-12s iot %.1f%%  wrong %d  missed %d"%(lab,ia,t["wrong_actions"],t["missed"]))
print()
print("  per-class @ th=0.55 (structural):")
p=predict(0.55,"struct"); p0=predict(0.55,"rank1")
for c in sorted(SPEC):
    idx=[i for i,y in enumerate(YT) if y==c]
    if not idx: continue
    a=100*np.mean([p0[i]==c for i in idx]); b=100*np.mean([p[i]==c for i in idx])
    d=b-a
    print("    %-22s n=%-4d 1NN %3.0f%% -> struct %3.0f%%  %+.0f %s"%(c,len(idx),a,b,d,
          "UNMEASURED" if len(idx)<20 else ("<<<" if d<-5 else (">>>" if d>5 else ""))))
