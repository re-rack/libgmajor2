# G-Major 2 native library: build, checks and tooling. Run from the project root.
#
#   make build                          the static library, build/default/libgm2.a (the default)
#   make install                        libgm2.a, the headers and gm2.pc under PREFIX (/usr/local),
#                                       DESTDIR-aware; make uninstall removes them
#   make check-install                  build a C and a C++ host against a staged install
#   make check                          every fixture (36 effects, swf, tables), the de-popped
#                                       variants and the host checks
#   make check-fixture FIXTURE=<name>   one fixture, or several names in quotes
#   make check-swf                      the swf fixture
#   make check-tables                   the tables fixture
#   make check-host                     the beat rule, align and the context
#   make benchmark-all-fx               every effect's processing time in ns per frame
#   make benchmark-fx FX=<name>         one effect, or several names in quotes
#   make check-convert                  all 2^32 floats on every conversion path, NEON and RVV
#                                       under qemu
#   make record-all-fixtures            rewrite tests/fixtures/ from the emulator
#   make record-fixture FIXTURE=<name>  rewrite one fixture
#   make ghidra-prepare                 download TC's updater and manual, extract the firmware,
#                                       and build the disassembly, call graph and Ghidra project
#                                       (see reverse_engineering/README.md)
#   make ghidra-clean                   remove what ghidra-prepare generates, but not the
#                                       downloads or the Ghidra project and its exports
#   make format                         apply clang-format
#   make static-check                   format check, cppcheck, clang and gcc warnings and analyzers
#   make sanitize                       every check and the quick converter test under ASan and
#                                       UBSan, with gcc and clang
#   make coverage                       gcov line and branch coverage of src/
#   make clean                          remove every build artifact
#
# Only the record targets link the xemu DSP56300 library. They and ghidra-prepare
# extract the firmware into reverse_engineering/firmware/ first; that and the analysis
# land under reverse_engineering/, everything else in build/.

CC ?= cc
CFLAGS ?= -O2 -g
GCC ?= gcc
CLANG ?= clang
CLANG_FORMAT ?= clang-format
CPPCHECK ?= cppcheck
AARCH64_CC ?= aarch64-linux-gnu-gcc
QEMU_AARCH64 ?= qemu-aarch64
AARCH64_SYSROOT ?= /usr/aarch64-linux-gnu
RISCV64_CC ?= riscv64-linux-gnu-gcc
QEMU_RISCV64 ?= qemu-riscv64
RISCV64_SYSROOT ?= /usr/riscv64-linux-gnu
JOBS ?= $(shell nproc)
PYTHON ?= python3
DISASSEMBLER ?= dsp56300-disasm
GHIDRA ?= /opt/ghidra
PYGHIDRA ?= $(HOME)/.local/share/gmajor2/pyghidra/bin/python
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
VERSION := 0.1.0
RE := reverse_engineering
FIRMWARE := $(RE)/firmware
GHIDRA_PROJECT := $(RE)/ghidra/GMajor2.gpr

BUILD ?= build/default

STRICT := -std=c17 -Wall -Wextra -Werror -Wpedantic -Wformat=2 -Wshadow -Wundef \
          -Wcast-qual -Wpointer-arith -Wwrite-strings -Wstrict-prototypes \
          -Wold-style-definition -Wmissing-prototypes -Wredundant-decls \
          -Wdouble-promotion -Wvla -Wswitch-enum -Wconversion -Wsign-conversion
DEPFLAGS := -MMD -MP
C_DIRS := src tests reverse_engineering/harness
INCLUDES := $(addprefix -I,$(C_DIRS))
EMU_LDLIBS := -ldsp56300_emu_ffi -ldl -lpthread -lm

MACHINE := $(shell $(CC) -dumpmachine)
ARCH := $(if $(findstring x86_64,$(MACHINE)),x86_64,$(if $(findstring aarch64,$(MACHINE)),aarch64,))

