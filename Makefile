
CFLAGS = -fPIC -O3 -Wall
CINCL  = -I src -I deps/discodb/src
CLIBS  = $(foreach L,Judy cmph m,-l$(L))
CSRCS  = $(wildcard src/*.c src/dsfmt/dSFMT.c deps/discodb/src/*.c)
COBJS  = $(patsubst %.c,%.o,$(CSRCS))

PYTHON = python

.PHONY: all bins libs clean python

all: bins libs

bins: bin/encode bin/index bin/merge #bin/mix

libs: lib/libtraildb.a lib/libtraildb.so

clean:
	rm -f $(COBJS)
	rm -f bin/encode bin/index bin/mix
	rm -f lib/libtraildb.*

bin/merge: trail-merge/*.c $(COBJS)
	$(CC) $(CFLAGS) $(CINCL) -I $(<D) $(CLIBS) -o $@ $^

bin/mix: trail-mix/*.c trail-mix/ops/*.c $(COBJS)
	$(CC) $(CFLAGS) $(CINCL) -I $(<D) $(CLIBS) -o $@ $^

bin/%: bin/%.c $(COBJS)
	$(CC) $(CFLAGS) $(CINCL) $(CLIBS) -o $@ $^

deps/discodb/src/%.o:
	make -C deps/discodb CFLAGS="$(CFLAGS)"

deps/%.git:
	git submodule update --init $(@D)

lib/libtraildb.a: $(COBJS)
	$(AR) -ruvs $@ $^

lib/libtraildb.so: $(COBJS)
	$(CC) $(CFLAGS) $(CINCL) $(CLIBS) -shared -o $@ $^

src/dsfmt/%.o: src/dsfmt/%.c
	$(CC) $(CFLAGS) $(CINCL) -DDSFMT_MEXP=521 -DHAVE_SSE2=1 -msse2 -c -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(CINCL) -DENABLE_COOKIE_INDEX -c -o $@ $^

trail-mix/%.c: trail-mix/ops.h

trail-mix/ops.h: trail-mix/generate-ops.sh Makefile
	$< > $@

python: CMD = build
python:
	(cd lib/python && $(PYTHON) setup.py $(CMD))
