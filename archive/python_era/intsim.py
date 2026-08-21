"""Simulate pure-integer inference and measure what precision actually costs.
Weights already int8. The question is activations: quantise per-row, accumulate
in int32, apply scales once per output - exactly what the device would do."""
import json,sys,numpy as np
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from needle.model.tokenizer import get_tokenizer
V,D,F,L,H,S=8192,64,256,2,4,24
z=np.load("student.npz")
W={}; SC={}
for n in ["emb","pos"]+[f"b{i}.{k}" for i in range(L) for k in ("q","k","v","o","f1","f2","g1","g2")]:
    a=np.asarray(z[n],np.float32); s=float(np.abs(a).max())/127.0 or 1.0
    W[n]=np.clip(np.round(a/s),-127,127).astype(np.int8); SC[n]=s
tok=get_tokenizer(V)

def qrow(x, bits):                      # per-row dynamic symmetric quantisation
    m=np.abs(x).max(-1,keepdims=True); m=np.where(m==0,1,m)
    q=np.clip(np.round(x/m*(2**(bits-1)-1)),-(2**(bits-1)-1),2**(bits-1)-1)
    return q.astype(np.int32), m/(2**(bits-1)-1)

def mm_int(x, wname, bits):
    """int matmul: quantise activations, int32 accumulate, rescale once."""
    q,s = qrow(x,bits)
    acc = q @ W[wname].astype(np.int32)          # int32 accumulation
    return acc.astype(np.float32) * s * SC[wname]

def mm_float(x,wname):
    return x @ (W[wname].astype(np.float32)*SC[wname])

def encode(text, mode, bits=8):
    ids=tok.encode(text)[:S]; n=len(ids)
    mm = (lambda x,w: mm_int(x,w,bits)) if mode!="float" else mm_float
    x = W["emb"][ids].astype(np.float32)*SC["emb"] + W["pos"][:n].astype(np.float32)*SC["pos"]
    for l in range(L):
        g1=W[f"b{l}.g1"].astype(np.float32)*SC[f"b{l}.g1"]
        g2=W[f"b{l}.g2"].astype(np.float32)*SC[f"b{l}.g2"]
        h=g1*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
        q=mm(h,f"b{l}.q"); k=mm(h,f"b{l}.k"); v=mm(h,f"b{l}.v")
        q=q.reshape(n,H,D//H); k=k.reshape(n,H,D//H); v=v.reshape(n,H,D//H)
        a=np.einsum('ihd,jhd->hij',q,k)/np.sqrt(D//H)
        a=np.exp(a-a.max(-1,keepdims=True)); a/=a.sum(-1,keepdims=True)
        o=np.einsum('hij,jhd->ihd',a,v).reshape(n,D)
        x=x+mm(o,f"b{l}.o")
        h=g2*x/np.sqrt((x**2).mean(-1,keepdims=True)+1e-6)
        f=np.maximum(mm(h,f"b{l}.f1"),0)
        x=x+mm(f,f"b{l}.f2")
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
print("  %-26s %-14s %s" % ("activation precision","cos vs float","routing"))
ref=None
for mode,bits,label in (("float",32,"float32 (baseline)"),("int",16,"int16 acts"),
                        ("int",8,"int8 acts"),("int",6,"int6 acts"),("int",4,"int4 acts")):
    C=np.stack([encode(r["text"],mode,bits) for r in keep])
    Q=np.stack([encode(q,mode,bits) for q,_ in GOLD])
    if ref is None: ref=Q.copy()
    cos=float(np.mean(np.sum(Q*ref,-1)))
    ok=0
    for i,(q,g) in enumerate(GOLD):
        s=C@Q[i]; j=int(s.argmax())
        pred=keep[j]["label"] if s[j]>0.54 else None
        pred=None if pred=="none" else pred
        ok+= pred==g
    print("  %-26s %-14.6f %d/16" % (label,cos,ok))
