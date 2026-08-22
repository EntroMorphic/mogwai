# Provenance — tracked on purpose

This directory is the **one exception** to `archive/` being gitignored, and it
exists to solve a specific unrecoverable-loss risk.

## `needle-upstream.bundle`

A complete `git bundle` of [anjaustin/needle](https://github.com/anjaustin/needle)
as it stood when this project separated from it — the Cactus Needle 2 Python
package this began as an audit of. 270 commits, all refs.

**Three of those commits were never pushed.** `origin` only ever carried `main`:

    3b8cefd  feat: pack ternary weights in base-3, 1.6 bits/weight
    990978b  feat: add `needle build --kv-window`
    281baec  fix: repair `needle run`, Python 3.9 import, JAX dependency split

Until this file was tracked, the only two copies — the original `.git` and a
bundle beside it — were **in the same folder on the same disk**. Co-located
redundancy is not redundancy. Tracking it here puts those commits on a remote,
which is the only copy that survives losing the machine.

It lives here rather than in `archive/needle_upstream/` for a mechanical reason
as well as a tidiness one: that directory contains a nested `.git`, so git
refuses to track anything inside it no matter what `-f` you pass. A backup
should not live inside the repository it is backing up.

## Restore

    git clone provenance/needle-upstream.bundle <dest>
    shasum -a 256 -c provenance/needle-upstream.bundle.sha256   # verify first

Verified at creation by cloning from it and confirming all three unpushed
commits are recoverable. `make regress` re-checks the checksum on every run.
