# Everything is C. Every run is logged. No Python anywhere in this pipeline.
CC      := cc
# _POSIX_C_SOURCE: strdup is POSIX, not C11. glibc hides it under strict
# -std=c11, so eight files failed to build on Linux while building clean on
# macOS, where Apple's headers expose it regardless. Caught by CI, not by me.
# -lm: libm is separate on Linux; folded into libc on macOS.
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_POSIX_C_SOURCE=200809L
LDLIBS  := -lm
SRC     := c/src
BIN     := c/bin
CORE    := $(SRC)/router.c $(SRC)/ternary.c $(SRC)/cascade.c $(SRC)/invariants.c $(SRC)/prior.c $(SRC)/prune.c $(SRC)/cue.c $(SRC)/gate.c
DATA    := data/train.json data/validation.json data/test.json data/nlu_home.csv
LOG     := results/RESULTS.tsv

.PHONY: all fetch compare ship testset testset-negbound test tools regress route repl demo clean log-header
all: $(BIN)/compare

$(BIN)/%: $(SRC)/%.c $(CORE) $(SRC)/router.h $(SRC)/ternary.h
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -o $@ $< $(CORE) $(LDLIBS)

fetch:
	@./scripts/fetch.sh

$(DATA):
	@./scripts/fetch.sh

# every run appends one row to results/RESULTS.tsv, stamped with the git SHA
# `compare` previously listed prerequisites but carried NO recipe — the recipe
# below was attached to `test:` alone. `make compare` therefore did nothing and
# exited 0: a validation command that silently passed without validating.
# Split explicitly, and name the test-set target so it cannot be typed by habit.
define RUNCOMPARE
@mkdir -p results
@sha=$$(git rev-parse --short HEAD 2>/dev/null || echo nogit); \
 dirty=$$(git diff --quiet 2>/dev/null && echo clean || echo DIRTY); \
 ts=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
 out=$$($(BIN)/compare $(DATA) $(1) 2>&1); \
 echo "$$out" | grep -v '^ROW'; \
 echo "$$out" | awk -F'\t' -v s="$$sha" -v d="$$dirty" -v ts="$$ts" \
   'BEGIN{OFS="\t"} $$1=="ROW"{ r=$$2; for(i=3;i<=NF;i++) r=r OFS $$i; print ts,s,d,r }' \
   >> $(LOG); \
 n=$$(echo "$$out" | grep -c '^ROW'); \
 echo "  -> $$n row(s) appended to $(LOG)"
endef

# dev/validation split — safe to run as often as you like
compare: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,,compare)

# the SHIPPED operating point (threshold RSHIP_TH, not tune()'s choice).
# Reproduces the table in README.md. `compare` auto-tunes and will NOT match it.
ship: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,--ship,ship)

# TEST SET. Burns one unit of results/TEST_BUDGET. Deliberately not `make test`.
# TEST SET at the SHIPPED threshold. Burns one budget unit.
# TEST SET with the selector. Burns one budget unit.
testset-sel: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,--test --ship --gatesel --selmargin=8,TESTSEL)

testset-ship: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,--test --ship,TESTSET)

# TEST SET with boundary-witness negative selection. Burns one budget unit.
# Pre-registered as test evaluation #7 in doc/EXPERIMENTS.md.
testset-negbound: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,--test --fixth=136 --prune-negbound=2685,TESTSET)

testset: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,--test,TESTSET)

# guard: `make test` is a habit-typo for a budgeted resource. Refuse it.
test:
	@echo "  'make test' is ambiguous. Use 'make compare' (dev) or 'make testset'"
	@echo "  ('make testset' consumes one held-out test evaluation)"; exit 1

log-header:
	@mkdir -p results
	@[ -s $(LOG) ] || printf "utc\tgit_sha\ttree\tsplit\tvariant\tiot_acc\tse\tfa\twa\tmissed\tindex_kb\tth\tn\n" > $(LOG)

clean:
	rm -rf $(BIN)

# Build every tool and test. Catches bit-rot in diagnostics that nothing else
# compiles — probe.c sat broken behind a changed t_score signature until this
# target existed.
# The two tests live in c/test/, so the $(BIN)/% pattern above - which needs a
# matching $(SRC)/%.c - never applies to them. Without these explicit rules
# `make c/bin/blobfmt` is a SILENT NO-OP: make finds a file with no rule and
# considers it already up to date, exit 0, no output. doc/TOOLS.md advertises
# `make c/bin/<name>` for every tool; it did not work for these two, and a
# mutation test consequently ran the PREVIOUS binary and reported a pass.
$(BIN)/t_popcnt: c/test/t_popcnt.c c/test/probe.c $(SRC)/router.c $(SRC)/router.h $(SRC)/ternary.h
	@mkdir -p $(BIN)
	@$(CC) $(CFLAGS) -DTPOPCNT=1 -o $@ c/test/t_popcnt.c c/test/probe.c $(SRC)/router.c $(LDLIBS)

