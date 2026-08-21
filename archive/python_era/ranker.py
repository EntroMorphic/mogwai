"""Structural ranker v2 over top-K retrieval candidates.

v1 was inert (+0.9 pts). Diagnosis: on the 28 addressable queries, structure
preferred the correct class only 14 times vs 10 wrong. Root causes found by
inspecting all 10 failures:
  - "switch" was in the PLUG lexicon; it is a verb ("switch on the roomba")
  - actions were a SET, so "change the lights to dim" tied change against dim
  - lexicon gaps: "no lights" / "cease" -> off, colour words -> change
  - alpha=0.10 too weak to overturn similarity even when structure was right
v2 returns a single best device and action under explicit precedence.
"""
import re, numpy as np

SPEC = {
 "iot_cleaning":        ("cleaner", "run"),
 "iot_coffee":          ("coffee",  "run"),
 "iot_hue_lightchange": ("light",   "change"),
 "iot_hue_lightdim":    ("light",   "dim"),
 "iot_hue_lightoff":    ("light",   "off"),
 "iot_hue_lighton":     ("light",   "on"),
 "iot_hue_lightup":     ("light",   "up"),
 "iot_wemo_off":        ("plug",    "off"),
 "iot_wemo_on":         ("plug",    "on"),
}
# device: specific appliance nouns beat the generic socket words.
DEV_ORDER = [
 ("coffee",  r"\b(coffee|brew|brewing|espresso|latte|cuppa|percolator)\b"),
 ("cleaner", r"\b(clean|cleans|cleaning|vacuum|hoover|roomba|mop|sweep|tidy)\b"),
 ("light",   r"\b(light|lights|lighting|lamp|lamps|bulb|bulbs|hue|brightness|dimmer)\b"),
 ("plug",    r"\b(plug|plugs|socket|sockets|outlet|wemo|kettle|cooker|fan|heater|charger)\b"),
]
# action: most specific first. "change ... to dim" must resolve to dim.
ACT_ORDER = [
 ("change", r"\b(pink|blue|red|green|purple|orange|yellow|white|warm|warmer|cool|cooler|"
            r"colour|color|colours|colors|scene|mood)\b"),
 ("up",     r"\b(brighter|brighten|brightener|brightest|increase|raise|lighter|turn up|up)\b"),
 ("dim",    r"\b(dim|dimmer|dimmest|darker|lower|less|reduce|soft|softer|down)\b"),
 ("off",    r"\b(off|stop|stops|disable|deactivate|kill|shut|cut|cease|sleep|darkness|out|no)\b"),
 ("on",     r"((\b(turn|switch|put|power|flip|pop|kick|get)\b[^.]{0,12}?\bon\b)|\bon\s*$|"
            r"\b(start|starts|enable|activate)\b)"),
 ("change", r"\b(change|adjust|set to)\b"),
]
_D = [(k, re.compile(v)) for k, v in DEV_ORDER]
_A = [(k, re.compile(v)) for k, v in ACT_ORDER]
_CLEAN = re.compile(r"[^a-z0-9 ]")

def structure(text):
    """Single best (device, action) under precedence. None if absent."""
    s = " " + _CLEAN.sub(" ", text.lower()) + " "
    dev = next((k for k, r in _D if r.search(s)), None)
    act = next((k for k, r in _A if r.search(s)), None)
    return dev, act

def struct_score(label, dev, act):
    if label not in SPEC: return 0.0
    d, a = SPEC[label]
    s = 0.0
    if dev: s += 1.0 if d == dev else -1.0
    if act: s += 0.8 if a == act else -0.8
    return s

def rank(cands, sims, text, alpha=0.35):
    dev, act = structure(text)
    best = {}
    for c, s in zip(cands, sims):
        if c not in best or s > best[c]: best[c] = s
    return max(best, key=lambda c: best[c] + alpha * struct_score(c, dev, act))
