"""RED TEAM 1: is 13/16 real, or did I leak the test set into the threshold?"""
import json,sys,numpy as np
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from needle.model.tokenizer import get_tokenizer
V,D,F,L,H,S=8192,64,256,2,4,24
z=np.load("student.npz"); W={}
for n in ["emb","pos"]+[f"b{i}.{k}" for i in range(L) for k in ("q","k","v","o","f1","f2","g1","g2")]:
    a=np.asarray(z[n],np.float32); s=float(np.abs(a).max())/127.0 or 1.0
    W[n]=np.clip(np.round(a/s),-127,127).astype(np.int8).astype(np.float32)*s
tok=get_tokenizer(V)
def enc(t):
    ids=tok.encode(t)[:S]; n=len(ids)
    x=W["emb"][ids]+W["pos"][:n]
    for l in range(L):
        h=W[f"b{l}.g1"]*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
        q=(h@W[f"b{l}.q"]).reshape(n,H,D//H);k=(h@W[f"b{l}.k"]).reshape(n,H,D//H)
        v=(h@W[f"b{l}.v"]).reshape(n,H,D//H)
        a=np.einsum('ihd,jhd->hij',q,k)/np.sqrt(D//H)
        a=np.exp(a-a.max(-1,keepdims=True)); a/=a.sum(-1,keepdims=True)
        x=x+np.einsum('hij,jhd->ihd',a,v).reshape(n,D)@W[f"b{l}.o"]
        h=W[f"b{l}.g2"]*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
        x=x+np.maximum(h@W[f"b{l}.f1"],0)@W[f"b{l}.f2"]
    e=x.mean(0); return e/(np.linalg.norm(e)+1e-9)

rows=[json.loads(l) for l in open("corpus.jsonl")]
keep=[r for r in rows if r["label"]!="multi"]
TEST=[("it is way too bright in here","set_lights"),("i am freezing","set_thermostat"),
 ("the office is dark","set_lights"),("i am boiling hot","set_thermostat"),
 ("how warm is it right now","read_sensor"),("drive pin 7 to zero","set_gpio"),
 ("what is the capital of portugal",None),("tell me a joke",None),
 ("could you make the lounge less glaring","set_lights"),("my hands are numb","set_thermostat"),
 ("is the air damp in here","read_sensor"),("put gpio nine to ground","set_gpio"),
 ("this room is like an oven","set_thermostat"),("i cannot read my book in here","set_lights"),
 ("whats on telly tonight",None),("remind me to buy milk",None)]

print("=== A. how novel are my 'hard' test queries, really? ===")
C=np.stack([enc(r["text"]) for r in keep])
for q,g in TEST:
    v=enc(q); s=C@v; j=int(s.argmax())
    print("  %-40s nearest train: %-42s %.3f" % (q[:40], keep[j]["text"][:42], s[j]))

print()
print("=== B. threshold leakage: I tuned 0.54 ON the test set ===")
# honest protocol: pick the threshold on a validation split of the CORPUS,
# never on the test queries
rng=np.random.default_rng(7); idx=rng.permutation(len(keep))
val=[keep[i] for i in idx[:400]]; idxset=set(idx[:400].tolist())
index=[keep[i] for i in range(len(keep)) if i not in idxset]
Ci=np.stack([enc(r["text"]) for r in index])
Vq=np.stack([enc(r["text"]) for r in val])
best=(0,-1)
for th in np.arange(0.30,0.95,0.01):
    ok=0
    for i,r in enumerate(val):
        s=Ci@Vq[i]; j=int(s.argmax())
        pred=index[j]["label"] if s[j]>th else None
        pred=None if pred=="none" else pred
        gold=None if r["label"]=="none" else r["label"]
        ok+= pred==gold
    if ok>best[1]: best=(th,ok)
th_honest=best[0]
print("  threshold chosen on corpus validation split : %.2f  (val acc %d/%d)" % (th_honest,best[1],len(val)))
print("  threshold I reported earlier                : 0.54  (tuned on the 16 test queries)")

print()
print("=== C. accuracy split by contamination ===")
C=np.stack([enc(r["text"]) for r in keep])
CONTAM=[]; NOVEL=[]
for q,g in TEST:
    v=enc(q); s=C@v; j=int(s.argmax())
    (CONTAM if s[j]>0.80 else NOVEL).append((q,g))
for name,SET in (("near-duplicate of training",CONTAM),("genuinely novel",NOVEL)):
    for th,lab in ((0.54,"leaked th"),(th_honest,"honest th")):
        ok=0
        for q,g in SET:
            v=enc(q); s=C@v; j=int(s.argmax())
            pred=keep[j]["label"] if s[j]>th else None
            pred=None if pred=="none" else pred
            ok+= pred==g
        print("  %-30s %-12s %d/%d" % (name,lab,ok,len(SET)))
