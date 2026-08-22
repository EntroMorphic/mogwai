# SYNTHESIZE — what to actually do

## The finding

**The index is 89% negatives, and the negatives are irreplaceable.** They are
not redundancy waiting to be compressed away — they are the only evidence the
system has about what a non-command looks like. Rejection is a similarity
judgement and must happen in the representation the positives live in. The word
channel classifies better than the router and cannot reject at all.

That kills the most attractive unconventional idea (74 KB of positives plus a
statistical negative model) and, more usefully, it **bounds the problem**: the
footprint floor is set by how much negative evidence rejection needs, not by
how cleverly the bytes are packed.

## Why the C6 technique does not port

The Ternary-CfC on peripheral fabric works because a CfC has **small state and
expensive computation** — exactly what a dataflow engine is for. This router is
the mirror image: computation is 8% of the time, state is 656 KB. Peripherals
compute; they do not store. Every addressable peripheral memory on this part
sums to under 20 KB.

Where the technique WOULD apply here: not the index, but a future always-on
front end. A wake-gate that decides "is anything speech-like happening" has
small state and streaming input — the right shape for the fabric, and it would
run while both cores sleep. That is the P1-1 I/O gap, and it is where this
approach belongs.

## The ordered plan, cheapest first

1. **Prune (measured, safe).** Condensation: 656 -> 418 KB, accuracy cost inside
   the noise floor. This is the only cut that is both measured and free.
2. **Pack the sign plane (projected, 1.63x).** 24.8 B per vector is provably
   dead — sign bits for dimensions with no evidence. 418 -> 256 KB combined with
   pruning. Costs the branchless popcount, which at 61% memory-bound may still
   be a net win. **Implement and verify bit-exact, as `gate.c` was.**
3. **Entropy-code the mask (projected, 2.0x).** 31.9 B/vector. Combined with
   pruning: 208 KB. More complex, and the decoder cost is unmeasured.
4. **Ask how few negatives rejection actually needs.** The pruning curve was
   measured against the whole frontier; nobody has swept negatives specifically
   at matched fa. If rejection survives on 3000 negatives instead of 9345, that
   is a bigger cut than any encoding.

## The target worth aiming at

**Under ~171 KB.** Below that the index stops living in flash and moves to
internal SRAM, which is worth a measured **2.58x on latency** as well as being
the footprint win. Pruning plus entropy coding reaches 208 KB; pruning plus
entropy plus a real answer to (4) would clear it.

That threshold is the only number in this analysis where footprint and speed
stop trading against each other and start compounding.

## What this cycle cost and returned

Four ideas built and measured, three killed:
- asymmetric positives/negatives — **killed**, and it taught us what the index is for
- text-as-storage — **killed twice**, dominated on size and 13x too slow
- delta encoding — **killed**, hashing destroys text-level redundancy
- peripheral fabric — **does not port**, wrong problem shape

The one that survived is not exotic: pack the sign plane, because a quarter of
every vector is bits that carry no information. The unconventional exploration
did not find a trick; it found the boundary, and the boundary is what makes the
conventional cut worth doing.
