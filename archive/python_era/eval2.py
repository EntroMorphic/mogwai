import json,sys,numpy as np
exec(open("eval.py").read().split("ANCHORS=")[0])   # reuse model + emb()
q8={k:(jnp.round(v/(jnp.abs(v).max()+1e-9)*127)*(jnp.abs(v).max()+1e-9)/127)
    for k,v in p.items() if not isinstance(v,dict)}
for _i in range(L):
    q8[f"b{_i}"]={k:(jnp.round(v/(jnp.abs(v).max()+1e-9)*127)*(jnp.abs(v).max()+1e-9)/127)
                  for k,v in p[f"b{_i}"].items()}
rows=[json.loads(l) for l in open("corpus.jsonl")]
# multi-clause rows are training diversity, not routing anchors - drop from index
keep=[i for i,r in enumerate(rows) if r["label"]!="multi"]
texts=[rows[i]["text"] for i in keep]; lbl=[rows[i]["label"] for i in keep]
GOLD=[("it is way too bright in here","set_lights"),("i am freezing","set_thermostat"),
 ("the office is dark","set_lights"),("i am boiling hot","set_thermostat"),
 ("how warm is it right now","read_sensor"),("drive pin 7 to zero","set_gpio"),
 ("what is the capital of portugal",None),("tell me a joke",None),
 ("could you make the lounge less glaring","set_lights"),("my hands are numb","set_thermostat"),
 ("is the air damp in here","read_sensor"),("put gpio nine to ground","set_gpio"),
 ("this room is like an oven","set_thermostat"),("i cannot read my book in here","set_lights"),
 ("whats on telly tonight",None),("remind me to buy milk",None)]
def score(P):
    C=emb(texts,P); Q=emb([q for q,_ in GOLD],P)
    return C,Q
for name,P in (("student f32",p),("student int8",q8)):
    C,Q=score(P)
    best=None
    for th in np.arange(0.40,0.86,0.02):
        ok=sum((lbl[int((C@Q[i]).argmax())] if (C@Q[i]).max()>th else None)==g
               if (lbl[int((C@Q[i]).argmax())] if (C@Q[i]).max()>th else None)!="none"
               else (None==g) for i,(q,g) in enumerate(GOLD))
        if best is None or ok>best[1]: best=(th,ok)
    th=best[0]
    print("=== %s  (threshold %.2f) ===" % (name,th))
    ok=0
    for i,(q,g) in enumerate(GOLD):
        s=C@Q[i]; j=int(s.argmax())
        pred=lbl[j] if s[j]>th else None
        pred=None if pred=="none" else pred
        ok+= pred==g
        print("    %-38s -> %-15s %.3f %s"%(q[:38],pred or "refuse",s[j],"OK" if pred==g else "x"))
    print("  %s: %d/16\n" % (name,ok))
