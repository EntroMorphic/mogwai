import sys,numpy as np,collections
sys.path.insert(0,".")
exec(open("run_ranker.py").read().split("def predict")[0])
from ranker import rank, structure, struct_score, SPEC
def predict(th,mode,K=20,alpha=0.35,veto=True):
    out=[]
    for i in range(len(YT)):
        if S[i][order[i,0]]<=th: out.append("none"); continue
        lab = YI[order[i,0]] if mode=="rank1" else rank(list(YI[order[i,:K]]),[S[i][j] for j in order[i,:K]],TT[i],alpha)
        lab=apply_polarity(lab,TT[i])
        if veto:
            sg=CLS[int(SS[i].argmax())]
            if family(sg)!=family(lab): out.append("none"); continue
        out.append(lab)
    return out
print("  === alpha sweep (th=0.55, K=20) ===")
print("  %-8s %-10s %-9s %s"%("alpha","iot acc","wrong","missed"))
for al in (0.0,0.15,0.25,0.35,0.50,0.75,1.0,1.5):
    p=predict(0.55,"struct",alpha=al); t=taxonomy(p,YT)
    ia=100*np.mean([p[i]==YT[i] for i in iotix])
    print("  %-8.2f %-10s %-9d %d"%(al,"%.1f%%"%ia,t["wrong_actions"],t["missed"]))
print("\n  === K sweep (alpha=0.35) ===")
print("  %-8s %-10s %-9s %s"%("K","iot acc","wrong","missed"))
for K in (5,10,20,50,100):
    p=predict(0.55,"struct",K=K,alpha=0.35); t=taxonomy(p,YT)
    ia=100*np.mean([p[i]==YT[i] for i in iotix])
    print("  %-8d %-10s %-9d %d"%(K,"%.1f%%"%ia,t["wrong_actions"],t["missed"]))
print("\n  === operating curve: 1-NN vs structural ranker v2 ===")
def curve(mode,**kw):
    return [(taxonomy(predict(th,mode,**kw),YT)["missed"],taxonomy(predict(th,mode,**kw),YT)["wrong_actions"]) for th in np.arange(0.30,0.86,0.02)]
A=curve("rank1"); B=curve("struct",K=20,alpha=0.35)
print("  %-10s %-16s %s"%("missed~","1-NN","structural v2"))
for tg in (20,25,30,40,60):
    a=sorted(A,key=lambda r:abs(r[0]-tg))[0]; b=sorted(B,key=lambda r:abs(r[0]-tg))[0]
    print("  %-10d %-16s %s"%(tg,"%d (m=%d)"%(a[1],a[0]),"%d (m=%d)"%(b[1],b[0])))
print("\n  === per-class @ th=0.55 ===")
p0=predict(0.55,"rank1"); p=predict(0.55,"struct",alpha=0.35)
for c in sorted(SPEC):
    idx=[i for i,y in enumerate(YT) if y==c]
    if not idx: continue
    a=100*np.mean([p0[i]==c for i in idx]); b=100*np.mean([p[i]==c for i in idx])
    print("    %-22s n=%-4d %3.0f%% -> %3.0f%%  %+.0f %s"%(c,len(idx),a,b,b-a,
          "UNMEASURED" if len(idx)<20 else (">>>" if b-a>5 else ("<<<" if b-a<-5 else ""))))
ia0=100*np.mean([p0[i]==YT[i] for i in iotix]); ia=100*np.mean([p[i]==YT[i] for i in iotix])
print("    %-22s n=%-4d %3.1f%% -> %3.1f%%  %+.1f"%("MEAN",len(iotix),ia0,ia,ia-ia0))
