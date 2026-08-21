import sys,json,numpy as np,collections
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
from datasets import load_dataset
from router_v3 import embed, apply_polarity, taxonomy, family, D
from vocab2 import build, propose
rng=np.random.default_rng(0)
M=load_dataset('mteb/amazon_massive_intent','en')
def mrows(sp): return [{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in M[sp]]
tr=mrows("train"); va=mrows("validation"); te=mrows("test")
vi=[r for r in va if r["y"]!="none"]; vn=[r for r in va if r["y"]=="none"]
p=rng.permutation(len(vi)); IDX_VAL=[vi[i] for i in p[:len(p)//2]]
TUNE=[vi[i] for i in p[len(p)//2:]]+vn[:400]
NEW=[{"t":t,"y":"iot_"+l} for t,l in json.load(open("nlu_new_iot.json"))]
NEG=[{"t":t,"y":"none"} for t in json.load(open("nlu_new_neg.json"))]
print("  new iot %d | new in-distribution negatives %d"%(len(NEW),len(NEG)))
CFG=[("C  MASSIVE train + val-iot (prev best)", tr+IDX_VAL),
     ("E  + NLU iot (478)",                     tr+IDX_VAL+NEW),
     ("F  + NLU negatives (20,899)",            tr+IDX_VAL+NEG),
     ("G  + NLU iot + NLU negatives",           tr+IDX_VAL+NEW+NEG)]
print()
print("  %-38s %-8s %-9s %-7s %-7s"%("index","size","iot acc","wrong","missed"))
for lab,IX in CFG:
    XI=np.stack([embed(r["t"]) for r in IX]).astype(np.float32); YI=np.array([r["y"] for r in IX])
    mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
    SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS]).astype(np.float32)
    DEC=build([r["t"] for r in IX],[r["y"] for r in IX],8,0.20)
    def prep(R):
        X=np.stack([embed(r["t"]) for r in R]).astype(np.float32); S=X@XI.T
        SS=((X-mu)@SIG.T)/(np.linalg.norm(X-mu,axis=1,keepdims=True)*np.sqrt(D)+1e-9)
        return S,S.argmax(1),SS,[r["t"] for r in R],[r["y"] for r in R]
    Sd,jd,SSd,Td,Gd=prep(TUNE); St,jt,SSt,Tt,Gt=prep(te)
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
    pr=pred(St,jt,SSt,Tt,b[0]); t=taxonomy(pr,Gt)
    ii=[i for i,y in enumerate(Gt) if y!="none"]
    ia=100*np.mean([pr[i]==Gt[i] for i in ii])
    print("  %-38s %-8d %-9s %-7d %-7d"%(lab,len(IX),"%.1f%%"%ia,t["wrong_actions"],t["missed"]))
