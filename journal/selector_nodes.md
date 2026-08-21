# NODES — the claims, separated so each can be killed independently

## N1. The prior's margin carries information about whether the prior is right
No prior_only item has prmarg < 8; seven router_only items do. Mean 24.7 vs 7.8.

## N2. prmarg = 0 is categorically different from prmarg = small
Four router_only items have prmarg exactly 0 — the prior matched no
discriminating word at all. That is *abstention*, not a weak opinion. A rule
that ignores an abstaining voter needs no fitted threshold.

## N3. The router's own confidence carries NO information about its correctness
Router margin is 11.1 (router right) vs 11.6 (router wrong). Top score runs the
wrong way: 164.4 vs 157.0. **The router cannot tell when it is failing.**

## N4. Therefore the selector must be driven by the prior's confidence, not the
router's
N1 + N3 jointly. This is forced, not chosen: only one channel emits a usable
confidence.

## N5. A prmarg threshold of ~8 would net +7 items on dev
10/10 prior_only caught, 3/10 router_only wrongly flipped.

## N6. The +7 is measured on the same 20 items used to choose the threshold
Twenty items total in the disagreement cells. The threshold was placed in a gap
observed in those twenty. This is the failure mode that produced the archived
lexicon (+4.5 points, fitted to failures, worth nothing leakage-free) and the
126 threshold (+2.1 on dev, did not transfer).

## N7. The selector has never been evaluated against negatives
`--seldump` skips the 1335 non-commands. Every previous attempt that touched
class assignment moved `fa` — the unconstrained delegation produced fa=824.
A selector that improves `wa` while moving `fa` is not obviously a win, because
`fa` is the safety-critical column.

## N8. High prior margin is confounded with agreement
`both correct` has prmarg 45.1, higher than prior_only's 24.7. So high prmarg
mostly indicates "easy item both channels get right", not "trust the prior over
the router". The disagreement cells are a biased slice.

## N9. Three prior combination rules have already failed
Soft bias inert, hard cue harmful, class delegation harmful. A selector is a
fourth rule of the same family. The base rate for this family of intervention in
this project is 0 for 3.

## N10. The oracle bound (94.3%) is not the selector's ceiling
The oracle sees the answer. A selector sees only prmarg. Its ceiling is whatever
prmarg can separate, which is at most the +7 of N5 — i.e. 165 -> 172 of 192,
89.6%, *if* N6 does not apply. Not 94.3%.
