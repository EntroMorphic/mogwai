import sys,numpy as np,itertools,collections
sys.path.insert(0,"."); sys.path.insert(0,"archive/structural_ranker")
exec(open("clean_eval.py").read().split('print("  === tuning')[0])
from vocab2 import build, propose
from math import comb
TXI=[r["t"] for r in IX]; LBI=[r["y"] for r in IX]
def pred(S,o,SS,T,th,DEC):
    out=[]
    for i in range(S.shape[0]):
        if S[i][o[i,0]]<=th: out.append("none"); continue
        lab=apply_polarity(YI[o[i,0]],T[i])
        if DEC:
            p=propose(DEC,T[i])
            if p is not None: lab=apply_polarity(p,T[i])
        if family(CLS[int(SS[i].argmax())])!=family(lab): out.append("none"); continue
        out.append(lab)
    return out
wa=lambda p,g,i:(g[i]=="none" and p[i]!="none") or (g[i]!="none" and p[i]!="none" and p[i]!=g[i])
def breaks(S,o,SS,T,G,DEC,ths=(0.45,0.50,0.55,0.60)):
    tot_f=tot_k=0
    for th in ths:
        a=pred(S,o,SS,T,th,None); b=pred(S,o,SS,T,th,DEC)
        tot_f+=sum(1 for i in range(len(G)) if wa(a,G,i) and not wa(b,G,i))
        tot_k+=sum(1 for i in range(len(G)) if wa(b,G,i) and not wa(a,G,i))
    return tot_f,tot_k
print("  === DEVELOPMENT ON DEV: which strength breaks zero? ===")
print("  %-10s %-10s %-8s %-10s %-10s %s"%("min_count","min_cover","tokens","dev fixed","dev broke","verdict"))
cands=[]
for mc,mv in itertools.product((2,3,5,8),(0.0,0.02,0.05,0.10,0.20)):
    DEC=build(TXI,LBI,mc,mv)
    if not DEC: continue
    f,k=breaks(Sd,od,SSd,Td,Gd,DEC)
    v="pass" if k==0 else "break"
    if k==0 and f>0: cands.append((f,mc,mv,len(DEC)))
    print("  %-10d %-10.2f %-8d %-10d %-10d %s"%(mc,mv,len(DEC),f,k,v))
if not cands:
    print("\n  nothing passes breaks-zero on dev -> per the synthesis, we ship without it")
else:
    cands.sort(reverse=True); f,mc,mv,nt=cands[0]
    print("\n  dev-selected: min_count=%d min_cover=%.2f (%d tokens, dev fixed %d, broke 0)"%(mc,mv,nt,f))
    DEC=build(TXI,LBI,mc,mv)
    print("\n  === ONE CONFIRMATION ON TEST ===")
    tf,tk=breaks(St,ot,SSt,Tt,Gt,DEC)
    print("  test: fixed %d, broke %d  -> %s"%(tf,tk,"CONFIRMED" if tk==0 else "FAILED on test"))
    iot=[i for i,y in enumerate(Gt) if y!="none"]
    for D,lab in ((None,"retrieval + polarity + veto"),(DEC,"+ decisive vocabulary")):
        b=(0,1e9)
        for th in np.arange(0.30,0.85,0.01):
            p=pred(Sd,od,SSd,Td,th,D); t=taxonomy(p,Gd); c=t["wrong_actions"]*3+t["missed"]
            if c<b[1]: b=(round(float(th),2),c)
        p=pred(St,ot,SSt,Tt,b[0],D); t=taxonomy(p,Gt)
        ia=100*np.mean([p[i]==Gt[i] for i in iot])
        print("  %-34s %.1f%%  wrong %-3d missed %-3d  (%.1f%% of 96.8%% ceiling)"
              %(lab,ia,t["wrong_actions"],t["missed"],100*ia/96.8))
