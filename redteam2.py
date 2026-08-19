import json,sys,numpy as np
exec(open("redteam1.py").read().split("rows=[json.loads")[0])
rows=[json.loads(l) for l in open("corpus.jsonl")]
keep=[r for r in rows if r["label"]!="multi"]

print("=== D. the LCVDB claim I asserted without testing ===")
print("    (I said tinyenc vectors are dense/zero-mean so need no projection)")
E=np.stack([enc(r["text"]) for r in keep])
def occ(X,tau=3):
    s=np.abs(X).max(-1,keepdims=True); s=np.where(s==0,1,s)
    nib=np.clip(np.round(X/s*7),-8,7)
    t=np.where(nib>tau,1,np.where(nib<-tau,-1,0))
    return [float(np.mean(t==c)) for c in (-1,0,1)]
o=occ(E)
print("    tinyenc trit occupancy   -1 %.3f | 0 %.3f | +1 %.3f  %s" %
      (o[0],o[1],o[2], "OK" if o[1]<0.90 and min(o[0],o[2])>0.02 else "PROBLEM"))
print("    per-dim mean |mu|        %.4f (0 = centred)" % np.abs(E.mean(0)).mean())
print("    mean pairwise cosine     %.3f (high = anisotropic, bad for thresholding)"
      % float(np.mean(E@E.T)))
sv=np.linalg.svd(E-E.mean(0),compute_uv=False)
print("    top-1 singular variance  %.1f%%" % (100*sv[0]**2/(sv**2).sum()))

print()
print("=== E. robustness: inputs nobody tested ===")
for label,q in (("empty string",""),("single space"," "),("one char","x"),
                ("very long", "turn the kitchen lights on "*20),
                ("unicode","allume la lumière de la cuisine"),
                ("digits only","13 27 4"),("repeated token","on on on on on on"),
                ("punctuation only","!!! ??? ...")):
    try:
        v=enc(q) if q.strip() else None
        if v is None: print("    %-16s -> encode SKIPPED (empty token list)" % label); continue
        n=float(np.linalg.norm(v)); s=E@v
        print("    %-16s -> |v|=%.3f  max_sim=%.3f  %s" % (label,n,s.max(),
              "nan/inf!" if not np.isfinite(v).all() else ""))
    except Exception as ex:
        print("    %-16s -> EXCEPTION %s: %s" % (label,type(ex).__name__,str(ex)[:50]))
