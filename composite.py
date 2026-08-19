"""Composite router: multiple feature views, late fusion, optional cascade.
Evaluated on the FULL MASSIVE test split (n=2974) for tighter CIs."""
import json,numpy as np,re,collections,sys
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from datasets import load_dataset
from needle.model.tokenizer import get_tokenizer
tok=get_tokenizer(8192)
ds=load_dataset('mteb/amazon_massive_intent','en')
rng=np.random.default_rng(0)
def rows(split,neg=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[split]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    idx=rng.permutation(len(non))[:neg]
    return iot+[non[i] for i in idx]
INDEX=rows("train",1500); DEV=rows("validation",400); TEST=rows("test")   # FULL test
print("  index %d | dev %d | test %d (iot %d, none %d)"%(len(INDEX),len(DEV),len(TEST),
      sum(1 for r in TEST if r["y"]!="none"),sum(1 for r in TEST if r["y"]=="none")))
D=2048
def V_char(t):
    s=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; s=re.sub(r"\s+"," ",s); v=np.zeros(D)
    for n in (3,4):
        for i in range(len(s)-n+1): v[hash(s[i:i+n])%D]+=1.0
    return v/(np.linalg.norm(v) or 1.0)
def V_word(t):
    w=re.sub(r"[^a-z0-9 ]"," ",t.lower()).split(); v=np.zeros(D)
    for x in w: v[hash("W"+x)%D]+=1.0
    for i in range(len(w)-1): v[hash("B"+w[i]+"_"+w[i+1])%D]+=1.0
    return v/(np.linalg.norm(v) or 1.0)
def V_tok(t):
    ids=tok.encode(t)[:24]; v=np.zeros(D)
    for x in ids: v[hash("T%d"%x)%D]+=1.0
    for i in range(len(ids)-1): v[hash("P%d_%d"%(ids[i],ids[i+1]))%D]+=1.0
    return v/(np.linalg.norm(v) or 1.0)
VIEWS=[("char",V_char),("word",V_word),("tok",V_tok)]
def build(f,rs): return np.stack([f(r["t"]) for r in rs])
M={n:{"index":build(f,INDEX),"dev":build(f,DEV),"test":build(f,TEST)} for n,f in VIEWS}
YI=[r["y"] for r in INDEX]
def sims(view,split): return M[view][split]@M[view]["index"].T
S={v:{k:sims(v,k) for k in ("dev","test")} for v,_ in VIEWS}
iot_mask=np.array([r["y"]!="none" for r in INDEX])
def predict(Smat,th,restrict=None):
    out=[]
    for i in range(Smat.shape[0]):
        s=Smat[i].copy()
        if restrict is not None: s=np.where(iot_mask,s,-9e9) if restrict else s
        j=int(s.argmax()); out.append(YI[j] if s[j]>th else "none")
    return out
def tune(Sdev,ys,restrict=None,lo=-0.2,hi=1.0):
    best=(0.0,-1)
    for th in np.arange(lo,hi,0.01):
        p=predict(Sdev,th,restrict); ok=sum(a==b for a,b in zip(p,ys))
        if ok>best[1]: best=(th,ok)
    return best[0]
ydev=[r["y"] for r in DEV]; ytest=[r["y"] for r in TEST]
res={}
def record(name,pred):
    res[name]=np.array([float(a==b) for a,b in zip(pred,ytest)])
# single views
for v,_ in VIEWS:
    th=tune(S[v]["dev"],ydev); record("single view: %s"%v,predict(S[v]["test"],th))
# late fusion (sum of similarities)
for combo in (("char","word"),("char","tok"),("char","word","tok")):
    Sd=sum(S[c]["dev"] for c in combo)/len(combo)
    St=sum(S[c]["test"] for c in combo)/len(combo)
    th=tune(Sd,ydev); record("late fusion: "+"+".join(combo),predict(St,th))
# cascade: domain gate (fused) then intent (char, iot-only index)
Sd_f=sum(S[c]["dev"] for c in ("char","word","tok"))/3
St_f=sum(S[c]["test"] for c in ("char","word","tok"))/3
# gate: binary in-domain detection, tuned as such
def tune_gate(Sd):
    yb=[r["y"]!="none" for r in DEV]; best=(0.0,-1)
    for th in np.arange(-0.2,1.0,0.005):
        ok=sum((Sd[i].max()>th)==yb[i] for i in range(len(yb)))
        if ok>best[1]: best=(th,ok)
    return best[0]
gate_th=tune_gate(Sd_f); gate_c=tune_gate(S["char"]["dev"])
def cascade(Sfuse,Schar):
    out=[]
    for i in range(Sfuse.shape[0]):
        if Sfuse[i].max()<=gate_th: out.append("none"); continue
        s=np.where(iot_mask,Schar[i],-9e9); out.append(YI[int(s.argmax())])
    return out
record("cascade: fused gate -> char intent",cascade(St_f,S["char"]["test"]))
gate_th=gate_c
record("cascade: char gate -> char intent",cascade(S["char"]["test"],S["char"]["test"]))
n=len(ytest); rngb=np.random.default_rng(1); B=5000; boot=rngb.integers(0,n,(B,n))
iotix=[i for i,y in enumerate(ytest) if y!="none"]
ni=len(iotix); booti=rngb.integers(0,ni,(B,ni))
base=res["single view: char"]; basei=base[iotix]
print("\n  base rate: refusing everything scores %.1f%% overall\n"%(100*sum(1 for y in ytest if y=="none")/len(ytest)))
print("  %-38s %-9s %-9s %s"%("architecture","overall","iot-only","iot 95% CI vs char"))
for k,c in res.items():
    ci=c[iotix]; d=(ci-basei)[booti].mean(1)
    lo,hi=100*np.percentile(d,2.5),100*np.percentile(d,97.5)
    tag="" if k=="single view: char" else "%+.1f [%+.1f,%+.1f] %s"%(100*(ci.mean()-basei.mean()),lo,hi,"SIG" if lo>0 else "")
    print("  %-38s %-9s %-9s %s"%(k,"%.1f%%"%(100*c.mean()),"%.1f%%"%(100*ci.mean()),tag))
