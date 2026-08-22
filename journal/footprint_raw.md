# RAW — shrinking the footprint by unconventional means

Prompt: the ESP32-C6 was turned into a computable fabric running a Ternary-CfC
without powering the LP or HP cores. Can conventional hardware here be used
unconventionally to shrink the footprint?

Everything below is measured on device or on the full corpus.

## Where the bytes are

    index                    656.2 KB   10500 vectors x 64 B
    popcount table             0.25 KB   DRAM
    firmware code             ~78   KB   flash
    total                    734.5 KB    17.9% of a 4 MB part

    per vector:  mask plane 32 B (256 bits, 57.9 set — 22.6% density)
                 sign plane 32 B (256 bits, but only 57.9 carry meaning)
                 -> 24.8 B per vector is provable waste

## The source is smaller than the artifact

    stored vector        64.0 B
    source text          35.0 B average (189 max)
    corpus as text      358.5 KB   vs   656.2 KB as vectors

**The text that generates a vector is 1.83x smaller than the vector.** We store
an expansion of something more compact.

## Vectors do not compress against each other

XOR each vector against its nearest neighbour among the preceding 400:

    set bits stored as-is           317393
    set bits after delta            275427   (86.8%)

Only 13% reduction, despite a 36-37% near-duplicate rate in the TEXT. Hashing
destroys the resemblance: near-duplicate utterances do not produce
near-duplicate codes. Delta encoding is not a lever here.

## What the index is actually FOR

The 74 KB word-prior gate already classifies IoT better than the 656 KB router
(89.6% vs 85.9%). So the index is not buying classification. Measured, on dev:

    signal                 commands   non-commands   separation
    prior margin              40.9        51.8         0.79x   <- wrong way
    words                      5.8         7.0         0.82x   <- wrong way
    router score (656 KB)    186.3       160.5         1.16x

Prior margin is HIGHER for non-commands. As a standalone rejector the prior
fires on 538-1047 of 1335 non-commands. The router fires on **1**.

**The index buys the ability to say no.** And 9345 of its 10500 vectors (89%,
584 KB) are negatives that exist only for that.

## The exchange rate on those 584 KB

Positives only (1155 vectors, 72 KB), threshold swept, with and without a
statistical none-veto from the word channel:

    th    veto   fa   wa   missed   recall
    150   off     8   10     27     80.7%
    165   off     3    6     48     71.9%
    180   off     1    3     73     60.4%
    150   on      8    9     31     79.2%
    180   on      1    3     74     59.9%

    full 656 KB index:   fa=1  wa=13  missed=14  85.9%

At matched fa=1: **73 missed without the negatives, 14 with them.** The veto
adds nothing — it is marginally worse.

584 KB buys 59 recovered commands. ~10 KB per command.
