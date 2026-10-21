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

# Hardware-test tools (stage 0a)
TOOLS_BIN = $(BUILD)/tools/mkgrid $(BUILD)/tools/pbm2ddst

.PHONY: tools test-tools

tools: $(TOOLS_BIN)

$(BUILD)/tools/mkgrid: tools/mkgrid.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/tools/pbm2ddst: tools/pbm2ddst.c $(LIB) $(LIB_HDR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(JBIG_CFLAGS) $< $(LIB) $(JBIG_LIBS) -o $@

test-tools: tools
	sh tests/test_tools.sh $(BUILD)/tools

# --- install (Task 7) ---
PREFIX ?= /usr/local

.PHONY: install uninstall

install: app
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BUILD)/ricoh-sp200-printer-app $(DESTDIR)$(PREFIX)/bin/ricoh-sp200-printer-app

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ricoh-sp200-printer-app

clean:
	rm -rf $(BUILD)

# --- PAPPL printer application + mock integration test (Task 6) ---
PAPPL_CFLAGS ?= $(shell pkg-config --cflags pappl)
PAPPL_LIBS   ?= $(shell pkg-config --libs pappl)
APP      = $(BUILD)/ricoh-sp200-printer-app
APP_SRC  = src/app/main.c src/app/driver.c src/app/device.c
APP_HDR  = src/app/driver.h src/app/device.h
IT_BIN   = $(BUILD)/integration/mock-sink $(BUILD)/integration/mkpwg $(BUILD)/integration/check-ddst \
           $(BUILD)/integration/mkpng $(BUILD)/integration/test-autoadd

.PHONY: app integration

app: $(APP)

$(APP): $(APP_SRC) $(APP_HDR) $(LIB) $(LIB_HDR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(PAPPL_CFLAGS) $(APP_SRC) $(LIB) $(JBIG_LIBS) $(PAPPL_LIBS) -o $@

$(BUILD)/integration/mock-sink: tests/integration/mock-sink.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/integration/mkpwg: tests/integration/mkpwg.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Wno-deprecated-declarations $< -lcups -o $@

$(BUILD)/integration/check-ddst: tests/integration/check-ddst.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(JBIG_CFLAGS) $< $(JBIG_LIBS) -o $@

$(BUILD)/integration/mkpng: tests/integration/mkpng.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -lz -o $@

$(BUILD)/integration/test-autoadd: tests/integration/test-autoadd.c src/app/driver.c src/app/device.c $(APP_HDR) $(LIB) $(LIB_HDR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(PAPPL_CFLAGS) $< src/app/driver.c src/app/device.c $(LIB) $(JBIG_LIBS) $(PAPPL_LIBS) -o $@

integration: $(APP) $(IT_BIN)
	sh tests/integration/run.sh
