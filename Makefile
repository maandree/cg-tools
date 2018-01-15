PREFIX = /usr
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
MANDIR = $(DATADIR)/man
MAN1DIR = $(MANDIR)/man1
MAN7DIR = $(MANDIR)/man7
LICENSEDIR = $(DATADIR)/licenses


PKGNAME = cg-tools


BIN = bin/cg-brilliance \
      bin/cg-darkroom \
      bin/cg-gamma \
      bin/cg-icc \
      bin/cg-limits \
      bin/cg-linear \
      bin/cg-negative \
      bin/cg-query \
      bin/cg-rainbow \
      bin/cg-remove \
      bin/cg-sleepmode \
      bin/cg-shallow

MAN1 = man/cg-brilliance.1 \
       man/cg-darkroom.1 \
       man/cg-gamma.1 \
       man/cg-icc.1 \
       man/cg-limits.1 \
       man/cg-linear.1 \
       man/cg-negative.1 \
       man/cg-query.1 \
       man/cg-rainbow.1 \
       man/cg-remove.1 \
       man/cg-sleepmode.1 \
       man/cg-shallow.1

MAN7 = man/cg-tools.7


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


install: install-base install-doc

install-base: install-cmd install-copyright

install-copyright: install-license install-copying

install-doc: install-man

install-man: install-man1 install-man7

install-cmd: $(BIN)
	mkdir -p -- "$(DESTDIR)$(BINDIR)"
	install -m755 -- $(BIN) "$(DESTDIR)$(BINDIR)"

install-license:
	mkdir -p -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)"
	install -m644 -- LICENSE "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/LICENSE"

install-copying:
	mkdir -p -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)"
	install -m644 -- COPYING "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/COPYING"

install-man1:
	mkdir -p -- "$(DESTDIR)$(MAN1DIR)"
	install -m644 -- $(MAN1) "$(DESTDIR)$(MAN1DIR)"

install-man7:
	mkdir -p -- "$(DESTDIR)$(MAN7DIR)"
	install -m644 -- $(MAN7) "$(DESTDIR)$(MAN7DIR)"


uninstall:
	-cd "$(DESTDIR)$(BINDIR)" && rm -- $$(printf -- '%s\n' $(BIN) | cut -d / -f 2-)
	-rm -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/LICENSE"
	-rm -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/COPYING"
	-rmdir -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)"
	-cd "$(DESTDIR)$(MAN1DIR)" && rm -- $$(printf -- '%s\n' $(MAN1) | cut -d / -f 2-)
	-cd "$(DESTDIR)$(MAN7DIR)" && rm -- $$(printf -- '%s\n' $(MAN7) | cut -d / -f 2-)


clean:
	-rm -r -- bin obj

.PHONY: all clean install install-base install-doc install-cmd install-copyright \
	install-license install-copying install-man install-man1 install-man7 uninstall
