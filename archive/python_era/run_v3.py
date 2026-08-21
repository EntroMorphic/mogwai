import sys, numpy as np, collections
sys.path.insert(0,".")
from datasets import load_dataset
from router_v3 import Router, embed, taxonomy, choose_threshold, apply_polarity
ds=load_dataset('mteb/amazon_massive_intent','en'); rng=np.random.default_rng(0)
def rows(split,neg=None):
    r=[{"t":x["text"],"y":x["label_text"] if x["label_text"].startswith("iot_") else "none"} for x in ds[split]]
    if neg is None: return r
    iot=[x for x in r if x["y"]!="none"]; non=[x for x in r if x["y"]=="none"]
    return iot+[non[i] for i in rng.permutation(len(non))[:neg]]
IX=rows("train",1500); DV=rows("validation",400); TS=rows("test")
R=Router([r["t"] for r in IX],[r["y"] for r in IX])
Sd=R.scores([r["t"] for r in DV]); St=R.scores([r["t"] for r in TS])
yi=R.y; gd=[r["y"] for r in DV]; gt=[r["y"] for r in TS]
td=[r["t"] for r in DV]; tt=[r["t"] for r in TS]
def predict(S,texts,th,pol):
    out=[]
    for i in range(S.shape[0]):
        j=int(S[i].argmax())
        if S[i][j]<=th: out.append("none"); continue
        lab=yi[j]; out.append(apply_polarity(lab,texts[i]) if pol else lab)
    return out
print("=== A. polarity check: does it fix the measured inversion? (th=0.55) ===")
for pol in (False,True):
    p=predict(St,tt,0.55,pol); t=taxonomy(p,gt)
    idx=[i for i,g in enumerate(gt) if g=="iot_wemo_on"]
    wo=np.mean([p[i]==gt[i] for i in idx])
    iotix=[i for i,g in enumerate(gt) if g!="none"]
    print("  polarity=%-5s  iot-only %.1f%%  wemo_on %.0f%%  wrong-actions %d  missed %d"
          %(pol,100*np.mean([p[i]==gt[i] for i in iotix]),100*wo,t["wrong_actions"],t["missed"]))
print()
print("=== B. per-class, with support (polarity on, th=0.55) ===")
p=predict(St,tt,0.55,True)
sup=collections.Counter(yi)
print("  %-22s %-9s %-8s %-7s %s"%("intent","train sup","test n","acc","note"))
for c in sorted(set(g for g in gt if g!="none")):
    idx=[i for i,g in enumerate(gt) if g==c]
    a=np.mean([p[i]==c for i in idx])
    note="UNMEASURED (n<20)" if len(idx)<20 else ""
    print("  %-22s %-9d %-8d %-7s %s"%(c,sup[c],len(idx),"%.0f%%"%(100*a),note))
iotix=[i for i,g in enumerate(gt) if g!="none"]
print("  %-22s %-9d %-8d %-7s"%("MEAN (demoted)",sum(sup[c] for c in sup if c!="none"),
      len(iotix),"%.1f%%"%(100*np.mean([p[i]==gt[i] for i in iotix]))))
print()
print("=== C. operating point from a stated cost ratio ===")
print("  %-26s %-8s %-15s %-12s %s"%("cost(wrong):cost(miss)","thresh","wrong actions","missed","iot acc"))
for cw,cm,lab in ((1,1,"1:1  (accuracy)"),(3,1,"3:1"),(10,1,"10:1"),(30,1,"30:1")):
    th=choose_threshold(Sd,yi,gd,td,cw,cm,True)
    pp=predict(St,tt,th,True); t=taxonomy(pp,gt)
    ia=100*np.mean([pp[i]==gt[i] for i in iotix])
    print("  %-26s %-8.2f %-15d %-12d %.1f%%"%(lab,th,t["wrong_actions"],t["missed"],ia))
