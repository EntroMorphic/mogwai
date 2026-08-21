"""Distil MiniLM into a 2-layer d=64 encoder for the ESP32.

Teacher is 384-d; LCVDB wants 64. We PCA the teacher to 64 offline (a fixed
map applied to the TEACHER, so it costs nothing at runtime) and train the
student to match both the direction of each vector and the pairwise geometry
of each batch - the latter is what retrieval actually depends on.
"""
import json, sys, time
import numpy as np, jax, jax.numpy as jnp, optax
sys.path.insert(0, "/Users/aaronjosserand-austin/Projects/needle")
from needle.model.tokenizer import get_tokenizer

V, D, F, L, H, S = 8192, 64, 256, 2, 4, 24
rows = [json.loads(l) for l in open("massive_train.jsonl")]
T = np.load("massive_teacher.npy")

# ---- teacher -> 64 dims (PCA), renormalised
Tc = T - T.mean(0, keepdims=True)
U, Sv, Vt = np.linalg.svd(Tc, full_matrices=False)
keep = (Sv[:D]**2).sum() / (Sv**2).sum()
T64 = Tc @ Vt[:D].T
T64 /= np.linalg.norm(T64, axis=1, keepdims=True) + 1e-9
print(f"  teacher 384 -> {D} dims keeps {100*keep:.1f}% of variance")

# ---- tokenise
tok = get_tokenizer(V)
ids = np.zeros((len(rows), S), np.int32); mask = np.zeros((len(rows), S), np.float32)
lens = []
for i, r in enumerate(rows):
    t = tok.encode(r["text"])[:S]; lens.append(len(t))
    ids[i, :len(t)] = t; mask[i, :len(t)] = 1.0
print(f"  tokens: median {int(np.median(lens))}  p95 {int(np.percentile(lens,95))}  max {max(lens)} (cap {S})")

rng = np.random.default_rng(0); perm = rng.permutation(len(rows))
n_test = 300; te, tr = perm[:n_test], perm[n_test:]

# ---- model
def init(key):
    k = jax.random.split(key, 12); n = lambda kk, sh, s=0.02: jax.random.normal(kk, sh) * s
    p = {"emb": n(k[0], (V, D)), "pos": n(k[1], (S, D), 0.01)}
    for i in range(L):
        p[f"b{i}"] = {"q": n(k[2+i],(D,D)), "k": n(k[4+i],(D,D)), "v": n(k[6+i],(D,D)),
                      "o": n(k[8+i],(D,D)), "f1": n(k[10],(D,F)), "f2": n(k[11],(F,D)),
                      "g1": jnp.ones((D,)), "g2": jnp.ones((D,))}
    return p

def rms(x, g):
    return g * x * jax.lax.rsqrt(jnp.mean(x**2, -1, keepdims=True) + 1e-6)

def encode(p, ids, m):
    x = p["emb"][ids] + p["pos"][None, :ids.shape[1]]
    big = -1e9 * (1.0 - m)[:, None, None, :]
    for i in range(L):
        w = p[f"b{i}"]; h = rms(x, w["g1"])
        B, Tn, _ = h.shape
        q = (h @ w["q"]).reshape(B,Tn,H,D//H).transpose(0,2,1,3)
        k = (h @ w["k"]).reshape(B,Tn,H,D//H).transpose(0,2,1,3)
        v = (h @ w["v"]).reshape(B,Tn,H,D//H).transpose(0,2,1,3)
        a = jax.nn.softmax(q @ k.transpose(0,1,3,2) / np.sqrt(D//H) + big, -1)
        x = x + (a @ v).transpose(0,2,1,3).reshape(B,Tn,D) @ w["o"]
        h = rms(x, w["g2"])
        x = x + jax.nn.relu(h @ w["f1"]) @ w["f2"]
    e = (x * m[..., None]).sum(1) / jnp.maximum(m.sum(1, keepdims=True), 1e-6)
    return e / (jnp.linalg.norm(e, axis=-1, keepdims=True) + 1e-6)

def loss_fn(p, ids, m, tgt):
    e = encode(p, ids, m)
    align = 1.0 - jnp.mean(jnp.sum(e * tgt, -1))              # direction
    geom  = jnp.mean((e @ e.T - tgt @ tgt.T) ** 2)            # pairwise geometry
    return align + geom

p = init(jax.random.PRNGKey(0))
n_par = sum(int(np.prod(x.shape)) for x in jax.tree_util.tree_leaves(p))
print(f"  student {n_par/1e6:.2f}M params  ({n_par/1024:.0f} KB int8)")

EPOCHS, BS = 60, 96
steps = EPOCHS * (len(tr) // BS)
sched = optax.warmup_cosine_decay_schedule(0.0, 3e-3, max(1, steps//20), steps)
opt = optax.chain(optax.clip_by_global_norm(1.0), optax.adamw(sched, weight_decay=1e-4))
st = opt.init(p)

@jax.jit
def step(p, st, i, m, t):
    l, g = jax.value_and_grad(loss_fn)(p, i, m, t)
    u, st = opt.update(g, st, p); return optax.apply_updates(p, u), st, l

t0 = time.time()
for ep in range(EPOCHS):
    order = rng.permutation(tr)
    for s in range(0, len(order) - BS + 1, BS):
        b = order[s:s+BS]
        p, st, l = step(p, st, jnp.asarray(ids[b]), jnp.asarray(mask[b]), jnp.asarray(T64[b]))
    if (ep+1) % 15 == 0 or ep == 0:
        e = np.asarray(encode(p, jnp.asarray(ids[te]), jnp.asarray(mask[te])))
        cs = float(np.mean(np.sum(e * T64[te], -1)))
        print(f"  epoch {ep+1:3d}/{EPOCHS}  loss {float(l):.4f}  held-out cos-to-teacher {cs:.4f}")
print(f"  trained in {time.time()-t0:.0f}s")
np.savez("student_massive.npz", **{k: np.asarray(v) for k, v in
         {**{kk: vv for kk, vv in p.items() if not isinstance(vv, dict)},
          **{f"{bk}.{kk}": vv for bk, bv in p.items() if isinstance(bv, dict) for kk, vv in bv.items()}}.items()})

print("  wrote student_massive.npz, teacher_pca.npy")
