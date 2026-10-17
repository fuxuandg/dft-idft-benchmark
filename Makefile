SHELL := /bin/bash
.DEFAULT_GOAL := help
CC = gcc
OAI_CC ?= gcc
ARCHFLAGS ?= -march=rv64gcv
CFLAGS ?= -O2 -std=gnu99
IMPL ?= v2
TRANSFORM ?= idft
SIZE ?= 64
CPU ?= 2
SEED ?= 1
SCALE ?= 1
ITERS ?= 100000
REPS ?= 7
CASES ?= 2000
MAG ?= 364
SIGNAL ?= sparse
CSV ?= data/current/results.csv
OUTPUT ?= pretty
COLOR ?= auto
V ?= 0
MAKEFLAGS += --no-print-directory
ifneq ($(V),1)
.SILENT:
endif
include modules/catalog.mk
ifeq ($(filter $(IMPL),$(CARTRIDGES)),)
$(error Unsupported IMPL=$(IMPL); available: $(CARTRIDGES))
endif
# The build identity includes compiler versions and flags, preventing stale
# objects when compilers/options are changed.  No cleans are needed for A/B.
BUILD_ID := $(shell printf '%s\n' '$(CC) $(shell $(CC) -dumpfullversion -dumpversion 2>/dev/null)' '$(OAI_CC) $(shell $(OAI_CC) -dumpfullversion -dumpversion 2>/dev/null)' '$(CFLAGS) $(ARCHFLAGS)' | sha256sum | cut -c1-12)
BUILD := build/$(BUILD_ID)
# OAI reference objects depend on OAI_CC, not on the candidate's compiler.
# Keeping a separate identity avoids recompiling the large reference source
# three times during a GCC/Clang matrix run.
BASE_ID := $(shell printf '%s\n' '$(OAI_CC) $(shell $(OAI_CC) -dumpfullversion -dumpversion 2>/dev/null)' '$(CFLAGS) $(ARCHFLAGS) SIMDE_NO_NATIVE' | sha256sum | cut -c1-12)
BASE_BUILD := build/base-$(BASE_ID)
OAI := third_party/oai
SIMDE := third_party/simde-standard
INC := -Iinclude -I$(SIMDE) -I$(OAI) -I$(OAI)/openair1 -I$(OAI)/openair1/PHY/TOOLS \
       -I$(OAI)/common/utils -I$(OAI)/common/utils/T -I$(OAI)/common/utils/LOG \
       -I$(OAI)/openair2 -I$(OAI)/openair2/COMMON -I$(OAI)/targets -I$(OAI)/targets/COMMON \
       -I$(OAI)/radio/COMMON -I$(OAI)/executables -I$(OAI)/nfapi/open-nFAPI/nfapi/public_inc
DEFS := -DNB_ANTENNAS_RX=1 -DNB_ANTENNAS_TX=1 -DMAX_NUM_CCs=1 -DOAI_DISABLE_WRITE_OUTPUT_MACRO -DSIMDE_NO_NATIVE
BASEOBJ := $(BASE_BUILD)/oai_dfts.o $(BASE_BUILD)/time_meas.o $(BASE_BUILD)/taus.o $(BASE_BUILD)/stubs.o
REF := $(BUILD)/reference.o
META := -DBUILD_CC='"$(CC): $(shell $(CC) -dumpfullversion -dumpversion 2>/dev/null)"' -DBUILD_FLAGS='"$(CFLAGS) $(ARCHFLAGS)"'
BIN := $(BUILD)/bench-$(IMPL)
ARGS := --transform $(TRANSFORM) --size $(SIZE) --cpu $(CPU) --scale $(SCALE) --seed $(SEED) --output $(OUTPUT) --color $(COLOR)
PIN := $(if $(filter -1,$(CPU)),,taskset -c $(CPU))

$(BASE_BUILD)/oai_dfts.o: $(OAI)/openair1/PHY/TOOLS/oai_dfts.c
	@mkdir -p $(@D)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -MMD -MP -c $< -o $@
$(BASE_BUILD)/time_meas.o: $(OAI)/common/utils/time_meas.c
	@mkdir -p $(@D)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -MMD -MP -c $< -o $@
$(BASE_BUILD)/taus.o: $(OAI)/openair1/SIMULATION/TOOLS/taus.c
	@mkdir -p $(@D)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -MMD -MP -c $< -o $@
$(BASE_BUILD)/stubs.o: tests/legacy/compat_stubs.c
	@mkdir -p $(@D)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -MMD -MP -c $< -o $@
