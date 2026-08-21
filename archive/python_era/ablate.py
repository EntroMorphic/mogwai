"""Honest component ablation: threshold tuned on DEV per config, test once.
Also a paired McNemar at matched threshold, which needs no tuning at all."""
import sys,numpy as np
sys.path.insert(0,".")
exec(open("clean_eval.py").read().split('print("  === tuning')[0])
from math import comb
def pred(S,o,SS,T,th,pol,veto):
    out=[]
    for i in range(S.shape[0]):
        if S[i][o[i,0]]<=th: out.append("none"); continue
        lab=YI[o[i,0]]
        if pol: lab=apply_polarity(lab,T[i])
        if veto and family(CLS[int(SS[i].argmax())])!=family(lab): out.append("none"); continue
        out.append(lab)
    return out
def cost(p,g,cw=3,cm=1):
    t=taxonomy(p,g); return t["wrong_actions"]*cw+t["missed"]*cm
iot=[i for i,y in enumerate(Gt) if y!="none"]
print("  === component ablation, threshold tuned on DEV, test once (cost 3:1) ===")
print("  %-30s %-8s %-9s %-8s %s"%("config","dev th","iot acc","wrong","missed"))
base=None
for pol,veto,lab in ((False,False,"retrieval only"),(True,False,"+ polarity"),
                     (False,True,"+ veto"),(True,True,"+ polarity + veto")):
    b=(0,1e9)
    for th in np.arange(0.30,0.85,0.01):
        c=cost(pred(Sd,od,SSd,Td,th,pol,veto),Gd)
        if c<b[1]: b=(round(float(th),2),c)
    th=b[0]; p=pred(St,ot,SSt,Tt,th,pol,veto); t=taxonomy(p,Gt)
    ia=100*np.mean([p[i]==Gt[i] for i in iot])
    if base is None: base=(ia,t["wrong_actions"],t["missed"])
    print("  %-30s %-8.2f %-9s %-8d %d"%(lab,th,"%.1f%%"%ia,t["wrong_actions"],t["missed"]))
print("\n  === paired McNemar at MATCHED threshold (no tuning involved) ===")
wa=lambda p,g,i:(g[i]=="none" and p[i]!="none") or (g[i]!="none" and p[i]!="none" and p[i]!=g[i])
for th in (0.45,0.50,0.55,0.60):
    for name,(p0kw,p1kw) in (("veto",((True,False),(True,True))),
                             ("polarity",((False,True),(True,True)))):
        a=pred(St,ot,SSt,Tt,th,*p0kw); b=pred(St,ot,SSt,Tt,th,*p1kw)
        f=sum(1 for i in range(len(Gt)) if wa(a,Gt,i) and not wa(b,Gt,i))
        k=sum(1 for i in range(len(Gt)) if wa(b,Gt,i) and not wa(a,Gt,i))
        n=f+k; p2=min(1.0,sum(comb(n,x) for x in range(min(f,k)+1))/2**n*2) if n else 1.0
        print("  th=%.2f  %-9s fixed %-3d broke %-3d  p=%.4f %s"%(th,name,f,k,p2,"SIG" if p2<0.05 else ""))
