import json,numpy as np,re,collections
sp=json.load(open("massive_splits.json"))
Y={k:[r["y"] for r in sp[k]] for k in ("index","dev","test")}
iot=[i for i,y in enumerate(Y["test"]) if y!="none"]
def make_emb(DIM,use_words,idf=None,df=None):
    def f(t):
        s=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; s=re.sub(r"\s+"," ",s)
        v=np.zeros(DIM)
        for n in (3,4):
            for i in range(len(s)-n+1): v[hash(s[i:i+n])%DIM]+=1.0
        if use_words:
            w=s.split()
            for x in w: v[hash("W"+x)%DIM]+=2.0
            for i in range(len(w)-1): v[hash("B"+w[i]+"_"+w[i+1])%DIM]+=2.0
        if idf is not None: v*=idf
        return v/(np.linalg.norm(v) or 1.0)
    return f
def build(f):
    return {k:np.stack([f(r["t"]) for r in sp[k]]) for k in ("index","dev","test")}
def run(X,labels,knn=1):
    def score(k): return X[k]@X["index"].T
    best=(0.0,-1); Sd=score("dev")
    for th in np.arange(-0.2,1.0,0.01):
        ok=0
        for i in range(len(Y["dev"])):
            o=np.argsort(-Sd[i])[:knn]
            if Sd[i][o[0]]<=th: p="none"
            else:
                votes=collections.Counter(labels[j] for j in o if Sd[i][j]>th*0.8)
                p=votes.most_common(1)[0][0] if votes else "none"
            ok+= p==Y["dev"][i]
        if ok>best[1]: best=(th,ok)
    th=best[0]; St=score("test"); corr=[]
    for i in iot:
        o=np.argsort(-St[i])[:knn]
        if St[i][o[0]]<=th: p="none"
        else:
            votes=collections.Counter(labels[j] for j in o if St[i][j]>th*0.8)
            p=votes.most_common(1)[0][0] if votes else "none"
        corr.append(float(p==Y["test"][i]))
    return np.array(corr)
res={}
base_f=make_emb(512,False); Xb=build(base_f)
res["char 3/4-gram, 1-NN, d=512  (current)"]=run(Xb,Y["index"],1)
for k in (3,5):
    res["  + %d-NN vote"%k]=run(Xb,Y["index"],k)
Xw=build(make_emb(512,True)); res["  + word uni/bigrams"]=run(Xw,Y["index"],1)
X2=build(make_emb(2048,False)); res["  + d=2048 (fewer collisions)"]=run(X2,Y["index"],1)
# IDF over the index
DIM=2048; cnt=np.zeros(DIM)
raw=make_emb(DIM,True)
tmp=np.stack([raw(r["t"]) for r in sp["index"]])
df=(tmp>0).sum(0); idf=np.log((len(tmp)+1)/(df+1))+1.0
Xi=build(make_emb(DIM,True,idf=idf)); res["  + words + d=2048 + IDF"]=run(Xi,Y["index"],1)
res["  + all of the above, 3-NN"]=run(Xi,Y["index"],3)
n=len(iot); rng=np.random.default_rng(0); B=10000; boot=rng.integers(0,n,(B,n))
base=res["char 3/4-gram, 1-NN, d=512  (current)"]
print("  n=%d\n"%n)
print("  %-42s %-8s %-18s %s"%("variant","iot acc","95% CI","vs current"))
for k,c in res.items():
    bs=c[boot].mean(1); d=(c-base)[boot].mean(1)
    lo,hi=100*np.percentile(d,2.5),100*np.percentile(d,97.5)
    tag="" if k.startswith("char") else "%+.1f [%+.1f,%+.1f] %s"%(100*(c.mean()-base.mean()),lo,hi,
        "SIG" if lo>0 else "")
    print("  %-42s %-8s [%.1f, %.1f]      %s"%(k,"%.1f%%"%(100*c.mean()),
          100*np.percentile(bs,2.5),100*np.percentile(bs,97.5),tag))
