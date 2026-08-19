import json, numpy as np, torch
from transformers import AutoTokenizer, AutoModel
M="sentence-transformers/all-MiniLM-L6-v2"
tok=AutoTokenizer.from_pretrained(M); mod=AutoModel.from_pretrained(M).eval()
def emb(ts):
    with torch.no_grad():
        b=tok(ts,padding=True,truncation=True,max_length=32,return_tensors="pt")
        h=mod(**b).last_hidden_state; m=b["attention_mask"].unsqueeze(-1).float()
        e=(h*m).sum(1)/m.sum(1).clamp(min=1e-9)
        return torch.nn.functional.normalize(e,dim=-1).numpy()
ANCH=["turn the lights on in the kitchen","switch the lights off","dim the lights",
 "brighten the living room","set the thermostat to 20","make it warmer","cool the room down",
 "read the temperature sensor","what is the humidity","set pin 5 high","pull gpio 12 low"]
TEST=["it is way too bright in here","i am freezing","the office is dark","i am boiling hot",
 "how warm is it right now","drive pin 7 to zero","what is the capital of portugal","tell me a joke",
 "could you make the lounge less glaring","my hands are numb","is the air damp in here",
 "put gpio nine to ground","this room is like an oven","i cannot read my book in here",
 "whats on telly tonight","remind me to buy milk"]
np.save("t_anchor.npy",emb(ANCH)); np.save("t_test.npy",emb(TEST))
rows=[json.loads(l) for l in open("corpus.jsonl")]
print("  saved teacher anchor/test embeddings")
