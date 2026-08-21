# Superseded diagnostics — archived, not deleted

## `probe.c`

Compared the then-current `t_score` (normalised by `|b|` alone) against a
candidate symmetric `dice()`, to diagnose a 10-point gap. **Dice won and was
adopted as `t_score`.** The tool therefore now compares a function to itself —
vacuous, not merely stale.

It had also bit-rotted: it calls `t_score(&q,&TI[k])` with two arguments, from
before `t_score` gained the precomputed-activity parameter. That is how it was
found — `make tools` builds every tool, so a signature change cannot silently
leave a diagnostic behind again.

Its conclusion lives on in the comment above `t_score` in `c/src/ternary.c`:
dividing by `|b|` alone made the score drift 23% with query length, biasing
strict-on-short / loose-on-long — exactly the wrong way, since IoT commands are
short and negatives are long.
