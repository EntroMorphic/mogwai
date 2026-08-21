"""Two things 20 experiments never looked at:
   1. per-class accuracy (support ranges 22..153 - the mean may hide a dead class)
   2. error TYPE - for a device that actuates hardware these are not equal cost"""
import json,numpy as np,re,collections,sys
sys.path.insert(0,"/Users/aaronjosserand-austin/Projects/needle")
from datasets import load_dataset
ds=load_dataset('mteb/amazon_massive_intent','en'); rng=np.random.default_rng(0)
def rows(split,neg=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[split]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    return iot+[non[i] for i in rng.permutation(len(non))[:neg]]
INDEX=rows("train",1500); DEV=rows("validation",400); TEST=rows("test")
D=2048
def V(t):
    s=" "+re.sub(r"[^a-z0-9 ]"," ",t.lower())+" "; s=re.sub(r"\s+"," ",s); v=np.zeros(D)
    for n in (3,4):
        for i in range(len(s)-n+1): v[hash(s[i:i+n])%D]+=1.0
    return v/(np.linalg.norm(v) or 1.0)
XI=np.stack([V(r["t"]) for r in INDEX]); YI=[r["y"] for r in INDEX]
XD=np.stack([V(r["t"]) for r in DEV]);   YD=[r["y"] for r in DEV]
XT=np.stack([V(r["t"]) for r in TEST]);  YT=[r["y"] for r in TEST]
SD=XD@XI.T; ST=XT@XI.T
best=(0,-1)
for th in np.arange(-0.2,1.0,0.01):
    ok=sum((YI[int(SD[i].argmax())] if SD[i].max()>th else "none")==YD[i] for i in range(len(YD)))
    if ok>best[1]: best=(th,ok)
th=best[0]
pred=[YI[int(ST[i].argmax())] if ST[i].max()>th else "none" for i in range(len(YT))]

sup=collections.Counter(YI)
print("  === 1. per-class accuracy (threshold %.2f) ==="%th)
print("  %-22s %-9s %-9s %-8s %s"%("intent","train sup","test n","acc","")); 
cls=sorted(set(y for y in YT if y!="none"))
for c in cls:
    idx=[i for i,y in enumerate(YT) if y==c]
    a=np.mean([pred[i]==c for i in idx])
    bar="#"*int(a*30)
    print("  %-22s %-9d %-9d %-8s %s"%(c,sup[c],len(idx),"%.0f%%"%(100*a),bar))
iotix=[i for i,y in enumerate(YT) if y!="none"]
print("  %-22s %-9d %-9d %-8s"%("MEAN",sum(sup[c] for c in cls),len(iotix),
      "%.1f%%"%(100*np.mean([pred[i]==YT[i] for i in iotix]))))
print()
print("  === 2. error taxonomy (all %d test) ==="%len(YT))
tax=collections.Counter()
for i,(p,y) in enumerate(zip(pred,YT)):
    if p==y: tax["correct"]+=1
    elif y=="none" and p!="none": tax["FALSE ACTUATION (fired on out-of-domain)"]+=1
    elif y!="none" and p=="none": tax["missed (refused a real command)"]+=1
    else: tax["WRONG ACTUATION (fired wrong intent)"]+=1
for k,v in tax.most_common():
    print("  %-44s %5d  %5.1f%%"%(k,v,100*v/len(YT)))
bad=tax["FALSE ACTUATION (fired on out-of-domain)"]+tax["WRONG ACTUATION (fired wrong intent)"]
print()
print("  actuations that were wrong : %d  (%.1f%% of all input)"%(bad,100*bad/len(YT)))
print("  safe abstentions           : %d"%tax["missed (refused a real command)"])
print("  ratio wrong-action : missed = 1 : %.1f"%(tax["missed (refused a real command)"]/max(bad,1)))

print()
print("  === 3. is the failure polarity? (on/off pairs) ===")
pairs=[("iot_hue_lighton","iot_hue_lightoff"),("iot_wemo_on","iot_wemo_off")]
for a,b in pairs:
    for c in (a,b):
        idx=[i for i,y in enumerate(YT) if y==c]
        conf=collections.Counter(pred[i] for i in idx)
        top=", ".join("%s:%d"%(k.replace("iot_",""),v) for k,v in conf.most_common(3))
        print("  %-22s -> %s"%(c,top))
print()
print("  === 4. operating curve: the threshold trades wrong-action for abstention ===")
print("  %-8s %-10s %-14s %-14s %s"%("thresh","acc(all)","wrong actions","safe abstain","ratio"))
for t in (0.35,0.40,0.47,0.55,0.60,0.65,0.70,0.75):
    p=[YI[int(ST[i].argmax())] if ST[i].max()>t else "none" for i in range(len(YT))]
    fa=sum(1 for i in range(len(YT)) if YT[i]=="none" and p[i]!="none")
    wa=sum(1 for i in range(len(YT)) if YT[i]!="none" and p[i]!="none" and p[i]!=YT[i])
    ms=sum(1 for i in range(len(YT)) if YT[i]!="none" and p[i]=="none")
    acc=np.mean([p[i]==YT[i] for i in range(len(YT))])
    mark=" <- accuracy-optimal" if abs(t-0.47)<0.01 else ""
    print("  %-8.2f %-10s %-14d %-14d 1:%.1f%s"%(t,"%.1f%%"%(100*acc),fa+wa,ms,ms/max(fa+wa,1),mark))
