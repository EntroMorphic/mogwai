# Archived: structural ranker (did not survive red-teaming)

Reported as +4.5 iot accuracy and a 46% cut in wrong actuations. **That result
was invalid.** Both the hyperparameters (alpha, K, threshold, veto mode) and the
hand lexicon were fitted to the test set — the lexicon terms "cease", "no",
"pink", "roomba", and the removal of "switch" from the plug list were all added
after inspecting test failures.

Under honest protocol (tune on dev, evaluate test once):

| system | iot acc | wrong | missed |
|---|---|---|---|
| 1-NN + veto (baseline) | 86.4% | 40 | 15 |
| hand lexicon (leaked)  | 88.6% | 44 | 15 |
| auto lexicon from train| 86.4% | 47 | 15 |

`ranker_auto.py` derives the vocabulary from train by log-odds, so it is
leakage-free — and it gains nothing while costing +7 wrong actuations.

Kept because the *idea* is not disproven, only unvalidated at this data scale:
769 train / ~140 dev / 220 test IoT examples cannot both tune and evaluate a
mechanism with four hyperparameters. Revisit only with substantially more data.
