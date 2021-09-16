.POSIX:

CONFIGFILE = config.mk
include $(CONFIGFILE)

PKGNAME = cg-tools

XOUT =\
	cg-brilliance\
	cg-darkroom\
	cg-gamma\
	cg-icc\
	cg-limits\
	cg-linear\
	cg-negative\
	cg-rainbow\
	cg-sleepmode\
	cg-shallow

XBIN =\
	cg-query\
	cg-remove

HDR =\
	arg.h\
	cg-base.h

BIN = $(XBIN) $(XOUT)
OUT = $(XOUT:=.out)
OBJ = $(BIN:=.o) cg-base.o
MAN1 = $(BIN:=.1)
MAN7 = cg-tools.7

all: $(XBIN) $(OUT)
$(OBJ): $(HDR)
$(OUT): cg-base.o

.c.o:
	$(CC) -c -o $@ $< $(CPPFLAGS) $(CFLAGS)

.o.out:
	$(CC) -o $@ $< cg-base.o $(LDFLAGS)

cg-query: cg-query.o
	$(CC) -o $@ $@.o $(LDFLAGS)

cg-remove: cg-remove.o
	$(CC) -o $@ $@.o $(LDFLAGS)

install: $(XBIN) $(OUT)
	mkdir -p -- "$(DESTDIR)$(PREFIX)/bin"
	mkdir -p -- "$(DESTDIR)$(MANPREFIX)/man1"
	mkdir -p -- "$(DESTDIR)$(MANPREFIX)/man7"
	cp -- $(XBIN) "$(DESTDIR)$(PREFIX)/bin"
	for x in $(XOUT); do cp -- "$$x.out" "$(DESTDIR)$(PREFIX)/bin/$$x" || exit 1; done
	cp -- $(MAN1) "$(DESTDIR)$(MANPREFIX)/man1"
	cp -- $(MAN7) "$(DESTDIR)$(MANPREFIX)/man7"

uninstall:
	-cd -- "$(DESTDIR)$(PREFIX)/bin" && rm -f -- $(XBIN) $(XOUT)
	-cd -- "$(DESTDIR)$(MANPREFIX)/man1" && rm -f -- $(MAN1)
	-cd -- "$(DESTDIR)$(MANPREFIX)/man7" && rm -f -- $(MAN7)

clean:
	-rm -f -- $(BIN) *.o *.su *.out

.SUFFIXES:
.SUFFIXES: .c .o .out

.PHONY: all install uninstall clean
