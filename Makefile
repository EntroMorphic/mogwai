# Everything is C. Every run is logged. No Python anywhere in this pipeline.
CC      := cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
SRC     := c/src
BIN     := c/bin
CORE    := $(SRC)/router.c $(SRC)/ternary.c $(SRC)/cascade.c $(SRC)/invariants.c $(SRC)/prior.c $(SRC)/prune.c
DATA    := data/train.json data/validation.json data/test.json data/nlu_home.csv
LOG     := results/RESULTS.tsv

.PHONY: all fetch compare testset test clean log-header
all: $(BIN)/compare

$(BIN)/%: $(SRC)/%.c $(CORE) $(SRC)/router.h $(SRC)/ternary.h
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -o $@ $< $(CORE)

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
 out=$$($(BIN)/compare $(DATA) $(1) 2>&1); \
 echo "$$out"; \
 echo "$$out" | awk -v s="$$sha" -v d="$$dirty" -v ts="$$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
   '/^  (binary|twin)/ {gsub(/%/,"",$$0); \
     printf "%s\t%s\t%s\t$(2)\t%s %s\t%s\t%s\t%s\t%s\n", ts, s, d, $$1, $$2, $$3, $$4, $$5, $$6}' \
   >> $(LOG); \
 echo "  -> logged to $(LOG)"
endef

# dev/validation split — safe to run as often as you like
compare: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,,compare)

# TEST SET. Burns one unit of results/TEST_BUDGET. Deliberately not `make test`.
testset: $(BIN)/compare $(DATA) log-header
	$(call RUNCOMPARE,--test,TESTSET)

# guard: `make test` is a habit-typo for a budgeted resource. Refuse it.
test:
	@echo "  'make test' is ambiguous. Use 'make compare' (dev) or 'make testset'"
	@echo "  ('make testset' consumes one held-out test evaluation)"; exit 1

log-header:
	@mkdir -p results
	@[ -s $(LOG) ] || printf "utc\tgit_sha\ttree\texperiment\tvariant\tiot_acc\twrong\tmissed\tindex_kb\n" > $(LOG)

clean:
	rm -rf $(BIN)

# Build every tool and test. Catches bit-rot in diagnostics that nothing else
# compiles — probe.c sat broken behind a changed t_score signature until this
# target existed.
.PHONY: tools
tools: $(patsubst $(SRC)/%.c,$(BIN)/%,$(filter-out $(CORE),$(wildcard $(SRC)/*.c)))
	@cc $(CFLAGS) -DTPOPCNT=1 -o $(BIN)/t_popcnt c/test/t_popcnt.c c/test/probe.c $(SRC)/router.c
	@cc $(CFLAGS) -o $(BIN)/blobfmt c/test/blobfmt.c $(SRC)/router.c $(SRC)/ternary.c
	@echo "  all tools + tests built: $$(ls $(BIN) | tr '\n' ' ')"
