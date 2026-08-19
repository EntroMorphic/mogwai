"""Dump MASSIVE train utterances + MiniLM teacher embeddings.
Train split ONLY — test is never touched here."""
import json, numpy as np, torch
from datasets import load_dataset
from transformers import AutoTokenizer, AutoModel
ds=load_dataset('mteb/amazon_massive_intent','en')
rows=[{"text":r["text"],"label":r["label_text"]} for r in ds["train"]]
with open("massive_train.jsonl","w") as f:
    for r in rows: f.write(json.dumps(r)+"\n")
print(f"  {len(rows)} train utterances ({sum(1 for r in rows if r['label'].startswith('iot_'))} iot)")
M="sentence-transformers/all-MiniLM-L6-v2"
tk=AutoTokenizer.from_pretrained(M); md=AutoModel.from_pretrained(M).eval()
out=[]
with torch.no_grad():
    for i in range(0,len(rows),256):
        b=tk([r["text"] for r in rows[i:i+256]],padding=True,truncation=True,max_length=32,return_tensors="pt")
        h=md(**b).last_hidden_state; m=b["attention_mask"].unsqueeze(-1).float()
        e=(h*m).sum(1)/m.sum(1).clamp(min=1e-9)
        out.append(torch.nn.functional.normalize(e,dim=-1).numpy())
        print(f"\r  teacher {min(i+256,len(rows))}/{len(rows)}",end="",flush=True)
E=np.concatenate(out).astype(np.float32); np.save("massive_teacher.npy",E)
print(f"\n  teacher {E.shape}  mean pairwise cos {float((E@E.T).mean()):.3f}")
