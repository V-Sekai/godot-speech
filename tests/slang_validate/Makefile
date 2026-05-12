# Real-input validators for the Speech.SlangCodegen.* kernels.
#
# Each kernel is converted by `slangc -target cpp` to a CPU function
# `main_0(varyingInput, params, globals)`, then compiled together with
# a hand-written test harness that:
#   1. populates the kernel's input buffers with known values,
#   2. invokes the kernel as a single dispatch,
#   3. asserts the output matches an independent CPU reference.
#
# `make test` runs every harness; non-zero exit on any failure.
#
# Discipline target: each kernel also builds through `slangc -target
# metal` to surface CPU-specific spec leaks (e.g. addrspace, intrinsic
# names). Audio never runs on the GPU at runtime — the Metal build is
# a free portability check. See CHI-101 issue / 2026-05-12 decision doc.
#
# Prerequisite: a slangc toolchain. Point $SLANGC at it, e.g.:
#   export SLANGC=$(realpath ../TOOL_cloth_dynamics/bin/slangc)
# Cloth-dynamics' bin/slangc (slangc 2026.8+) works directly. If you
# don't have one, run TOOL_cloth_dynamics/misc/install-slang.sh which
# downloads a known-good release into bin/.slang/.

REPO_ROOT  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SLANGC     ?= slangc
EMITTED    := $(CURDIR)/build
LEAN_DIR   := $(REPO_ROOT)/lean
# Slang's cpp prelude header lives next to the slangc binary tree.
# Try $(SLANGC_DIR)/.slang/include first (wrapper-script layout used by
# cloth_dynamics' bin/slangc), fall back to $(SLANGC_DIR)/../include
# (raw-binary layout).
SLANG_INC  := $(firstword \
                $(wildcard $(dir $(SLANGC)).slang/include) \
                $(abspath $(dir $(SLANGC))/../include))

CXX        ?= clang++
# -Wno-unused-parameter silences clang's complaint about
# `entryPointParams_0` in slangc -target cpp output (the parameter is
# part of the Slang ABI and unused by every compute kernel we emit).
CXXFLAGS   ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
INCLUDES   := -I$(SLANG_INC) -I$(EMITTED)

# Per-test-to-kernel mapping. test_name → kernel_name.
TESTS      := frame_energy:frame_energy \
              framing_cursor:framing_cursor \
              jitter_append:jitter_append \
              pcm_from_s16:pcm_from_s16 \
              pcm_to_s16:pcm_to_s16 \
              vad_gate:vad_gate

# Metal kernels (discipline build; never run at runtime).
METAL_KERNELS := frame_energy framing_cursor jitter_append pcm_from_s16 pcm_to_s16 vad_gate
METALLIBS     := $(addprefix $(EMITTED)/,$(addsuffix .metallib,$(METAL_KERNELS)))

TEST_NAMES := $(foreach t,$(TESTS),$(word 1,$(subst :, ,$(t))))
KERNEL_FOR  = $(word 2,$(subst :, ,$(filter $(1):%,$(TESTS))))

KERNELS    := $(sort $(foreach t,$(TESTS),$(word 2,$(subst :, ,$(t)))))
SHADERS    := $(addprefix $(EMITTED)/,$(addsuffix _emit.cpp,$(KERNELS)))
BINS       := $(addprefix $(EMITTED)/,$(addsuffix _test,$(TEST_NAMES)))

.PHONY: test test-cpp test-metal clean emit-shaders metal-discipline

# Default target: cpp-target bit-exact tests + Metal-target discipline build.
# `test-metal` is a no-op on Linux (xcrun missing); CI gates it on host OS.
test: test-cpp metal-discipline

test-cpp: $(BINS)
	@fails=0; \
	for b in $(BINS); do \
	  echo "=== $$b"; $$b || fails=$$(( fails + 1 )); \
	done; \
	if [ $$fails -ne 0 ]; then echo "FAILED: $$fails"; exit 1; fi; \
	echo "all kernels OK (cpp target)"

# Metal discipline: every kernel must reach .metallib without error.
# We don't dispatch them at runtime.
metal-discipline: $(METALLIBS)
	@echo "all kernels OK (metal target — discipline build)"

# Make sure the .slang files exist before we ask slangc for cpp output.
emit-shaders:
	@cd $(LEAN_DIR) && lake exe emit_shaders /tmp/godot_speech_emit >/dev/null

# slang → cpp source.
$(EMITTED)/%_emit.cpp: emit-shaders
	@mkdir -p $(EMITTED)
	$(SLANGC) -target cpp -stage compute -entry main \
	  -o $@ /tmp/godot_speech_emit/$*.slang

# Per-test build rule. Each test_name maps to a kernel_name via the
# TESTS list; the test source #include's its kernel's emit.
.SECONDEXPANSION:
$(EMITTED)/%_test: %_test.cpp $$(EMITTED)/$$(call KERNEL_FOR,%)_emit.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

# Multi-kernel composition: pcm_roundtrip co-includes both PCM emits
# via the namespace + SLANG_PRELUDE_EXTERN_C-undef pattern (see
# tests/slang_validate/pcm_roundtrip_test.cpp header comment, and
# cloth_dynamics' cg_demo_test.cpp for the original pattern).
$(EMITTED)/pcm_roundtrip_test: pcm_roundtrip_test.cpp \
                               $(EMITTED)/pcm_to_s16_emit.cpp \
                               $(EMITTED)/pcm_from_s16_emit.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I$(SLANG_INC) -o $@ $<

# Add pcm_roundtrip to the test list (not in TESTS because it depends
# on multiple kernels, but we still want `make test` to run it).
BINS       += $(EMITTED)/pcm_roundtrip_test

# ---- Metal discipline pipeline ----------------------------------------
# slang -> .metal source -> .air -> .metallib. xcrun-only; skip on non-macOS.

$(EMITTED)/%.metal: emit-shaders
	@mkdir -p $(EMITTED)
	$(SLANGC) -target metal -stage compute -entry main \
	  -o $@ /tmp/godot_speech_emit/$*.slang

$(EMITTED)/%.air: $(EMITTED)/%.metal
	xcrun -sdk macosx metal -c $< -o $@

$(EMITTED)/%.metallib: $(EMITTED)/%.air
	xcrun -sdk macosx metallib $< -o $@

clean:
	rm -rf $(EMITTED)
