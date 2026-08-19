# RAW — twin-ternary "lost" and that was impossible

Twin-ternary is a strict superset of binary. Set every mask bit and you have
binary exactly. It cannot be fundamentally worse. So when test said 71.4% for
twin-ternary against 76.4% for binary, that was not a finding — it was a bug
report, and I wrote it up as a finding.

That is the part that bothers me most. I had a structural guarantee available.
The representation *contains* the baseline. Any measurement showing the superset
losing is a measurement error by construction, and I should have gone looking
the moment I saw it instead of reporting a "collapse" and proposing next steps
around it. It took the user saying "this makes absolutely no sense" for me to
apply an argument I already had.

The bug: 75.6% of dev utterances leak verbatim back into the index through
NLU-Eval. I carved dev out of MASSIVE train two turns ago and never added dev
items to the dedup set. MASSIVE derives FROM NLU-Eval, so three quarters of my
dev queries had exact copies of themselves sitting in the index. Dev was
measuring exact-duplicate retrieval, which is why it read 99.5% and why I
believed a number I had already called "too good" out loud.

And the dedup set existed precisely to prevent this. I built the guard, then
added a code path that bypassed it.

Everything derived from that dev is void: d=256, mask=none, veto=off, and the
Dice validation. The veto in particular I "rejected" on leaked evidence, and it
had passed a proper paired McNemar in the Python implementation. With the leak
fixed it is not harmful at all — it is a safety trade, 11 wrong / 16 missed
against 17 / 7.

What scares me: this is the third variant of one failure. Python: fitted the
lexicon to test. C: iterated against test. C again: contaminated dev. Different
mechanisms, identical shape — the evaluation data touching the training data.
I keep building guards against the last instance and walking into the next.

Open questions:
1. What other structural guarantees am I holding and not using as checks?
2. Why did "too good to be true" not trigger action when I said it aloud?
3. Is the test number (71.4%) also void, since the config was leak-derived?
4. How do I make contamination structurally impossible rather than remembered?
5. Which of the four voided decisions actually change on clean dev?
