import sys,numpy as np,collections
sys.path.insert(0,".")
exec(open("run_ranker.py").read().split("def predict")[0])
from ranker import structure, struct_score, SPEC
K=20
print("=== A. does structure extraction actually fire? (on the 220 iot queries) ===")
nd=na=nb=0
for i in iotix:
    d,a=structure(TT[i]); nd+= len(d)>0; na+= len(a)>0; nb+= len(d)>0 and len(a)>0
print("  device detected %.0f%%   action detected %.0f%%   both %.0f%%"%(100*nd/len(iotix),100*na/len(iotix),100*nb/len(iotix)))
print("\n=== B. the addressable set: correct class in top-K but not rank-1 ===")
addr=[i for i in iotix if YT[i]!=YI[order[i,0]] and YT[i] in set(YI[order[i,:K]])]
print("  %d of %d iot queries are addressable by re-ranking (%.1f%%)"%(len(addr),len(iotix),100*len(addr)/len(iotix)))
print("\n=== C. on those, is the structural signal even present & correct? ===")
good=bad=absent=0
for i in addr:
    d,a=structure(TT[i])
    if not d and not a: absent+=1; continue
    sc_true=struct_score(YT[i],d,a); sc_pred=struct_score(YI[order[i,0]],d,a)
    if sc_true>sc_pred: good+=1
    else: bad+=1
print("  structure prefers the CORRECT class : %d"%good)
print("  structure prefers the WRONG class   : %d"%bad)
print("  no structure detected               : %d"%absent)
print("\n=== D. how much candidate diversity is there in top-K? ===")
div=[len(set(YI[order[i,:K]])) for i in iotix]
print("  distinct classes among top-%d: mean %.1f, median %d"%(K,np.mean(div),int(np.median(div))))
print("\n=== E. examples the ranker should fix but doesn't ===")
shown=0
for i in addr:
    d,a=structure(TT[i])
    if shown>=6: break
    print("    %-44s gold=%-20s 1nn=%-20s dev=%s act=%s"%(TT[i][:44],YT[i].replace("iot_",""),
          str(YI[order[i,0]]).replace("iot_",""),sorted(d),sorted(a)))
    shown+=1
