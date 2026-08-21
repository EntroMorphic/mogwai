import sys,numpy as np,collections
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
exec(open("clean_eval.py").read().split('print("  === tuning')[0])
from vocab import build, propose
from math import comb
DEC=build([r["t"] for r in IX],[r["y"] for r in IX])
print("  decisive vocabulary derived from train: %d tokens"%len(DEC))
byc=collections.Counter(DEC.values())
print("  per class: "+", ".join("%s:%d"%(c.replace("iot_",""),n) for c,n in byc.most_common()))
def pred(S,o,SS,T,th,use_vocab):
    out=[]
    for i in range(S.shape[0]):
        if S[i][o[i,0]]<=th: out.append("none"); continue
        lab=apply_polarity(YI[o[i,0]],T[i])
        if use_vocab:
            p=propose(DEC,T[i])
            if p is not None: lab=apply_polarity(p,T[i])
        if family(CLS[int(SS[i].argmax())])!=family(lab): out.append("none"); continue
        out.append(lab)
    return out
wa=lambda p,g,i:(g[i]=="none" and p[i]!="none") or (g[i]!="none" and p[i]!="none" and p[i]!=g[i])
ms=lambda p,g,i: g[i]!="none" and p[i]=="none"
print("\n  === ACCEPTANCE TEST: does it break zero? (paired, fixed thresholds) ===")
ok=True
for th in (0.45,0.50,0.55,0.60):
    a=pred(St,ot,SSt,Tt,th,False); b=pred(St,ot,SSt,Tt,th,True)
    f=sum(1 for i in range(len(Gt)) if wa(a,Gt,i) and not wa(b,Gt,i))
    k=sum(1 for i in range(len(Gt)) if wa(b,Gt,i) and not wa(a,Gt,i))
    mf=sum(1 for i in range(len(Gt)) if ms(a,Gt,i) and not ms(b,Gt,i))
    mk=sum(1 for i in range(len(Gt)) if ms(b,Gt,i) and not ms(a,Gt,i))
    n=f+k; p2=min(1.0,sum(comb(n,x) for x in range(min(f,k)+1))/2**n*2) if n else 1.0
    verdict="PASS" if k==0 else "FAIL (broke %d)"%k
    if k>0: ok=False
    print("  th=%.2f  wrong: fixed %-3d broke %-3d | missed: fixed %-3d broke %-3d | p=%.4f  %s"
          %(th,f,k,mf,mk,p2,verdict))
print("\n  VERDICT: %s"%("ADMITTED - breaks zero at every threshold" if ok else "REJECTED"))
iot=[i for i,y in enumerate(Gt) if y!="none"]
print("\n  %-34s %-9s %-8s %-8s"%("system","iot acc","wrong","missed"))
for uv,lab in ((False,"retrieval + polarity + veto"),(True,"+ decisive vocabulary")):
    b=(0,1e9)
    for th in np.arange(0.30,0.85,0.01):
        p=pred(Sd,od,SSd,Td,th,uv); t=taxonomy(p,Gd)
        c=t["wrong_actions"]*3+t["missed"]
        if c<b[1]: b=(round(float(th),2),c)
    p=pred(St,ot,SSt,Tt,b[0],uv); t=taxonomy(p,Gt)
    ia=100*np.mean([p[i]==Gt[i] for i in iot])
    print("  %-34s %-9s %-8d %-8d (dev th=%.2f)"%(lab,"%.1f%%"%ia,t["wrong_actions"],t["missed"],b[0]))
    if uv: print("  %-34s %.1f%% of the 96.8%% recoverable ceiling"%("",100*ia/96.8))
