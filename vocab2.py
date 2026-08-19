"""Decisive vocabulary, strength selected on DEV (that is what dev is for).

v1 admitted any token unique to one class with >=2 occurrences: 303 tokens,
mostly distributional accidents ("kitchen" happening to land in lightoff).
A token must also be CHARACTERISTIC of its class, not merely unique to it.
The strength of that requirement is a parameter, so it is chosen on dev and
confirmed once on test.
"""
import re, collections
_CLEAN = re.compile(r"[^a-z0-9 ]")
def toks(t):
    w = _CLEAN.sub(" ", t.lower()).split()
    return set(w) | {a+"_"+b for a, b in zip(w, w[1:])}

def build(texts, labels, min_count, min_cover):
    seen = collections.defaultdict(collections.Counter)
    n_c = collections.Counter(labels)
    for t, l in zip(texts, labels):
        for k in toks(t): seen[k][l] += 1
    dec = {}
    for tok, cc in seen.items():
        iot = {c: n for c, n in cc.items() if c != "none"}
        if len(iot) != 1: continue
        c, n = next(iter(iot.items()))
        if n < min_count: continue
        if cc.get("none", 0) > 0: continue           # must not occur in out-of-domain
        if n / max(n_c[c], 1) < min_cover: continue  # must be characteristic of the class
        dec[tok] = c
    return dec

def propose(dec, text):
    hits = {dec[t] for t in toks(text) if t in dec}
    return next(iter(hits)) if len(hits) == 1 else None
