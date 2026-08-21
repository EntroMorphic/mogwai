import json, sys, numpy as np, jax, jax.numpy as jnp
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from needle.model.tokenizer import get_tokenizer
V,D,F,L,H,S = 8192,64,256,2,4,24
z=np.load("student.npz")
p={"emb":jnp.asarray(z["emb"]),"pos":jnp.asarray(z["pos"])}
for i in range(L): p[f"b{i}"]={k:jnp.asarray(z[f"b{i}.{k}"]) for k in ("q","k","v","o","f1","f2","g1","g2")}
tok=get_tokenizer(V)
def rms(x,g): return g*x*jax.lax.rsqrt(jnp.mean(x**2,-1,keepdims=True)+1e-6)
def encode(p,ids,m):
    x=p["emb"][ids]+p["pos"][None,:ids.shape[1]]; big=-1e9*(1.0-m)[:,None,None,:]
    for i in range(L):
        w=p[f"b{i}"]; h=rms(x,w["g1"]); B,Tn,_=h.shape
        q=(h@w["q"]).reshape(B,Tn,H,D//H).transpose(0,2,1,3)
        k=(h@w["k"]).reshape(B,Tn,H,D//H).transpose(0,2,1,3)
        v=(h@w["v"]).reshape(B,Tn,H,D//H).transpose(0,2,1,3)
        a=jax.nn.softmax(q@k.transpose(0,1,3,2)/np.sqrt(D//H)+big,-1)
        x=x+(a@v).transpose(0,2,1,3).reshape(B,Tn,D)@w["o"]
        x=x+jax.nn.relu(rms(x,w["g2"])@w["f1"])@w["f2"]
    e=(x*m[...,None]).sum(1)/jnp.maximum(m.sum(1,keepdims=True),1e-6)
    return e/(jnp.linalg.norm(e,axis=-1,keepdims=True)+1e-6)
def emb(texts,P=p):
    ids=np.zeros((len(texts),S),np.int32); m=np.zeros((len(texts),S),np.float32)
    for i,t in enumerate(texts):
        e=tok.encode(t)[:S]; ids[i,:len(e)]=e; m[i,:len(e)]=1
    return np.asarray(encode(P,jnp.asarray(ids),jnp.asarray(m)))

ANCHORS=[("turn the lights on in the kitchen","set_lights"),("switch the lights off","set_lights"),
 ("dim the lights","set_lights"),("brighten the living room","set_lights"),
 ("set the thermostat to 20","set_thermostat"),("make it warmer","set_thermostat"),
 ("cool the room down","set_thermostat"),
 ("read the temperature sensor","read_sensor"),("what is the humidity","read_sensor"),
 ("set pin 5 high","set_gpio"),("pull gpio 12 low","set_gpio")]
# (a) same hard set used on every earlier approach
HARD=[("it is way too bright in here","set_lights"),("i am freezing","set_thermostat"),
 ("the office is dark","set_lights"),("i am boiling hot","set_thermostat"),
 ("how warm is it right now","read_sensor"),("drive pin 7 to zero","set_gpio"),
 ("what is the capital of portugal",None),("tell me a joke",None)]
# (b) phrasings and vocabulary NOT in the training corpus
NOVEL=[("could you make the lounge less glaring","set_lights"),
 ("my hands are numb","set_thermostat"),("is the air damp in here","read_sensor"),
 ("put gpio nine to ground","set_gpio"),("this room is like an oven","set_thermostat"),
 ("i cannot read my book in here","set_lights"),("whats on telly tonight",None),
 ("remind me to buy milk",None)]
def run(P,label,thresh=0.45):
    A=emb([a for a,_ in ANCHORS],P)
    for name,SET in (("hard",HARD),("novel",NOVEL)):
        ok=0
        for q,gold in SET:
            v=emb([q],P)[0]; s=A@v; i=int(s.argmax())
            pred=ANCHORS[i][1] if s[i]>thresh else None
            ok+= (pred==gold)
            if name=="novel": print("    %-38s -> %-15s %.3f %s"%(q[:38],pred or "refuse",s[i],"OK" if pred==gold else "x"))
        print("  %-22s %-6s %d/%d" % (label,name,ok,len(SET)))
print("=== float32 student ===")
run(p,"student f32")
q8={k:(jnp.round(v/ (jnp.abs(v).max()+1e-9)*127)*(jnp.abs(v).max()+1e-9)/127) if v.ndim>=1 else v
    for k,v in p.items() if not isinstance(v,dict)}
for i in range(L):
    q8[f"b{i}"]={k:(jnp.round(v/(jnp.abs(v).max()+1e-9)*127)*(jnp.abs(v).max()+1e-9)/127) for k,v in p[f"b{i}"].items()}
print("=== int8-quantised student (what ships) ===")
run(q8,"student int8")

# ---- realistic deployment: the whole corpus is the index, not 11 anchors
print()
print("=== FULL-CORPUS INDEX (how it actually deploys) ===")
rows=[json.loads(l) for l in open("corpus.jsonl")]
lbl=[r["label"] for r in rows]
GOLD=[("it is way too bright in here","set_lights"),("i am freezing","set_thermostat"),
 ("the office is dark","set_lights"),("i am boiling hot","set_thermostat"),
 ("how warm is it right now","read_sensor"),("drive pin 7 to zero","set_gpio"),
 ("what is the capital of portugal",None),("tell me a joke",None),
 ("could you make the lounge less glaring","set_lights"),("my hands are numb","set_thermostat"),
 ("is the air damp in here","read_sensor"),("put gpio nine to ground","set_gpio"),
 ("this room is like an oven","set_thermostat"),("i cannot read my book in here","set_lights"),
 ("whats on telly tonight",None),("remind me to buy milk",None)]
def run_full(P,label,thresh=0.45):
    C=emb([r["text"] for r in rows],P)
    ok=0
    for q,g in GOLD:
        v=emb([q],P)[0]; s=C@v; j=int(s.argmax())
        pred=lbl[j] if s[j]>thresh else None
        pred=None if pred in ("none","multi") else pred
        ok+= pred==g
        print("    %-38s -> %-15s %.3f %s"%(q[:38],pred or "refuse",s[j],"OK" if pred==g else "x"))
    print("  %-22s %d/%d" % (label,ok,len(GOLD)))
run_full(p,"student f32")
run_full(q8,"student int8")
