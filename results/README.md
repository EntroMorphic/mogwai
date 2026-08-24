# Run log

`RESULTS.tsv` gets one row per variant per run, stamped with UTC, the git SHA,
and whether the tree was clean. Appended automatically by `make compare` /
`make testset`.

    utc  git_sha  tree  split  variant  iot_acc  se  fa  wa  missed  index_kb  th  n

`fa` = fired on a non-command (unbidden actuation). `wa` = acted wrongly on a
real command. `iot_acc` is recall over IoT items and **cannot see `fa`** — see
`../doc/METHOD.md`.

## `RESULTS.v1-corrupt.tsv` — kept as evidence, do not use

The v1 log was written by scraping the formatted console output with awk on
whitespace fields. `binary (1 bit)` is **three** fields, so every binary row
logged its variant as `binary (1`, shifted each later column by one, and dropped
`index_kb` entirely. The schema also changed silently mid-project when error
bars were added, so early rows and late rows do not mean the same thing.

Not repairable from the file itself — the lost columns were never written.
Retained because the timestamps and SHAs are still a true record of *when* runs
happened, and because deleting the evidence of a tracking failure would be the
wrong lesson.

`compare` now emits its own `ROW\t...` line and the Makefile only stamps and
appends it. Nothing parses formatted output.

`mutation-coverage.txt` and `mutation-detail.txt` are written by
`scripts/mutate.sh`: which checks a mutation made fail, flat and per-mutation.
`mutation-summary.txt` records the headline — coverage, suite size, and the
commit it was measured at — because that number previously went to stdout only,
so it survived in commit messages and nowhere you could audit.

`RELEASE_VERIFIED.tsv` records that a published release was downloaded, flashed
to an **erased** board, and seen to boot and route — tag, the sha256 of the
bytes that were flashed, when, the measured latency, and the commit. Written by
`scripts/verify-release.sh`; `regress.sh` asserts every tag has a row. Recording
the sha matters as much as recording the tag: re-running a release workflow
replaces the published asset under the same tag, which is how v0.1.0's binary
was silently swapped 83 minutes after release.