$(BIN)/blobfmt: c/test/blobfmt.c $(SRC)/router.c $(SRC)/ternary.c $(SRC)/router.h $(SRC)/ternary.h
	@mkdir -p $(BIN)
	@$(CC) $(CFLAGS) -o $@ c/test/blobfmt.c $(SRC)/router.c $(SRC)/ternary.c $(LDLIBS)

$(BIN)/blobguard: c/test/blobguard.c $(SRC)/router.c $(SRC)/ternary.c $(SRC)/router.h $(SRC)/ternary.h
	@mkdir -p $(BIN)
	@$(CC) $(CFLAGS) -o $@ c/test/blobguard.c $(SRC)/router.c $(SRC)/ternary.c $(LDLIBS)

$(BIN)/imgcheck: c/test/imgcheck.c
	@mkdir -p $(BIN)
	@$(CC) $(CFLAGS) -o $@ c/test/imgcheck.c

.PHONY: tools
tools: $(patsubst $(SRC)/%.c,$(BIN)/%,$(filter-out $(CORE),$(wildcard $(SRC)/*.c))) $(BIN)/t_popcnt $(BIN)/blobfmt $(BIN)/blobguard $(BIN)/imgcheck
	@echo "  all tools + tests built: $$(ls $(BIN) | tr '\n' ' ')"

# Full host regression. Run after any structural change.
.PHONY: regress
regress:
	@./scripts/regress.sh

# --- try it -----------------------------------------------------------------
# route one utterance and show why:   make route TEXT="dim the bedroom lights"
.PHONY: route repl demo
route: $(BIN)/compare $(DATA)
	@$(BIN)/compare --ship --route="$(TEXT)"

repl: $(BIN)/compare $(DATA)
	@$(BIN)/compare --ship --repl

# a 60-second tour: what it does, how well, and how fast
demo: $(BIN)/compare $(DATA)
	@printf '\n\033[1m1. it routes commands\033[0m\n'
	@$(BIN)/compare --ship --route="turn off the kitchen light"
	@printf '\n\033[1m2. it declines non-commands\033[0m\n'
	@$(BIN)/compare --ship --route="what time does the train leave"
	@printf '\n\033[1m3. it declines nonsense\033[0m\n'
	@$(BIN)/compare --ship --route="zzz qqq xyzzy"
	@printf '\n\033[1m4. measured, at the shipped operating point\033[0m\n'
	@$(BIN)/compare --ship 2>/dev/null | grep -vE '^ROW' | tail -5
	@printf '\n\033[1m5. on the device\033[0m  (4.3 ms, 100%% SRAM-resident, PARITY EXACT — see esp32_router/README.md)\n\n'

# A single file a user can flash at offset 0x0 with no ESP-IDF, no toolchain,
# and no repo checkout. bootloader + partition table + app + blob, merged.
#
# `idf.py merge-bin` rather than esptool with hand-written offsets: it reads the
# offsets out of the build that just happened. The hand-written version was
# 0x1000/0x8000/0x10000 in two files, which is correct only for as long as
# partitions.csv does not move — and a moved app would have produced a
# plausible image of exactly the right size that does not boot.
#
# Then imgcheck, because nothing else in the chain verifies that the data which
# came OUT is the data that went IN.
IMAGE := dist/mogwai-esp32-$(shell git describe --tags --always --dirty 2>/dev/null || echo dev).bin
image: $(BIN)/imgcheck
	@mkdir -p dist
	@cd esp32_router && idf.py -DPRODUCT=1 -DRD=256 -DTPOPCNT=1 build >/dev/null
	@cd esp32_router && idf.py merge-bin -o $(CURDIR)/$(IMAGE) >/dev/null
	@$(BIN)/imgcheck $(IMAGE) esp32_router/main/router.bin
	@shasum -a 256 $(IMAGE) | tee $(IMAGE).sha256
	@printf '\n  %s\n  %s bytes — flash to offset 0x0 (needs a 4 MB ESP32)\n\n' \
	  "$(IMAGE)" "$$(wc -c < $(IMAGE) | tr -d ' ')"
.PHONY: image
