# Everything is C. Every run is logged. No Python anywhere in this pipeline.
CC      := cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
SRC     := c/src
BIN     := c/bin
CORE    := $(SRC)/router.c $(SRC)/ternary.c $(SRC)/cascade.c $(SRC)/invariants.c
DATA    := data/train.json data/validation.json data/test.json data/nlu_home.csv
LOG     := results/RESULTS.tsv

.PHONY: all fetch compare clean log-header
all: $(BIN)/compare

$(BIN)/%: $(SRC)/%.c $(CORE) $(SRC)/router.h $(SRC)/ternary.h
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -o $@ $< $(CORE)

fetch:
	@./scripts/fetch.sh

$(DATA):
	@./scripts/fetch.sh

# every run appends one row to results/RESULTS.tsv, stamped with the git SHA
compare: $(BIN)/compare $(DATA) log-header   # dev only
test: $(BIN)/compare $(DATA) log-header
	@mkdir -p results
	@sha=$$(git rev-parse --short HEAD 2>/dev/null || echo nogit); \
	 dirty=$$(git diff --quiet 2>/dev/null && echo clean || echo DIRTY); \
	 out=$$($(BIN)/compare $(DATA) 2>&1); \
	 echo "$$out"; \
	 echo "$$out" | awk -v s="$$sha" -v d="$$dirty" -v ts="$$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
	   '/^  (binary|twin)/ {gsub(/%/,"",$$0); \
	     printf "%s\t%s\t%s\tcompare\t%s %s\t%s\t%s\t%s\t%s\n", ts, s, d, $$1, $$2, $$3, $$4, $$5, $$6}' \
	   >> $(LOG); \
	 echo "  -> logged to $(LOG)"

log-header:
	@mkdir -p results
	@[ -s $(LOG) ] || printf "utc\tgit_sha\ttree\texperiment\tvariant\tiot_acc\twrong\tmissed\tindex_kb\n" > $(LOG)

clean:
	rm -rf $(BIN)
