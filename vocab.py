"""Decisive-token vocabulary, derived mechanically from TRAIN only.

A token is DECISIVE for class c if, across the training index, it occurs in c
and in no other IoT class. No weights, no thresholds, no top-N: the only
stipulation is that a token must not be a singleton (>=2 occurrences), which
excludes hapax noise rather than tuning anything.

At inference: if the decisive tokens present in a query all point to one class,
and that class is reachable in the index, propose it. The veto still runs
afterwards, so a proposal can still be refused.
"""
import re, collections
_CLEAN = re.compile(r"[^a-z0-9 ]")
def toks(t):
    w = _CLEAN.sub(" ", t.lower()).split()
    return set(w) | {a+"_"+b for a, b in zip(w, w[1:])}

def build(texts, labels):
    seen = collections.defaultdict(collections.Counter)     # token -> class -> count
    for t, l in zip(texts, labels):
        for k in toks(t): seen[k][l] += 1
    dec = {}
    for tok, cc in seen.items():
        iot = {c: n for c, n in cc.items() if c != "none"}
        if len(iot) != 1: continue                          # must be unique to one iot class
        c, n = next(iter(iot.items()))
        if n < 2: continue                                  # not a singleton
        dec[tok] = c
    return dec

def propose(dec, text):
    """Return the unanimous decisive class, or None."""
    hits = {dec[t] for t in toks(text) if t in dec}
    return next(iter(hits)) if len(hits) == 1 else None
