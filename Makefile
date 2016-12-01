PKGNAME = cg-tools


BIN = bin/cg-brilliance \
      bin/cg-darkroom \
      bin/cg-gamma \
      bin/cg-icc \
      bin/cg-limits \
      bin/cg-negative \
      bin/cg-query \
      bin/cg-rainbow \
      bin/cg-remove \
      bin/cg-sleepmode

OPTIMISE = -O2

WARN = -Wall -Wextra

DEF = -D'PKGNAME="$(PKGNAME)"' -D_DEFAULT_SOURCE -D_BSD_SOURCE


all: $(BIN)

bin/%: obj/%.o obj/cg-base.o
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) -std=c99 $(OPTIMISE) $(WARN) $(DEF) -o $@ $^ $(LDFLAGS) -lm -lcoopgamma

bin/cg-query: obj/cg-query.o
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) -std=c99 $(OPTIMISE) $(WARN) $(DEF) -o $@ $^ $(LDFLAGS) -lm -lcoopgamma

bin/cg-remove: obj/cg-remove.o
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) -std=c99 $(OPTIMISE) $(WARN) $(DEF) -o $@ $^ $(LDFLAGS) -lm -lcoopgamma

obj/%.o: src/%.c src/*.h
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) -std=c99 $(OPTIMISE) $(WARN) $(DEF) -c -o $@ $< $(CFLAGS) $(CPPFLAGS)

clean:
	-rm -r -- bin obj

.PHONY: all clean