LIB_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard src/*.c))
LIB_HEADERS := $(wildcard src/*.h)
# Plugins are shared objects: the library has to be position-independent, and hiding its
# symbols keeps two plugins that embed different versions from binding to each other's.
$(LIB_OBJECTS): LIB_FLAGS := -fPIC -fvisibility=hidden
SHARED_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,tests/gm2_fixture.c tests/gm2_signal.c \
                   $(wildcard tests/effect_*.c))
CHECK_OBJECTS := $(BUILD)/tests/gm2_check.o $(SHARED_OBJECTS)
RECORD_OBJECTS := $(BUILD)/tests/gm2_record.o $(BUILD)/reverse_engineering/harness/gm2_emu.o \
                  $(patsubst %.c,$(BUILD)/%.o,$(wildcard tests/firmware_*.c)) $(SHARED_OBJECTS)
BENCH_OBJECTS := $(BUILD)/tests/gm2_bench.o $(SHARED_OBJECTS)
OBJECTS := $(sort $(LIB_OBJECTS) $(CHECK_OBJECTS) $(RECORD_OBJECTS) $(BENCH_OBJECTS))
DEPS := $(OBJECTS:.o=.d)

C_FILES := $(foreach dir,$(C_DIRS),$(wildcard $(dir)/*.c))
FORMAT_FILES := $(C_FILES) $(foreach dir,$(C_DIRS),$(wildcard $(dir)/*.h))

# gm2_convert.c picks its path when it is compiled, so every path gets its own test binary.
CONVERT_VARIANTS := scalar
CONVERT_FLAGS_scalar := -DGM2_NO_SIMD
CONVERT_NEEDS_scalar :=
CONVERT_NAMES_scalar := scalar
ifeq ($(ARCH),x86_64)
CONVERT_VARIANTS += sse2 avx2 avx512
CONVERT_FLAGS_sse2 := -march=x86-64
CONVERT_FLAGS_avx2 := -march=x86-64-v3
CONVERT_FLAGS_avx512 := -march=x86-64-v4
CONVERT_NEEDS_sse2 := sse2
CONVERT_NEEDS_avx2 := avx2
CONVERT_NEEDS_avx512 := avx512f
CONVERT_NAMES_sse2 := SSE2
CONVERT_NAMES_avx2 := AVX2
CONVERT_NAMES_avx512 := AVX-512
else ifeq ($(ARCH),aarch64)
CONVERT_VARIANTS += neon
CONVERT_FLAGS_neon :=
CONVERT_NEEDS_neon :=
CONVERT_NAMES_neon := NEON
endif
CONVERT_BINS := $(foreach v,$(CONVERT_VARIANTS),$(BUILD)/test_convert_$(v))

# $(call convert,<path>,<arguments>)
convert = if [ -n '$(CONVERT_NEEDS_$1)' ] && ! grep -qw '$(CONVERT_NEEDS_$1)' /proc/cpuinfo; then \
            printf 'convert         SKIP  %s: not on this CPU\n' '$(CONVERT_NAMES_$1)'; \
          else $(BUILD)/test_convert_$1 --expect '$(CONVERT_NAMES_$1)' $2; fi

CONVERT_NATIVE := $(addprefix check-convert-,$(CONVERT_VARIANTS))
CONVERT_TARGETS := $(CONVERT_NATIVE)
ifeq ($(ARCH),x86_64)
CONVERT_TARGETS += check-convert-neon check-convert-rvv
endif

# The RVV path runs at 128- and 1024-bit vectors, and built for Zve32f, the smallest vector
# extension it accepts.
RVV_ARCH := rv64gcv
RVV_ARCH_ZVE32F := rv64imafdc_zve32f
RVV_CPU = rv64,v=true,vlen=$1
RVV_CPU_ZVE32F := rv64,v=false,zve32f=true,vlen=128

.DEFAULT_GOAL := build
.DELETE_ON_ERROR:

build: $(BUILD)/libgm2.a

install: $(BUILD)/libgm2.a
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig $(DESTDIR)$(INCLUDEDIR)/gm2
	install -m 644 $(BUILD)/libgm2.a $(DESTDIR)$(LIBDIR)/libgm2.a
	install -m 644 $(LIB_HEADERS) $(DESTDIR)$(INCLUDEDIR)/gm2/
	printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	  'Name: gm2' 'Description: The G-Major 2 guitar effects, bit-exact with the hardware' \
	  'Version: $(VERSION)' 'Cflags: -I$${includedir}/gm2' 'Libs: -L$${libdir} -lgm2' \
	  > $(DESTDIR)$(LIBDIR)/pkgconfig/gm2.pc

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/libgm2.a $(DESTDIR)$(LIBDIR)/pkgconfig/gm2.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)/gm2

INSTALL_CHECK := build/install-check
CXX_STRICT := -std=c++17 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wsign-conversion

check-install:
	@rm -rf $(INSTALL_CHECK)
	@$(MAKE) --no-print-directory install PREFIX=$(CURDIR)/$(INSTALL_CHECK) >/dev/null
	@for header in $(notdir $(wildcard src/gm2_*_setup.h)); do \
	  grep -q "#include \"$$header\"" src/gm2.h || \
	    { echo "check-install: gm2.h does not include $$header" >&2; exit 1; }; \
	done
	@export PKG_CONFIG_PATH=$(CURDIR)/$(INSTALL_CHECK)/lib/pkgconfig && \
	  $(CC) $(STRICT) $(CFLAGS) $$($(PKG_CONFIG) --cflags gm2) -o $(INSTALL_CHECK)/host_c \
	    tests/test_install.c $$($(PKG_CONFIG) --libs gm2) && \
	  $(CXX) $(CXX_STRICT) $(CFLAGS) $$($(PKG_CONFIG) --cflags gm2) -o $(INSTALL_CHECK)/host_cxx \
	    -x c++ tests/test_install.c -x none $$($(PKG_CONFIG) --libs gm2) && \
	  $(INSTALL_CHECK)/host_c && $(INSTALL_CHECK)/host_cxx

check: $(BUILD)/gm2_check
	@$(BUILD)/gm2_check

check-fixture: $(BUILD)/gm2_check
	@if [ -z "$(FIXTURE)" ]; then echo 'usage: make check-fixture FIXTURE=<name>' >&2; exit 1; fi
	@$(BUILD)/gm2_check $(FIXTURE)

check-swf: $(BUILD)/gm2_check
	@$(BUILD)/gm2_check swf

check-tables: $(BUILD)/gm2_check
	@$(BUILD)/gm2_check tables

check-host: $(BUILD)/gm2_check
	@$(BUILD)/gm2_check host

benchmark-all-fx: $(BUILD)/gm2_bench
	@$(BUILD)/gm2_bench

benchmark-fx: $(BUILD)/gm2_bench
	@if [ -z "$(FX)" ]; then echo 'usage: make benchmark-fx FX=<name>' >&2; exit 1; fi
	@$(BUILD)/gm2_bench $(FX)

check-convert: $(CONVERT_TARGETS)

$(CONVERT_NATIVE): check-convert-%: $(BUILD)/test_convert_%
	@$(call convert,$*,--exhaustive)

ifeq ($(ARCH),x86_64)
check-convert-neon:
	@mkdir -p $(BUILD)
	@if command -v $(AARCH64_CC) >/dev/null 2>&1 && command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	  $(AARCH64_CC) $(STRICT) $(CFLAGS) -static $(INCLUDES) -o $(BUILD)/test_convert_neon_aarch64 \
	    tests/test_convert.c src/gm2_convert.c -lm && \
	  $(QEMU_AARCH64) $(BUILD)/test_convert_neon_aarch64 --expect NEON --exhaustive; \
	else \
	  printf 'convert         SKIP  NEON: no %s or %s\n' '$(AARCH64_CC)' '$(QEMU_AARCH64)'; \
	fi

check-convert-rvv:
	@mkdir -p $(BUILD)
	@if command -v $(RISCV64_CC) >/dev/null 2>&1 && command -v $(QEMU_RISCV64) >/dev/null 2>&1; then \
	  $(RISCV64_CC) $(STRICT) $(CFLAGS) -march=$(RVV_ARCH) -static $(INCLUDES) \
	    -o $(BUILD)/test_convert_rvv_riscv64 tests/test_convert.c src/gm2_convert.c -lm && \
	  $(RISCV64_CC) $(STRICT) $(CFLAGS) -march=$(RVV_ARCH_ZVE32F) -static $(INCLUDES) \
	    -o $(BUILD)/test_convert_zve32f_riscv64 tests/test_convert.c src/gm2_convert.c -lm && \
	  $(QEMU_RISCV64) -cpu $(call RVV_CPU,128) $(BUILD)/test_convert_rvv_riscv64 \
	    --expect RVV --exhaustive && \
	  $(QEMU_RISCV64) -cpu $(call RVV_CPU,1024) $(BUILD)/test_convert_rvv_riscv64 --expect RVV && \
	  $(QEMU_RISCV64) -cpu $(RVV_CPU_ZVE32F) $(BUILD)/test_convert_zve32f_riscv64 --expect RVV; \
	else \
	  printf 'convert         SKIP  RVV: no %s or %s\n' '$(RISCV64_CC)' '$(QEMU_RISCV64)'; \
	fi
endif

firmware:
	@$(PYTHON) $(RE)/fetch_firmware.py

record-all-fixtures: $(BUILD)/gm2_record firmware
	@mkdir -p tests/fixtures
	@names=$$($(BUILD)/gm2_record --list) && \
	  printf '%s\n' $$names | xargs -P $(JOBS) -I{} $(BUILD)/gm2_record {} tests/fixtures/{}.bin

record-fixture: $(BUILD)/gm2_record firmware
	@if [ -z "$(FIXTURE)" ]; then echo 'usage: make record-fixture FIXTURE=<name>' >&2; exit 1; fi
	@mkdir -p tests/fixtures
	@for name in $(FIXTURE); do \
	  $(BUILD)/gm2_record $$name tests/fixtures/$$name.bin || exit 1; \
	done

# The Ghidra project is built once: it may hold the user's own analysis.
ghidra-prepare: firmware
	@$(PYTHON) $(RE)/build_ghidra.py --firmware $(FIRMWARE) --prepare build/ghidra
	@$(PYTHON) $(RE)/build_workspace.py --input $(FIRMWARE) --output $(RE) \
	  --disassembler $(DISASSEMBLER)
	@if [ -e $(GHIDRA_PROJECT) ]; then \
	  echo '$(GHIDRA_PROJECT) exists and is kept'; \
	elif [ -d $(GHIDRA) ] && $(PYGHIDRA) -c 'import pyghidra' 2>/dev/null; then \
	  $(PYGHIDRA) $(RE)/build_ghidra.py --ghidra $(GHIDRA) --firmware $(FIRMWARE); \
	else \
	  echo 'SKIP the Ghidra project: it needs Ghidra in $(GHIDRA) and PyGhidra in $(PYGHIDRA)'; \
	  echo '     (GHIDRA= and PYGHIDRA= move them; reverse_engineering/README.md installs them)'; \
	fi

# The exports stay with the project: only building the project writes them.
ghidra-clean:
	rm -rf build/ghidra $(FIRMWARE) \
	  $(addprefix $(RE)/,asm callgraph memory_map.csv entrypoints.csv seeds.csv)

format:
	@$(CLANG_FORMAT) -i $(FORMAT_FILES)

static-check: static-format static-cppcheck static-clang static-gcc

static-format:
	@$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

# cppcheck 2.22 has no c17; C17 adds no rule cppcheck models, so c11 is the
# closest standard it accepts.
static-cppcheck:
	@$(CPPCHECK) --enable=warning,style,performance,portability \
	  --inline-suppr --error-exitcode=1 --suppress=missingIncludeSystem --quiet \
	  --std=c11 $(INCLUDES) $(C_FILES)

# $(call clang_check,<flags>,<file>): clang --analyze silences ordinary warnings, so they get
# their own pass.
clang_check = $(CLANG) $(STRICT) $(INCLUDES) $1 -fsyntax-only $2 && \
              $(CLANG) --analyze -Xclang -analyzer-werror -Xclang -analyzer-output=text \
                -o /dev/null -std=c17 $(INCLUDES) $1 $2

# $(call gcc_check,<compiler>,<flags>,<file>): the flow warnings need -O2; the analyzer sees
# most at -O0, before the optimizer deletes code.
gcc_check = $1 $(STRICT) -O2 $(INCLUDES) $2 -c $3 -o /dev/null && \
            $1 $(STRICT) -O0 -fanalyzer $(INCLUDES) $2 -c $3 -o /dev/null

static-clang:
	@printf '%s\n' $(C_FILES) | xargs -P $(JOBS) -n 1 sh -c '$(call clang_check,,"$$0")'
ifeq ($(ARCH),x86_64)
	@for flags in -DGM2_NO_SIMD -march=x86-64-v3 -march=x86-64-v4; do \
	  $(call clang_check,$$flags,src/gm2_convert.c) || exit 1; \
	done
	@if [ -d $(AARCH64_SYSROOT) ]; then \
	  $(call clang_check,--target=aarch64-linux-gnu --sysroot=$(AARCH64_SYSROOT),src/gm2_convert.c); \
	else \
	  echo 'SKIP static-clang NEON path: no $(AARCH64_SYSROOT)'; \
	fi
	@if [ -d $(RISCV64_SYSROOT) ]; then \
	  for arch in $(RVV_ARCH) $(RVV_ARCH_ZVE32F); do \
	    $(call clang_check,--target=riscv64-linux-gnu -march=$$arch --sysroot=$(RISCV64_SYSROOT),src/gm2_convert.c) || exit 1; \
	  done; \
	else \
	  echo 'SKIP static-clang RVV path: no $(RISCV64_SYSROOT)'; \
	fi
else
	@echo 'SKIP static-clang: the other conversion paths are checked on x86-64 only'
endif
	@echo 'static-clang: $(words $(C_FILES)) files clean'

static-gcc:
	@printf '%s\n' $(C_FILES) | xargs -P $(JOBS) -n 1 sh -c '$(call gcc_check,$(GCC),,"$$0")'
ifeq ($(ARCH),x86_64)
	@for flags in -DGM2_NO_SIMD -march=x86-64-v3 -march=x86-64-v4; do \
	  $(call gcc_check,$(GCC),$$flags,src/gm2_convert.c) || exit 1; \
	done
	@if command -v $(AARCH64_CC) >/dev/null 2>&1; then \
	  $(call gcc_check,$(AARCH64_CC),,src/gm2_convert.c); \
	else \
	  echo 'SKIP static-gcc NEON path: no $(AARCH64_CC)'; \
	fi
	@if command -v $(RISCV64_CC) >/dev/null 2>&1; then \
	  for arch in $(RVV_ARCH) $(RVV_ARCH_ZVE32F); do \
	    $(call gcc_check,$(RISCV64_CC),-march=$$arch,src/gm2_convert.c) || exit 1; \
	  done; \
	else \
	  echo 'SKIP static-gcc RVV path: no $(RISCV64_CC)'; \
	fi
else
	@echo 'SKIP static-gcc: the other conversion paths are checked on x86-64 only'
endif
	@echo 'static-gcc: $(words $(C_FILES)) files clean'

SANITIZE_FLAGS := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
                  -fno-omit-frame-pointer

sanitize: sanitize-gcc sanitize-clang

sanitize-gcc:
	@$(MAKE) --no-print-directory BUILD=build/sanitize-gcc CC='$(GCC)' \
	  CFLAGS='$(SANITIZE_FLAGS)' exercise

sanitize-clang:
	@$(MAKE) --no-print-directory BUILD=build/sanitize-clang CC='$(CLANG)' \
	  CFLAGS='$(SANITIZE_FLAGS)' exercise

exercise: $(BUILD)/gm2_check $(CONVERT_BINS)
	@$(BUILD)/gm2_check
	@$(foreach v,$(CONVERT_VARIANTS),$(call convert,$(v),) &&) true

# gcov runs inside build/coverage, so the sources are recorded by absolute path.
# Each converter test compiles its own gm2_convert.c, and all of them count.
coverage:
	@rm -rf build/coverage
	@$(MAKE) --no-print-directory BUILD=build/coverage \
	  CFLAGS='-O0 -g --coverage -fprofile-abs-path' exercise
	@cd build/coverage && gcov -b src/*.gcda test_convert_*-gm2_convert.gcda 2>/dev/null | \
	  awk -v prefix='$(CURDIR)/src/' '/^File / { path = substr($$2, 2, length($$2) - 2); next } \
	    /^Lines executed:/ { lines = substr($$0, 16); wanted = (index(path, prefix) == 1); next } \
	    /^Branches executed:/ { branches = substr($$0, 19); next } \
	    /^No branches/ { branches = "none"; next } \
	    /^Calls executed:|^No calls/ { if (wanted) { name = path; sub(/.*\//, "", name); \
	    if (!(name in seen)) { seen[name] = 1; \
	    printf "%-16s lines %-18s branches %s\n", name, lines, branches } } \
	    wanted = 0; next }'

clean:
	rm -rf build

$(BUILD)/gm2_check: $(CHECK_OBJECTS) $(BUILD)/libgm2.a
	$(CC) $(STRICT) $(CFLAGS) -o $@ $^ -lm

$(BUILD)/gm2_record: $(RECORD_OBJECTS) $(BUILD)/libgm2.a
	$(CC) $(STRICT) $(CFLAGS) -o $@ $^ $(EMU_LDLIBS)

$(BUILD)/gm2_bench: $(BENCH_OBJECTS) $(BUILD)/libgm2.a
	$(CC) $(STRICT) $(CFLAGS) -o $@ $^ -lm

$(BUILD)/libgm2.a: $(LIB_OBJECTS)
	$(RM) $@
	ar rcs $@ $^

$(BUILD)/test_convert_%: tests/test_convert.c src/gm2_convert.c src/gm2_convert.h Makefile
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(CONVERT_FLAGS_$*) $(INCLUDES) -o $@ \
	  tests/test_convert.c src/gm2_convert.c -lm

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(LIB_FLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

-include $(DEPS)

.PHONY: build install uninstall check-install check check-fixture check-swf check-tables \
        check-host benchmark-all-fx benchmark-fx check-convert $(CONVERT_TARGETS) \
        firmware record-all-fixtures record-fixture ghidra-prepare ghidra-clean format static-check \
        static-format static-cppcheck static-clang static-gcc \
        sanitize sanitize-gcc sanitize-clang exercise coverage clean
