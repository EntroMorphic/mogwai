"""Generate the distillation corpus: device-control utterances with wide
phrasing variety, plus off-topic negatives so the encoder learns to place
them far away. Templates are the source of variety; the teacher supplies
the semantics."""
import itertools, json, random, sys

ROOMS = ["kitchen","bedroom","living room","garage","bathroom","office","hallway",
         "conservatory","attic","basement","nursery","study","porch","landing"]
SENSORS = ["temperature","humidity","pressure","light level","motion","air quality","CO2"]
MODES = ["heat","cool","auto"]

LIGHTS = [
 "turn the {room} lights on","turn on the lights in the {room}","switch the {room} light on",
 "lights on in the {room}","put the {room} lights on","{room} lights on",
 "turn the {room} lights off","switch off the {room} light","kill the lights in the {room}",
 "lights out in the {room}","{room} lights off","shut the {room} lights",
 "dim the {room} to {pct}","set the {room} lights to {pct} percent",
 "brighten the {room}","{room} at {pct} percent","make the {room} brighter",
 "make the {room} dimmer","it is too dark in the {room}","the {room} is too dark",
 "it is too bright in the {room}","the {room} is too bright","too much light in the {room}",
 "I can barely see in the {room}","the {room} is gloomy","it is pitch black in the {room}",
]
THERMO = [
 "set the thermostat to {temp}","make it {temp} degrees","set temperature to {temp}",
 "thermostat to {temp}","put the heating on {temp}","{temp} degrees please",
 "set the thermostat to {temp} on {mode}","{temp} degrees {mode}",
 "turn the heating up","turn the heating down","make it warmer","make it cooler",
 "warm the place up","cool the room down","I am freezing","I am cold",
 "it is chilly in here","I am too hot","it is boiling in here","I am sweating",
 "it is stuffy in here","crank the heat","turn the AC on","turn the heat off",
]
SENSOR = [
 "read the {sensor} sensor","what is the {sensor}","check the {sensor}",
 "get me the {sensor} reading","{sensor} reading","how is the {sensor}",
 "what is the current {sensor}","tell me the {sensor}","report the {sensor}",
 "how warm is it","how cold is it","how humid is it","is it damp in here",
 "what does the {sensor} sensor say","give me a {sensor} reading",
]
GPIO = [
 "set pin {pin} high","set pin {pin} low","pull gpio {pin} high","pull gpio {pin} low",
 "drive pin {pin} to {lvl}","set gpio {pin} to {lvl}","take pin {pin} high",
 "toggle pin {pin}","bring gpio {pin} down","raise pin {pin}","pin {pin} on","pin {pin} off",
]
OFFTOPIC = [
 "what is the capital of Portugal","tell me a joke","who won the world cup",
 "how do I make bread","what time is the train","sing me a song",
 "what is your name","how many sensors do you support","tell me about yourself",
 "what is the meaning of life","write me a poem","translate this to french",
 "how old are you","what is 2 plus 2","recommend a film","who is the president",
 "explain quantum mechanics","what is the weather in Tokyo next week",
 "book me a flight","order a pizza","play some music","call my mother",
]

def build(seed=0, n_per=260):
    rng = random.Random(seed)
    rows = []
    def fill(t):
        return (t.replace("{room}", rng.choice(ROOMS))
                 .replace("{pct}", str(rng.choice([5,10,20,25,30,40,50,60,70,75,80,90,100])))
                 .replace("{temp}", str(rng.randint(15, 26)))
                 .replace("{mode}", rng.choice(MODES))
                 .replace("{sensor}", rng.choice(SENSORS))
                 .replace("{pin}", str(rng.randint(0, 39)))
                 .replace("{lvl}", rng.choice(["0","1","high","low"])))
    for label, pool in (("set_lights",LIGHTS),("set_thermostat",THERMO),
                        ("read_sensor",SENSOR),("set_gpio",GPIO)):
        seen = set()
        # bounded: stop when the template space is exhausted, not when a
        # quota is hit - several pools have fewer than n_per reachable strings
        for _ in range(n_per * 40):
            if len(seen) >= n_per:
                break
            seen.add(fill(rng.choice(pool)))
        for s in seen:
            rows.append({"text": s, "label": label})
    for s in OFFTOPIC:
        for _ in range(6):
            rows.append({"text": s, "label": "none"})
    # compositional pairs - two clauses joined
    for _ in range(n_per // 2):
        a = fill(rng.choice(LIGHTS)); b = fill(rng.choice(THERMO))
        rows.append({"text": f"{a} and {b}", "label": "multi"})
    # cheap variety multiplier: natural carrier phrases around the core request
    PRE  = ["","","","please ","can you ","could you ","hey ","ok ","I want you to ",
            "would you ","go ahead and ","quick, "]
    POST = ["","","","","  please"," now"," thanks"," for me"," right now"," would you"]
    out=[]
    for r in rows:
        for _ in range(3):
            t=(rng.choice(PRE)+r["text"]+rng.choice(POST)).strip()
            out.append({"text":t,"label":r["label"]})
    seen=set(); dedup=[]
    for r in out:
        if r["text"] in seen: continue
        seen.add(r["text"]); dedup.append(r)
    rng.shuffle(dedup)
    return dedup

if __name__ == "__main__":
    rows = build()
    with open(sys.argv[1] if len(sys.argv)>1 else "corpus.jsonl","w") as f:
        for r in rows: f.write(json.dumps(r)+"\n")
    from collections import Counter
    print(f"  wrote {len(rows)} utterances")
    for k,v in Counter(r["label"] for r in rows).most_common(): print(f"    {k:16} {v}")
