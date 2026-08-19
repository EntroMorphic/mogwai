# NODES — the grain

N1. **The board is validated.** ESP-AT backed up + verified restore. Toolchain,
    flash, blob-in-flash, C encoder, host-diff all proven. This is real and done.

N2. **The encoder works numerically.** cos(dev,host) 0.999973 across four
    configs, 23ms/utterance, 2.84x optimised. Not in doubt.

N3. **The encoder does not work evidentially.** 5/10 on genuinely novel input
    once contamination and threshold leakage are removed.

N4. **There is no external referent.** Corpus, test queries, threshold, and
    success criteria all originate from me. No real utterance has ever been
    processed.

N5. **The command set is invented.** lights/thermostat/sensor/GPIO came from
    Needle's docs, not from a stated product.

N6. **The deterministic router is unreasonably strong.** 8/10 hard, 11/12 easy,
    0 bytes, fully auditable. It beat Needle on GPIO levels.

N7. **The two approaches have never been fairly compared.** Different test
    sets, different protocols. The one comparison that matters is missing.

N8. **The encoder→LCVDB handoff is broken.** 92.6% trit collapse. Fixable by
    rescaling, but currently silent garbage.

N9. **LCVDB is sized for a problem we don't have.** Its wins (ternary packing,
    LSH, sub-ms) appear at 65K–6.5M vectors. We have hundreds.

N10. **A flat scan may be sufficient.** 200 vectors x 64 dims int8 = 12.8KB and
     ~12,800 MACs. Trivial on hardware we've already profiled.

N11. **Concurrency bug outstanding.** `volatile` job struct with no barrier
     before the semaphore give. Currently benign, timing-dependent.

N12. **No requirements exist.** No accuracy target, latency budget, or power
     figure. We optimised 65ms to 23ms against nothing.

N13. **The hardware may be incidental.** ESP32 classic was running ESP-AT — a
     WiFi modem firmware. It may have been bought for something unrelated.

## Tensions

T1. **Neural generalisation vs deterministic auditability.** The encoder handles
    paraphrase the router can't; the router never fires a confidently-wrong call.
    Unresolved because untested.

T2. **Building capability vs establishing ground truth.** Every hour spent on
    the tokenizer or the LCVDB port is an hour not spent finding out what the
    device says. But data collection feels like not-working.

T3. **LCVDB is good and it is the user's own.** Recommending we skip it is
    recommending against a thing they built and are proud of. The engineering
    and the social pull in different directions.

T4. **Optimisation is measurable; specification is not.** 2.84x is a number I
    can show. "We don't know what this device does" is not. The former is
    seductive precisely because it is legible.

T5. **Sharpen first, cut once — but we cut first.** This session produced eight
    optimisation configs, two encoders, three eval harnesses, and a firmware,
    before anyone asked what the thing is for.
