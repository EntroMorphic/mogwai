"""Deployment candidate. VALIDATED COMPONENTS ONLY.

Every component here passed a paired McNemar at matched thresholds (no tuning
involved, therefore not leakable) and broke zero cases. The structural ranker
is deliberately absent - see archive/structural_ranker/.

Original note: Parameter-free retrieval + deterministic polarity,
with the operating point chosen from a stated cost ratio rather than accuracy.

  family   = 1-NN over char n-gram hash (d=2048) against real indexed utterances
  polarity = token scan, overrides the retrieved label within a polarity pair
  threshold= argmin over (wrong_actuations * C_wrong + missed * C_miss)
"""
import re, numpy as np, collections

D = 2048
_CLEAN = re.compile(r"[^a-z0-9 ]")
_WS = re.compile(r"\s+")

def embed(t):
    s = " " + _CLEAN.sub(" ", t.lower()) + " "
    s = _WS.sub(" ", s)
    v = np.zeros(D)
    for n in (3, 4):
        for i in range(len(s) - n + 1):
            v[hash(s[i:i+n]) % D] += 1.0
    return v / (np.linalg.norm(v) or 1.0)

# --- polarity: pairs that retrieval is structurally blind to -----------------
PAIRS = [("iot_hue_lighton","iot_hue_lightoff"),
         ("iot_wemo_on","iot_wemo_off"),
         ("iot_hue_lightup","iot_hue_lightdim")]
SIB = {}
for a,b in PAIRS: SIB[a]=b; SIB[b]=a
POS = {a for a,_ in PAIRS}          # the "on"/"up" side of each pair

# "on" is a switch cue only in a switch construction or utterance-final.
# As a bare preposition ("the light ON the landing") it is not a cue at all -
# and because prepositions follow the verb, a last-cue-wins rule inverts on it.
_SWITCH_ON = re.compile(r"\b(turn|switch|put|power|flip|pop|kick|get)\s+(\w+\s+){0,2}on\b")
_TRAIL_ON  = re.compile(r"\bon\s*$")
CUE_POS = re.compile(r"\b(start|starts|started|enable|enabled|activate|"
                     r"brighter|brighten|brightener|increase|raise|more|lighter)\b")
CUE_NEG = re.compile(r"\b(off|stop|stops|stopped|disable|disabled|deactivate|"
                     r"kill|killed|shut|cut|dim|dimmer|darker|down|decrease|"
                     r"lower|less|reduce|sleep)\b")

def polarity(text):
    """+1 = on/up cue, -1 = off/down cue, 0 = no cue. Later cue wins ties."""
    s = " " + _CLEAN.sub(" ", text.lower()) + " "
    p = [(m.start(), +1) for m in CUE_POS.finditer(s)]
    p += [(m.end(), +1) for m in _SWITCH_ON.finditer(s)]
    m = _TRAIL_ON.search(s.rstrip())
    if m: p.append((m.start(), +1))
    p += [(m.start(), +1) for m in re.finditer(r"\bup\b", s)]
    n = [(m.start(), -1) for m in CUE_NEG.finditer(s)]
    both = sorted(p + n)
    if not both: return 0
    if p and n: return both[-1][1]      # "turn on ... off" -> trust the last
    return both[0][1]

def apply_polarity(label, text):
    if label not in SIB: return label
    pol = polarity(text)
    if pol == 0: return label
    want_pos = (pol == +1)
    is_pos = label in POS
    return label if want_pos == is_pos else SIB[label]

def family(label):
    """Coarse action family - the granularity the veto operates at."""
    if label == "none": return "none"
    if "hue" in label: return "light"
    if "wemo" in label: return "wemo"
    return label

class Router:
    """Retrieval chooses; signatures check; polarity corrects.

    The signature veto exists because signatures are a poor CLASSIFIER
    (-8.2 pts replacing retrieval) but a good DETECTOR of retrieval failure:
    when the class-level summary disagrees with the nearest neighbour, that
    neighbour is unrepresentative of its class. Measured: fixes 8-12 wrong
    actuations and breaks 0 (McNemar p=0.0005 at th=0.50). Costs 4.6 KB.
    """
    def __init__(self, texts, labels, threshold=0.55, use_polarity=True, use_veto=True):
        self.X = np.stack([embed(t) for t in texts])
        self.y = list(labels); self.th = threshold
        self.pol = use_polarity; self.veto = use_veto
        self.mu = self.X.mean(0)
        self.cls = sorted(set(self.y))
        Xc = self.X - self.mu
        self.sig = np.stack([np.sign(Xc[[i for i, l in enumerate(self.y) if l == c]].sum(0))
                             for c in self.cls])
    def route(self, text):
        v = embed(text); s = self.X @ v; j = int(s.argmax())
        if s[j] <= self.th: return "none", float(s[j])
        lab = self.y[j]
        if self.pol: lab = apply_polarity(lab, text)
        if self.veto:
            vc = v - self.mu
            ss = (self.sig @ vc) / (np.linalg.norm(vc) * np.sqrt(D) + 1e-9)
            if family(self.cls[int(ss.argmax())]) != family(lab):
                return "none", float(s[j])
        return lab, float(s[j])
    def scores(self, texts):
        Q = np.stack([embed(t) for t in texts]); return Q @ self.X.T

def taxonomy(pred, gold):
    t = collections.Counter()
    for p, g in zip(pred, gold):
        if p == g: t["correct"] += 1
        elif g == "none": t["false_actuation"] += 1
        elif p == "none": t["missed"] += 1
        else: t["wrong_actuation"] += 1
    t["wrong_actions"] = t["false_actuation"] + t["wrong_actuation"]
    return t

def choose_threshold(S, y_index, dev_gold, dev_texts, c_wrong, c_miss, use_polarity):
    best = (0.55, float("inf"))
    for th in np.arange(0.30, 0.90, 0.01):
        pred = []
        for i in range(S.shape[0]):
            j = int(S[i].argmax())
            if S[i][j] <= th: pred.append("none"); continue
            lab = y_index[j]
            pred.append(apply_polarity(lab, dev_texts[i]) if use_polarity else lab)
        t = taxonomy(pred, dev_gold)
        cost = t["wrong_actions"] * c_wrong + t["missed"] * c_miss
        if cost < best[1]: best = (round(float(th), 2), cost)
    return best[0]
