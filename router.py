"""On-device router v2. Still zero model weights. Adds the things a *closed
world* lets you do deterministically: clause splitting, number words, negation,
config-driven gazetteer, and required-slot validation before acting."""
import re, sys, json, math
DIM = 512
def embed(t):
    t = " " + re.sub(r"[^a-z0-9 ]", " ", t.lower()) + " "
    t = re.sub(r"\s+", " ", t); v = [0.0]*DIM
    for n in (3,4):
        for i in range(len(t)-n+1): v[hash(t[i:i+n]) % DIM] += 1.0
    nrm = math.sqrt(sum(x*x for x in v)) or 1.0
    return [x/nrm for x in v]
def cos(a,b): return sum(x*y for x,y in zip(a,b))

CORPUS = [
 ("turn the lights on in the kitchen","set_lights"),("switch the lights off","set_lights"),
 ("dim the lights","set_lights"),("lights on at 50 percent","set_lights"),
 ("kill the lights in the bedroom","set_lights"),("brighten the living room","set_lights"),
 ("it is too bright in here","set_lights"),("too dark in here","set_lights"),
 ("set the thermostat to 20","set_thermostat"),("make it warmer","set_thermostat"),
 ("cool the room down","set_thermostat"),("set temperature to 18 heat","set_thermostat"),
 ("turn the heating up","set_thermostat"),("i am freezing","set_thermostat"),
 ("i am too hot","set_thermostat"),
 ("read the temperature sensor","read_sensor"),("what is the humidity","read_sensor"),
 ("check the sensor reading","read_sensor"),("get me the pressure","read_sensor"),
 ("set pin 5 high","set_gpio"),("pull gpio 12 low","set_gpio"),
 ("drive pin 2 high","set_gpio"),("set gpio 9 to 0","set_gpio"),
]
INDEX=[(embed(u),t) for u,t in CORPUS]
# device CONFIG - not open vocabulary. you know your own rooms.
ROOMS=["kitchen","bedroom","living room","garage","bathroom","office","hallway","conservatory"]
SENSORS=["temperature","humidity","pressure","light","motion"]
REQUIRED={"set_lights":["room"],"set_thermostat":["temperature"],
          "read_sensor":["sensor"],"set_gpio":["pin"]}
WORDNUM={"zero":0,"one":1,"two":2,"three":3,"four":4,"five":5,"six":6,"seven":7,"eight":8,
 "nine":9,"ten":10,"eleven":11,"twelve":12,"thirteen":13,"fourteen":14,"fifteen":15,
 "sixteen":16,"seventeen":17,"eighteen":18,"nineteen":19,"twenty":20,"thirty":30,"forty":40,
 "fifty":50,"sixty":60,"seventy":70,"eighty":80,"ninety":90,"hundred":100}
def numbers(s):
    out=[int(n) for n in re.findall(r"-?\d+",s)]
    toks=re.findall(r"[a-z]+",s); i=0
    while i<len(toks):
        if toks[i] in WORDNUM:
            v=WORDNUM[toks[i]]; j=i+1
            while j<len(toks) and toks[j] in WORDNUM and WORDNUM[toks[j]]<10:
                v+=WORDNUM[toks[j]]; j+=1
            out.append(v); i=j
        else: i+=1
    return out
NEG=re.compile(r"\b(don'?t|do not|not|never|without|instead of|leave|ignore)\b")
def clauses(q):
    parts=re.split(r"\s*(?:,|\band then\b|\bthen\b|\band\b)\s*",q)
    return [p for p in parts if len(p.split())>=2] or [q]

def route_clause(c, thresh=0.35):
    if NEG.search(c): return None                      # negated clause -> no action
    e=embed(c); score,tool=max(((cos(e,v),t) for v,t in INDEX))
    if score<thresh: return None
    cl=c.lower(); nums=numbers(cl); a={}
    if tool=="set_lights":
        a["room"]=next((r for r in ROOMS if r in cl),None)
        off=any(w in cl for w in ("off","kill","out"))
        dark="dark" in cl
        a["on"]= (not off) if not dark else True
        if nums: a["brightness"]=nums[0]
        elif off: a["brightness"]=0
        elif "bright" in cl and not dark: a["brightness"]=20
    elif tool=="set_thermostat":
        if nums: a["temperature"]=nums[0]
        elif "freez" in cl or "cold" in cl: a["temperature"]=22; a["mode"]="heat"
        elif "hot" in cl: a["temperature"]=19; a["mode"]="cool"
        for m in ("heat","cool","auto"):
            if m in cl: a["mode"]=m
    elif tool=="read_sensor":
        a["sensor"]=next((s for s in SENSORS if s in cl),None)
    elif tool=="set_gpio":
        if nums: a["pin"]=nums[0]
        a["level"]=0 if "low" in cl else 1
    a={k:v for k,v in a.items() if v is not None}
    if any(r not in a for r in REQUIRED[tool]): return None   # slot check == abstain
    return {"name":tool,"args":a,"score":round(score,3)}

def route(q):
    calls=[c for c in (route_clause(x) for x in clauses(q)) if c]
    # negation rescue: if the whole query was split and a non-negated clause exists
    if not calls:
        for c in clauses(q):
            if NEG.search(c): continue
        return {"calls":[]}
    return {"calls":calls}

if __name__=="__main__":
    for q in json.load(open(sys.argv[1])):
        r=route(q)
        c=', '.join('%s(%s)'%(x['name'],','.join('%s=%s'%kv for kv in x['args'].items())) for x in r['calls']) or 'refuse'
        print('  %-44s %s' % (q[:44], c[:60]))
