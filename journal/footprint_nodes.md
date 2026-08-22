# NODES — the claims, separated so each can be killed

## N1. The index's irreplaceable job is REJECTION, not classification
The 74 KB gate out-classifies the 656 KB router on IoT items. The router's only
unique capability is refusing to act.

## N2. Rejection requires stored negatives IN THE SAME REPRESENTATION
No word-level statistic reproduces it. Prior margin runs the wrong way (0.79x).
A statistical none-veto added nothing on top of positives-only.

## N3. Therefore the footprint floor is set by how many negatives rejection needs
Not by encoding cleverness. 584 KB of the 656 KB is negatives.

## N4. The source text is smaller than the vectors it generates
358 KB vs 656 KB. Storing text and regenerating is a 1.83x cut — if regeneration
is affordable.

## N5. Regeneration is NOT affordable per query
Encode measures ~53 us per utterance. 10500 x 53 us = 558 ms against a 43.5 ms
budget. Text-as-storage requires either boot-time expansion (needs 656 KB of RAM
we do not have) or per-query re-encoding (13x too slow).

## N6. Entropy-coded vectors beat text anyway
31.9 B/vector = 327 KB, below the 358 KB text form, and needs no re-encoding.
So text-as-storage is dominated even before the speed problem.

## N7. Delta/dictionary compression is not available
13% reduction against nearest neighbours. Hashing destroys the text-level
redundancy that would make it work.

## N8. The peripheral-fabric trick does not port from C6 to D0WD-V3
The C6 result used PARLIO. This part has no PARLIO. Its nearest equivalent —
DMA -> I2S parallel -> GPIO loopback -> PCNT — was priced at 268 ms per scan,
3.4x slower than the CPU, because PCNT counts EDGES and caps the rate.

## N9. Peripheral RAM is too small to hold anything
RMT 2 KB, SPI buffers 64 B, RTC slow/fast 8 KB each, eFuse ~1 KB. Sum is under
20 KB against a 656 KB index. Peripherals can compute; they cannot store.

## N10. The only measured, accuracy-safe cut is pruning
Condensation: 656 -> 418 KB, cost inside the noise floor. Everything else is
either projected (encodings) or costs real accuracy (cnn+2, d=128).
