"""What actually ships? Accuracy vs footprint vs scan time, across dims and
float-vs-ternary. Ternary is XOR+popcount (32 dims/word), not MAC-bound."""
import sys,json,re,numpy as np
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
from datasets import load_dataset
from router_v3 import apply_polarity, taxonomy, family
from vocab2 import build, propose
rng=np.random.default_rng(0)
M=load_dataset('mteb/amazon_massive_intent','en')
def mrows(sp): return [{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in M[sp]]
tr=mrows("train"); va=mrows("validation"); te=mrows("test")
vi=[r for r in va if r["y"]!="none"]; vn=[r for r in va if r["y"]=="none"]
p=rng.permutation(len(vi))
IX=tr+[vi[i] for i in p[:len(p)//2]]+[{"t":t,"y":"iot_"+l} for t,l in json.load(open("nlu_new_iot.json"))]
TUNE=[vi[i] for i in p[len(p)//2:]]+vn[:400]
_C=re.compile(r"[^a-z0-9 ]")
def emb(t,D):
    s=" "+_C.sub(" ",t.lower())+" "; s=re.sub(r"\s+"," ",s); v=np.zeros(D,np.float32)
    for n in (3,4):
        for i in range(len(s)-n+1): v[hash(s[i:i+n])%D]+=1.0
    nr=np.linalg.norm(v)
    return v/(nr or 1.0)
CLK=240e6
print("  %-6s %-9s %-9s %-9s %-9s %-8s"%("dim","repr","iot acc","wrong","index MB","scan ms"))
for D in (256,512,1024,2048):
    XI=np.stack([emb(r["t"],D) for r in IX]); YI=np.array([r["y"] for r in IX])
    mu=XI.mean(0); CLS=sorted(set(YI.tolist()))
    SIG=np.stack([np.sign((XI-mu)[YI==c].sum(0)) for c in CLS]).astype(np.float32)
    DEC=build([r["t"] for r in IX],[r["y"] for r in IX],8,0.20)
    TI=np.sign(XI-mu).astype(np.int8)                       # ternary index
    for repr_ in ("float","ternary"):
        def prep(R):
            X=np.stack([emb(r["t"],D) for r in R])
            if repr_=="float": S=X@XI.T
            else:
                Q=np.sign(X-mu).astype(np.int8)
                S=(Q.astype(np.float32)@TI.T.astype(np.float32))/D
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
        lo,hi=(-0.5,1.0) if repr_=="ternary" else (0.30,0.85)
        b=(0,1e9)
        for th in np.arange(lo,hi,0.01):
            t=taxonomy(pred(Sd,jd,SSd,Td,th),Gd); c=t["wrong_actions"]*3+t["missed"]
            if c<b[1]: b=(round(float(th),3),c)
        pr=pred(St,jt,SSt,Tt,b[0]); t=taxonomy(pr,Gt)
        ii=[i for i,y in enumerate(Gt) if y!="none"]
        ia=100*np.mean([pr[i]==Gt[i] for i in ii])
        N=len(IX)
        if repr_=="float": mb=N*D/1e6; ms=N*D/32.2e6*1000          # int8 store, MAC-bound
        else: mb=N*D*2/8/1e6; ms=(N*(D/32)*4)/(CLK*2)*1000          # 2-bit, xor+popcount, 2 cores
        fit="" if mb<1.5 else ("  <-too big" if mb>=1.5 else "")
        print("  %-6d %-9s %-9s %-9d %-9s %-8s%s"%(D,repr_,"%.1f%%"%ia,t["wrong_actions"],
              "%.2f"%mb,"%.1f"%ms,fit))
