# libddst + tests. POSIX make, clang.
CC      = clang
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2
JBIG_PREFIX ?= $(shell brew --prefix jbigkit 2>/dev/null || echo /opt/homebrew/opt/jbigkit)
JBIG_CFLAGS = -I$(JBIG_PREFIX)/include
JBIG_LIBS   = $(JBIG_PREFIX)/lib/libjbig.a

BUILD   = build
LIB     = $(BUILD)/libddst.a
LIB_SRC = src/ddst/ddst.c src/ddst/pbm.c src/ddst/dither.c src/ddst/paper.c
LIB_OBJ = $(LIB_SRC:src/ddst/%.c=$(BUILD)/obj/%.o)
LIB_HDR = $(wildcard src/ddst/*.h)

TESTS   = test_pjl test_jbig test_dither test_paper
TEST_BIN = $(TESTS:%=$(BUILD)/tests/%)

.PHONY: all libddst.a test clean golden

all: $(LIB)

libddst.a: $(LIB)

$(BUILD)/obj/%.o: src/ddst/%.c $(LIB_HDR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(JBIG_CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJ)
	@mkdir -p $(@D)
	rm -f $@
	ar rcs $@ $(LIB_OBJ)

$(BUILD)/tests/%: tests/%.c tests/test_util.h $(LIB) $(LIB_HDR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(JBIG_CFLAGS) $< $(LIB) $(JBIG_LIBS) -o $@

GOLDEN = tests/golden/text.jbg tests/golden/photo.jbg tests/golden/odd-width.jbg

GOLDEN_STAMP = tests/golden/.stamp

$(GOLDEN_STAMP): tests/golden/make-golden.sh tests/golden/gen-pbm.c
	JBIG_PREFIX=$(JBIG_PREFIX) sh tests/golden/make-golden.sh
	touch $@

$(GOLDEN): $(GOLDEN_STAMP)

golden: $(GOLDEN_STAMP)

test: $(TEST_BIN) $(GOLDEN_STAMP)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; ./$$t; done

clean:
	rm -rf $(BUILD)