$(REF): modules/oai_adapter.c include/bench_module.h
	@mkdir -p $(@D)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -MMD -MP -c $< -o $@
$(BUILD)/oai-selected.o: modules/oai_adapter.c include/bench_module.h
	@mkdir -p $(@D)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -DSELECT_OAI -MMD -MP -c $< -o $@
$(BUILD)/runner.o: tests/bench.c include/bench_module.h
	@mkdir -p $(@D)
	@printf '\n  [BUILD] test runner (%s)\n' '$(CC)'
	$(CC) $(CFLAGS) $(ARCHFLAGS) $(INC) $(META) -Wall -Wextra -MMD -MP -c $< -o $@

define RVV_RULES
$(BUILD)/kernel-$(1).o: $(SOURCE_$(1)) $$(wildcard modules/$(1)/*.h)
	@mkdir -p $$(@D)
	$(CC) $(CFLAGS) $(ARCHFLAGS) -Imodules/$(1) -D$(ENTRY_$(1))=$(SYMBOL_$(1)) -MMD -MP -c $$< -o $$@
$(BUILD)/adapter-$(1).o: $(ADAPTER_$(1)) include/bench_module.h
	@mkdir -p $$(@D)
	$(CC) $(CFLAGS) $(ARCHFLAGS) -Iinclude -DKERNEL_SYMBOL=$(SYMBOL_$(1)) -DMODULE_NAME='"$(1)"' -DMODULE_CONTRACT='"$(if $(filter 1,$(APPROX_$(1))),native-DIF-tolerant,AVX2-fixed-point-bit-exact)"' -DMODULE_APPROXIMATE=$(APPROX_$(1)) -DMODULE_POINTS=$(POINTS_$(1)) -DMODULE_INVERSE=$(INVERSE_$(1)) -MMD -MP -c $$< -o $$@
$(BUILD)/bench-$(1): $(BUILD)/runner.o $(BUILD)/adapter-$(1).o $(BUILD)/kernel-$(1).o $(REF) $(BASEOBJ)
	$(OAI_CC) $(ARCHFLAGS) $$^ -lm -lpthread -o $$@
$(BUILD)/legacy-$(1): tests/legacy/harness.c $(BUILD)/kernel-$(1).o $(BASEOBJ)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -DBENCH_USE_RVV=1 -Didft64_rvv=$(SYMBOL_$(1)) -o $$@ $$^ -lm -lpthread
endef
RVV_CARTRIDGES := $(filter-out oai,$(CARTRIDGES))
$(foreach v,$(RVV_CARTRIDGES),$(eval $(call RVV_RULES,$(v))))
$(BUILD)/bench-oai: $(BUILD)/runner.o $(BUILD)/oai-selected.o $(BASEOBJ)
	$(OAI_CC) $(ARCHFLAGS) $^ -lm -lpthread -o $@
$(BUILD)/legacy-oai: tests/legacy/harness.c $(BASEOBJ)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) $(DEFS) $(INC) -o $@ $^ -lm -lpthread
$(BUILD)/ops-test: tests/ops/rvv_ops_test.c tests/ops/rvv_ops.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(ARCHFLAGS) -DSIMDE_NO_NATIVE -I$(SIMDE) -Itests/ops $< -lm -o $@
$(BUILD)/differential-test: tests/differential_test.c $(BUILD)/kernel-v1.o $(BUILD)/kernel-v2.o
	$(CC) $(CFLAGS) $(ARCHFLAGS) -Didft64_rvv_orig=kernel_v1 -Didft64_rvv=kernel_v2 $^ -o $@

.PHONY: help build bench check quality math v1 v2 v3 oai check-all matrix legacy legacy-check legacy-tier1 legacy-tier2 differential ops historical package-check standalone-check legacy-vector-check negative-check
help:
	@printf '%s\n' 'make v1 | v2 | v3 | oai : correctness gate then benchmark' \
	 'make check-all         : golden, random, double, ops, pair differential, legacy checks' \
	 'make bench IMPL=v2 CC=clang CPU=2 ITERS=100000 REPS=7' \
	 'make bench IMPL=oai TRANSFORM=idft SIZE=1024 : extension smoke/timing, no golden claim' \
	 'make matrix            : repeat checks+timing for gcc/gcc-16/clang' \
	 'make historical        : list previous measurement data (does not rerun historical binaries)' \
	 'Output: OUTPUT=pretty|raw COLOR=auto|always|never V=1 (show build commands)' \
	 'See README.md and docs/ for numerical contracts and extension instructions.'
build: $(BIN)
check: $(BIN)
	./$(BIN) --mode golden $(ARGS)
quality: $(BIN)
	./$(BIN) --mode random $(ARGS) --cases $(CASES)
math: $(BIN)
	./$(BIN) --mode math $(ARGS)
bench: $(BIN)
	@mkdir -p $(dir $(CSV))
	./$(BIN) --mode bench $(ARGS) --iters $(ITERS) --reps $(REPS) --signal $(SIGNAL) --mag $(MAG) --csv $(CSV)
v1 v2 v3 oai:
	$(MAKE) check IMPL=$@
	$(MAKE) bench IMPL=$@
ops: $(BUILD)/ops-test
	$(PIN) ./$<
differential: $(BUILD)/differential-test
	$(PIN) ./$<
legacy: $(BUILD)/legacy-$(IMPL)
legacy-check: legacy
	$(PIN) ./$(BUILD)/legacy-$(IMPL) $(if $(filter v3,$(IMPL)),--check-tolerant,--check) reference/x86_golden.txt
legacy-tier1: legacy
	$(PIN) ./$(BUILD)/legacy-$(IMPL) --check-tier1 reference/x86_idft64_scale1_seed1.bin
legacy-tier2: legacy
	$(PIN) ./$(BUILD)/legacy-$(IMPL) --tier2
$(BUILD)/standalone-v1: tests/legacy/main.c tests/legacy/golden.h $(BUILD)/kernel-v1.o
	$(CC) $(CFLAGS) $(ARCHFLAGS) -Imodules/v1 -Itests/legacy -Didft64_rvv=kernel_v1 $< $(BUILD)/kernel-v1.o -o $@
$(BUILD)/standalone-v2: tests/legacy/main.c tests/legacy/golden.h $(BUILD)/kernel-v2.o
	$(CC) $(CFLAGS) $(ARCHFLAGS) -Imodules/v2 -Itests/legacy -Didft64_rvv=kernel_v2 $< $(BUILD)/kernel-v2.o -o $@
standalone-check: $(BUILD)/standalone-v1 $(BUILD)/standalone-v2
	$(PIN) ./$(BUILD)/standalone-v1 --check
	$(PIN) ./$(BUILD)/standalone-v2 --check
$(BUILD)/vector-v1: tests/legacy/rvv_idft64_test.c $(BUILD)/kernel-v1.o $(BASEOBJ)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) -Didft64_rvv=kernel_v1 $^ -lm -lpthread -o $@
$(BUILD)/vector-v2: tests/legacy/rvv_idft64_test.c $(BUILD)/kernel-v2.o $(BASEOBJ)
	$(OAI_CC) $(CFLAGS) $(ARCHFLAGS) -Didft64_rvv=kernel_v2 $^ -lm -lpthread -o $@
legacy-vector-check: $(BUILD)/vector-v1 $(BUILD)/vector-v2
	$(PIN) ./$(BUILD)/vector-v1
	$(PIN) ./$(BUILD)/vector-v2
negative-check: $(BUILD)/bench-v2
	@set -e; bin=./$(BUILD)/bench-v2; \
	 for args in '--mode golden --golden /dev/null' '--mode golden --golden /nonexistent/idft-golden' '--size 1024' '--transform dft' '--iters 0' '--seed 0'; do \
	   set +e; $$bin --cpu $(CPU) $$args >/dev/null 2>&1; rc=$$?; set -e; test $$rc -eq 2 || { echo "FAIL negative $$args rc=$$rc"; exit 1; }; echo "PASS negative $$args"; \
	 done
check-all:
	@set -e; for v in v1 v2 v3 oai; do $(MAKE) check quality math IMPL=$$v; done
	$(MAKE) ops differential standalone-check legacy-vector-check negative-check
	@set -e; for v in v1 v2 v3 oai; do $(MAKE) legacy-check legacy-tier2 IMPL=$$v; done
	@set -e; for v in v1 v2 oai; do $(MAKE) legacy-tier1 IMPL=$$v; done
	$(MAKE) check math IMPL=oai TRANSFORM=dft
matrix:
	@set -e; for cc in gcc gcc-16 clang; do command -v $$cc >/dev/null || { echo "SKIP unavailable $$cc"; continue; }; for v in v1 v2 v3; do $(MAKE) check bench CC=$$cc IMPL=$$v; done; done
historical:
	@find data/historical -type f | sort
package-check:
	sha256sum --quiet -c SHA256SUMS
	@printf '\n  [PASS] Release file checksums verified.\n\n'
-include $(wildcard $(BUILD)/*.d) $(wildcard $(BASE_BUILD)/*.d)
