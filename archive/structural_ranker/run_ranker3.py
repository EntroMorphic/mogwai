import sys,numpy as np
sys.path.insert(0,".")
exec(open("run_ranker.py").read().split("def predict")[0])
from ranker import rank, SPEC
def predict(th,mode,K=20,alpha=0.25,veto="post"):
    out=[]
    for i in range(len(YT)):
        if S[i][order[i,0]]<=th: out.append("none"); continue
        raw=YI[order[i,0]]
        lab = raw if mode=="rank1" else rank(list(YI[order[i,:K]]),[S[i][j] for j in order[i,:K]],TT[i],alpha)
        lab=apply_polarity(lab,TT[i])
        sg=family(CLS[int(SS[i].argmax())])
        if veto=="post" and sg!=family(lab): out.append("none"); continue
        if veto=="both" and (sg!=family(lab) or sg!=family(apply_polarity(raw,TT[i]))):
            out.append("none"); continue
        out.append(lab)
    return out
def tax(p): return taxonomy(p,YT)
def acc(p): return 100*np.mean([p[i]==YT[i] for i in iotix])
print("  === is the ranker defeating the veto? (th=0.55) ===")
for mode,vt,lab in (("rank1","post","1-NN + veto"),("struct","post","struct + veto(post)"),
                    ("struct","both","struct + veto(both)"),("rank1","none","1-NN no veto"),
                    ("struct","none","struct no veto")):
    p=predict(0.55,mode,veto=vt); t=tax(p)
    print("  %-24s iot %.1f%%  wrong %-4d missed %d"%(lab,acc(p),t["wrong_actions"],t["missed"]))
print("\n  === operating curves, matched missed ===")
def curve(mode,vt):
    o=[]
    for th in np.arange(0.30,0.86,0.02):
        p=predict(th,mode,veto=vt); t=tax(p); o.append((t["missed"],t["wrong_actions"],acc(p)))
    return o
A=curve("rank1","post"); B=curve("struct","post"); C=curve("struct","both")
print("  %-9s %-18s %-18s %s"%("missed~","1-NN+veto","struct+veto","struct+veto(both)"))
for tg in (20,25,30,40,60):
    r=[sorted(x,key=lambda z:abs(z[0]-tg))[0] for x in (A,B,C)]
    print("  %-9d %-18s %-18s %s"%(tg,*["%d wrong (m=%d, %.0f%%)"%(x[1],x[0],x[2]) for x in r]))
