"""The vocabulary failed because it fired globally, overriding confident-correct
retrievals. Gate it to the cases it is FOR: where retrieval is uncertain
(small margin between the top class and the next distinct class)."""
import sys,numpy as np
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
from vocab2 import build, propose
from math import comb
rng=np.random.default_rng(0)
M=load_dataset('mteb/amazon_massive_intent','en')
def mrows(sp): return [{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in M[sp]]
tr=mrows("train"); va=mrows("validation"); te=mrows("test")
vi=[r for r in va if r["y"]!="none"]; vn=[r for r in va if r["y"]=="none"]
p=rng.permutation(len(vi)); IX=tr+[vi[i] for i in p[:len(p)//2]]
TUNE=[vi[i] for i in p[len(p)//2:]]+vn[:400]
XI=np.stack([embed(r["t"]) for r in IX]).astype(np.float32); YI=np.array([r["y"] for r in IX])
mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS]).astype(np.float32)
def prep(R):
    X=np.stack([embed(r["t"]) for r in R]).astype(np.float32); S=X@XI.T
    o=np.argsort(-S,axis=1)[:,:60]
    SS=((X-mu)@SIG.T)/(np.linalg.norm(X-mu,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
    return S,o,SS,[r["t"] for r in R],[r["y"] for r in R]
Sd,od,SSd,Td,Gd=prep(TUNE); St,ot,SSt,Tt,Gt=prep(te)
TXI=[r["t"] for r in IX]; LBI=[r["y"] for r in IX]
def margin(S,o,i):
    top=YI[o[i,0]]; s0=S[i][o[i,0]]
    for j in o[i,1:]:
        if YI[j]!=top: return s0-S[i][j]
    return s0
def pred(S,o,SS,T,th,DEC,gate):
    out=[]
    for i in range(S.shape[0]):
        if S[i][o[i,0]]<=th: out.append("none"); continue
        l=apply_polarity(YI[o[i,0]],T[i])
        if DEC is not None and (gate is None or margin(S,o,i)<gate):
            q=propose(DEC,T[i])
            if q is not None: l=apply_polarity(q,T[i])
        if family(CLS[int(SS[i].argmax())])!=family(l): out.append("none"); continue
        out.append(l)
    return out
wa=lambda p,g,i:(g[i]=="none" and p[i]!="none") or (g[i]!="none" and p[i]!="none" and p[i]!=g[i])
def cost(p,g): t=taxonomy(p,g); return t["wrong_actions"]*3+t["missed"]
print("  === DEV: does margin-gating let an AGGRESSIVE vocabulary break zero? ===")
print("  %-12s %-8s %-8s %-10s %-10s %s"%("vocab","tokens","gate","dev fixed","dev broke","verdict"))
best=None
for mc,mv,tag in ((2,0.00,"aggressive"),(2,0.02,"medium"),(8,0.20,"conservative")):
    DEC=build(TXI,LBI,mc,mv)
    for gate in (0.02,0.05,0.10,0.20,None):
        f=k=0
        for th in (0.45,0.50,0.55,0.60):
            a=pred(Sd,od,SSd,Td,th,None,None); b=pred(Sd,od,SSd,Td,th,DEC,gate)
            f+=sum(1 for i in range(len(Gd)) if wa(a,Gd,i) and not wa(b,Gd,i))
            k+=sum(1 for i in range(len(Gd)) if wa(b,Gd,i) and not wa(a,Gd,i))
        v="pass" if k==0 else "break"
        if k==0 and (best is None or f>best[0]): best=(f,mc,mv,gate,len(DEC),tag)
        print("  %-12s %-8d %-8s %-10d %-10d %s"%(tag,len(DEC),str(gate),f,k,v))
print()
if best:
    f,mc,mv,gate,nt,tag=best
    print("  dev-selected: %s vocab (%d tokens), gate=%s, dev fixed %d broke 0"%(tag,nt,gate,f))
    DEC=build(TXI,LBI,mc,mv)
    tf=tk=0
    for th in (0.45,0.50,0.55,0.60):
        a=pred(St,ot,SSt,Tt,th,None,None); b=pred(St,ot,SSt,Tt,th,DEC,gate)
        tf+=sum(1 for i in range(len(Gt)) if wa(a,Gt,i) and not wa(b,Gt,i))
        tk+=sum(1 for i in range(len(Gt)) if wa(b,Gt,i) and not wa(a,Gt,i))
    print("  TEST confirmation: fixed %d broke %d -> %s"%(tf,tk,"CONFIRMED" if tk==0 else "FAILED"))
    iot=[i for i,y in enumerate(Gt) if y!="none"]
    for D_,g_,lab in ((None,None,"config C (data only)"),(DEC,gate,"+ margin-gated vocabulary")):
        b2=(0,1e9)
        for th in np.arange(0.30,0.85,0.01):
            c=cost(pred(Sd,od,SSd,Td,th,D_,g_),Gd)
            if c<b2[1]: b2=(round(float(th),2),c)
        p=pred(St,ot,SSt,Tt,b2[0],D_,g_); t=taxonomy(p,Gt)
        ia=100*np.mean([p[i]==Gt[i] for i in iot])
        print("  %-32s %.1f%%  wrong %-3d missed %-3d"%(lab,ia,t["wrong_actions"],t["missed"]))
