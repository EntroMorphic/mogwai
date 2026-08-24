# Quickstart

Two dependencies on the host: a C compiler and `curl`. No package manager, no
lockfile, no virtualenv. The tracked tree is 2.6 MB; a full clone is ~85 MB,
nearly all of it history (see doc/TODO.md P2-2).

## 60 seconds

    make demo

Fetches the corpora (~2 s), builds (~0.8 s), then shows the router accepting a
command, declining a non-command, declining nonsense, and its measured numbers.

## Talk to it

    make route TEXT="turn off the kitchen light"
    make repl

You get the decision, the score against the threshold, whether a polarity cue
fired, and — because this is a nearest-neighbour router — **the stored
utterances it actually matched**. That last part is the real explanation:

    "turn off the kitchen light"
      decision               iot_hue_lightoff
      score                  220   (threshold 136, margin +84)
      polarity               negative cue present, winner already agrees
      encoding               42 of 256 dims carry evidence
      nearest stored utterances:
         220  iot_hue_lightoff       "turn off the kitchen lights"
         199  iot_hue_lightoff       "turn off the light in the kitchen"
         181  iot_hue_lightoff       "kitchen light off"
         181  iot_hue_lightoff       "please turn off kitchen light"
         180  iot_hue_lightoff       "turn off the lights in the kitchenn"

There are two distinct ways to get `none`, and they are reported separately:
the score fell below the threshold, or the nearest match was not a command.

## Measure it

    make ship        # the shipped operating point — reproduces README's table (~0.7 s)
    make compare     # auto-tuned threshold instead (these differ; see EXPERIMENTS.md)
    make regress     # 72 checks incl. an exhaustive 2^32 proof (~25 s)

`make testset` evaluates on held-out data and **burns one unit of a deliberately
scarce budget** — read [doc/METHOD.md](METHOD.md) before you spend one.

## Put it on hardware

    make c/bin/mkblob && ./c/bin/mkblob data/train.json data/validation.json \
        data/test.json data/nlu_home.csv esp32_router/main/router.bin
    cd esp32_router && idf.py -DRD=256 -DTPOPCNT=1 build flash monitor

Needs ESP-IDF v5.5 (~4 GB toolchain — the one heavy dependency, and only for the
device half). Watch for `PARITY EXACT`: the firmware re-routes 64 host-computed
queries and prints it only if class **and** bit-exact score match on all 64.
See [esp32_router/README.md](../esp32_router/README.md) — that build is a
*validation* firmware, not a product one.

## Where to read next

| you want | read |
|---|---|
| what it is and how well it works | [README.md](../README.md) |
| every flag, grouped by whether it still helps | `compare --help` |
| what each tool in `c/src` does | [doc/TOOLS.md](TOOLS.md) |
| why the numbers can be trusted, and where they can't | [doc/METHOD.md](METHOD.md) |
| the full record, including what failed | [EXPERIMENTS.md](EXPERIMENTS.md) |
| what these numbers do and don't mean | [FRAME.md](FRAME.md) |

## Three rules

No Python. No float on the hot path. Nothing is ever deleted, only archived.
Each was learned by breaking it — hence the name.
