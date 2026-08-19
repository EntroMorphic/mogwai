# NODES

N1. **Twin-ternary works.** 74.1% -> 80.5%, +6.4 pts for 1 extra bit/dim.
    Two popcounts and three logic ops per word. Zero float.

N2. **The score is length-biased.** 149 -> 184 mean best-score across 2-4 vs
    8-10 word queries (23% drift). Dice cuts it to 9%.

N3. **The bias runs against us.** IoT commands are short (median 5 words);
    negatives are long. Strict-on-short, loose-on-long is the worst pairing.

N4. **32-47 missed commands now have a mechanical cause**, not a modelling one.

N5. **Mask plane alone: recall@100 = 97.3%**, at half the ops of full scoring.
    A legitimate cascade stage 1.

N6. **We already scan in 1.6ms.** Compute saved by cascading has no home unless
    we spend it on something.

N7. **The full-precision alternatives are storage-bound, not compute-bound.**
    d=2048 twin-ternary is 6.2MB. Does not fit.

N8. **The raw text is ~600KB.** We hash it and discard it, and it is strictly
    more information than the hash.

N9. **The earlier cascade lost 35 points** — but it was a CLASSIFICATION
    cascade whose second stage could not abstain.

N10. **om[d]*4 >= cnt is an unjustified constant** in the signature mask,
     sitting in the hot path, never validated.

## Tensions

T1. **Cascade saves compute we do not need.** Its value is entirely in what the
    savings buy, and the obvious purchase (more dimensions) is blocked by
    storage, not compute.

T2. **A bad name.** "Cascade" cost us 35 points once. The failure was the
    abstention channel, not the staging. Risk of vetoing a good idea by
    association.

T3. **Hash vs text.** The whole design is "cache-resident integer index", and
    keeping raw strings feels like a betrayal of it — but 600KB is cache-
    resident by any reasonable definition, and the hash is lossy on purpose.

T4. **Two candidate causes for the same 10 points.** Normalisation and
    dimension. If we fix both at once we will not know which mattered.
