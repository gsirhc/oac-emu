# Williams Joust (1982) arcade board emulator.
#
#   make test    GoogleTest suite (Williams chips + board + board smoke).
#   make test-smoke  Smoke.* GoogleTest only (what deploy CI runs)
#   make riddle  fetch Sean Riddle's real-hardware blit dumps for Riddle.*
#   make check   riddle + test (what nightly runs)
#   make coverage  board-code llvm-cov report
#   make wasm    WebAssembly module
#   make serve   wasm + test ROM + shared chrome, serve on :8000
#   make clean

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

# llvm tools: xcrun on macOS, PATH on Linux
LLVM_PROFDATA ?= xcrun llvm-profdata
LLVM_COV      ?= xcrun llvm-cov

.PHONY: help test check wasm serve clean roms test-smoke coverage riddle

help:
	@sed -n 's/^#   //p' $(MAKEFILE_LIST)

test:
	cmake -S tests -B tests/build
	cmake --build tests/build -j
	ctest --test-dir tests/build --output-on-failure

test-smoke:
	cmake -S tests -B tests/build
	cmake --build tests/build -j --target smoke_test
	ctest --test-dir tests/build -R '^Smoke\.' --output-on-failure

# 256 blits on real Joust boards, RAM dumped after each (seanriddle.com/blitter.html).
RIDDLE_URL = https://seanriddle.com/blitdump.zip
RIDDLE_SHA = 40e8b6398fc1548fa8f6b040fe8b8160bc9b38af665b0595a3d02d9d61a5d58d

riddle/blitdump.bin:
	mkdir -p riddle
	curl -sSfL -o riddle/blitdump.zip $(RIDDLE_URL)
	echo "$(RIDDLE_SHA)  riddle/blitdump.zip" | shasum -a 256 -c
	unzip -o -q riddle/blitdump.zip -d riddle
	rm -f riddle/blitdump.zip

riddle: riddle/blitdump.bin

# --- C++ coverage (source-based, llvm-cov) --------------------------
COV_TESTS = williams_test machine_test smoke_test
COV_SRCS  = machine.cpp ../shared/williams/pia6821.cpp ../shared/williams/blitter.cpp ../shared/williams/sound_board.cpp
coverage:
	cmake -S tests -B tests/build-cov -DCOVERAGE=ON >/dev/null
	cmake --build tests/build-cov -j >/dev/null
	@set -e; cd tests/build-cov && rm -f *.profraw cov.profdata; \
	  for t in $(COV_TESTS); do LLVM_PROFILE_FILE=$$t.profraw ./$$t >/dev/null; done; \
	  $(LLVM_PROFDATA) merge -sparse *.profraw -o cov.profdata; \
	  objs=$$(echo $(COV_TESTS) | sed 's/ / -object /g'); \
	  srcs="$(addprefix $(CURDIR)/,$(COV_SRCS))"; \
	  $(LLVM_COV) report $$objs -instr-profile=cov.profdata $$srcs; \
	  $(LLVM_COV) show $$objs -instr-profile=cov.profdata -format=html -output-dir=../coverage-cpp $$srcs >/dev/null; \
	  echo "  html: joust/tests/coverage-cpp/index.html"

roms:
	$(MAKE) -C web roms

wasm:
	$(MAKE) -C web

serve:
	$(MAKE) -C web serve

check: riddle test

clean:
	rm -rf tests/build tests/build-cov tests/coverage-cpp riddle
	$(MAKE) -C web clean
