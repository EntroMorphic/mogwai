import json,sys,numpy as np
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from needle.model.tokenizer import get_tokenizer
V,D,F,L,H,S=8192,64,256,2,4,24
def hmat(n):
    Hm=np.array([[1.0]],np.float32)
    while Hm.shape[0]<n: Hm=np.block([[Hm,Hm],[Hm,-Hm]])
    return Hm/np.sqrt(n)
Hd=hmat(D); tok=get_tokenizer(V)
def load(f,keys):
    z=np.load(f); W={}
    for n in ["emb","pos"]+[f"b{i}.{k}" for i in range(L) for k in keys]:
        a=np.asarray(z[n],np.float32); s=float(np.abs(a).max())/127.0 or 1.0
        W[n]=np.clip(np.round(a/s),-127,127).astype(np.int8).astype(np.float32)*s
    return W
def enc(W,text,hada):
    ids=tok.encode(text)[:S]; n=len(ids)
    x=W["emb"][ids]+W["pos"][:n]
    for l in range(L):
        h=W[f"b{l}.g1"]*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
        q=(h@W[f"b{l}.q"]).reshape(n,H,D//H); k=(h@W[f"b{l}.k"]).reshape(n,H,D//H)
        v=(h@W[f"b{l}.v"]).reshape(n,H,D//H)
        a=np.einsum('ihd,jhd->hij',q,k)/np.sqrt(D//H)
        a=np.exp(a-a.max(-1,keepdims=True)); a/=a.sum(-1,keepdims=True)
        x=x+np.einsum('hij,jhd->ihd',a,v).reshape(n,D)@W[f"b{l}.o"]
        h=W[f"b{l}.g2"]*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
        if hada:
            z=(W[f"b{l}.d1"]*h)@Hd
            z=z/(1+np.exp(-W[f"b{l}.d2"]*z))*0+ (W[f"b{l}.d2"]*z)/(1+np.exp(-(W[f"b{l}.d2"]*z)))
            x=x+W[f"b{l}.d3"]*(z@Hd)
        else:
            x=x+np.maximum(h@W[f"b{l}.f1"],0)@W[f"b{l}.f2"]
    e=x.mean(0); return e/(np.linalg.norm(e)+1e-9)
rows=[json.loads(l) for l in open("corpus.jsonl")]
keep=[r for r in rows if r["label"]!="multi"]
GOLD=[("it is way too bright in here","set_lights"),("i am freezing","set_thermostat"),
 ("the office is dark","set_lights"),("i am boiling hot","set_thermostat"),
 ("how warm is it right now","read_sensor"),("drive pin 7 to zero","set_gpio"),
 ("what is the capital of portugal",None),("tell me a joke",None),
 ("could you make the lounge less glaring","set_lights"),("my hands are numb","set_thermostat"),
 ("is the air damp in here","read_sensor"),("put gpio nine to ground","set_gpio"),
 ("this room is like an oven","set_thermostat"),("i cannot read my book in here","set_lights"),
 ("whats on telly tonight",None),("remind me to buy milk",None)]
for f,keys,hada,name in (("student.npz",("q","k","v","o","f1","f2","g1","g2"),False,"dense FFN"),
                          ("student_hada.npz",("q","k","v","o","d1","d2","d3","g1","g2"),True,"Hadamard MLP")):
    W=load(f,keys)
    C=np.stack([enc(W,r["text"],hada) for r in keep])
    ok=0
    for q,g in GOLD:
        v=enc(W,q,hada); s=C@v; j=int(s.argmax())
        pred=keep[j]["label"] if s[j]>0.54 else None
        pred=None if pred=="none" else pred
        ok+= pred==g
    print("  %-16s routing %d/16" % (name,ok))
print()
mac_dense=2*D*F; 
mul_h=3*D; add_h=2*D*int(np.log2(D))
print("  per-token FFN cost:")
print("    dense    : %d MACs" % mac_dense)
print("    hadamard : %d multiplies + %d add/sub  (%.0fx fewer ops)" % (mul_h,add_h,mac_dense/(mul_h+add_h)))
