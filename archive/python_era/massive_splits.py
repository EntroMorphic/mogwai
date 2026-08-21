"""Build the eval splits ONCE and embed them with the teacher, so every
system is scored on identical data."""
import json, numpy as np, torch
from datasets import load_dataset
from transformers import AutoTokenizer, AutoModel
ds=load_dataset('mteb/amazon_massive_intent','en'); rng=np.random.default_rng(0)
def prep(split,neg_n):
    rows=[{"t":r["text"],"y":r["label_text"]} for r in ds[split]]
    iot=[r for r in rows if r["y"].startswith("iot_")]
    non=[r for r in rows if not r["y"].startswith("iot_")]
    idx=rng.permutation(len(non))[:neg_n]
    return iot+[{"t":non[i]["t"],"y":"none"} for i in idx]
sp={"index":prep("train",1500),"dev":prep("validation",200),"test":prep("test",400)}
json.dump(sp,open("massive_splits.json","w"))
M="sentence-transformers/all-MiniLM-L6-v2"
tk=AutoTokenizer.from_pretrained(M); md=AutoModel.from_pretrained(M).eval()
def emb(ts):
    o=[]
    with torch.no_grad():
        for i in range(0,len(ts),256):
            b=tk(ts[i:i+256],padding=True,truncation=True,max_length=32,return_tensors="pt")
            h=md(**b).last_hidden_state; m=b["attention_mask"].unsqueeze(-1).float()
            e=(h*m).sum(1)/m.sum(1).clamp(min=1e-9)
            o.append(torch.nn.functional.normalize(e,dim=-1).numpy())
    return np.concatenate(o).astype(np.float32)
for k,v in sp.items(): np.save(f"t_{k}.npy", emb([r["t"] for r in v]))
print("  splits: "+", ".join(f"{k}={len(v)}" for k,v in sp.items()))
