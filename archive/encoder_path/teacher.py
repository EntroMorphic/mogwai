"""Embed the corpus with MiniLM-L6-v2 (the teacher). Runs on the SYSTEM python,
which has torch+transformers; the student trains in the JAX venv. Writes
teacher.npy (L2-normalised, 384-d) alongside the corpus order."""
import json, sys
import numpy as np, torch
from transformers import AutoTokenizer, AutoModel

M = "sentence-transformers/all-MiniLM-L6-v2"
rows = [json.loads(l) for l in open(sys.argv[1])]
texts = [r["text"] for r in rows]

tok = AutoTokenizer.from_pretrained(M)
mod = AutoModel.from_pretrained(M).eval()

out = []
with torch.no_grad():
    for i in range(0, len(texts), 128):
        b = tok(texts[i:i+128], padding=True, truncation=True, max_length=32,
                return_tensors="pt")
        h = mod(**b).last_hidden_state                      # B,T,384
        m = b["attention_mask"].unsqueeze(-1).float()
        e = (h * m).sum(1) / m.sum(1).clamp(min=1e-9)       # mean pool
        out.append(torch.nn.functional.normalize(e, dim=-1).numpy())
        print(f"\r  {min(i+128,len(texts))}/{len(texts)}", end="", flush=True)
E = np.concatenate(out).astype(np.float32)
np.save(sys.argv[2], E)
print(f"\n  teacher embeddings {E.shape} -> {sys.argv[2]}")
sim = E @ E.T
print(f"  mean pairwise cos {sim[np.triu_indices(len(E),1)].mean():.3f} (low = well spread)")
