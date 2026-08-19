"""Structural ranker v3: vocabulary DERIVED FROM TRAIN, not hand-written.

v2's lexicon was written by inspecting test failures - direct leakage. v3
removes the human: for each class, score tokens by log-odds against the rest
of the training index. The ranker then scores a candidate class by how much
the query's tokens favour it. No hand lexicon, no test inspection.
"""
import re, math, numpy as np, collections
_CLEAN = re.compile(r"[^a-z0-9 ]")
def toks(t):
    s = _CLEAN.sub(" ", t.lower()).split()
    return set(s) | {a+"_"+b for a,b in zip(s,s[1:])}

class AutoStruct:
    def __init__(self, texts, labels, min_df=3, top_n=40):
        self.cls = sorted(set(l for l in labels if l != "none"))
        df = collections.Counter(); per = {c: collections.Counter() for c in self.cls}
        n_c = collections.Counter()
        for t, l in zip(texts, labels):
            tk = toks(t); df.update(tk)
            if l in per: per[l].update(tk); n_c[l] += 1
        N = len(texts)
        self.w = {}
        for c in self.cls:
            sc = {}
            for tok, k in per[c].items():
                if df[tok] < min_df: continue
                p_in = (k + 0.5) / (n_c[c] + 1.0)
                p_out = (df[tok] - k + 0.5) / (N - n_c[c] + 1.0)
                lo = math.log(p_in / p_out)
                if lo > 0: sc[tok] = lo
            self.w[c] = dict(sorted(sc.items(), key=lambda kv: -kv[1])[:top_n])
        self.norm = {c: (max(v.values()) if v else 1.0) for c, v in self.w.items()}
    def score(self, label, text_toks):
        if label not in self.w: return 0.0
        w = self.w[label]
        s = sum(w[t] for t in text_toks if t in w)
        return s / self.norm[label]
    def rank(self, cands, sims, text, alpha):
        tk = toks(text); best = {}
        for c, s in zip(cands, sims):
            if c not in best or s > best[c]: best[c] = s
        return max(best, key=lambda c: best[c] + alpha * self.score(c, tk))
