PREFIX    = /usr
MANPREFIX = $(PREFIX)/share/man

PKGNAME = cg-tools

CC = cc

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700 -D'PKGNAME="$(PKGNAME)"'
CFLAGS   = -std=c99 -Wall -O2
LDFLAGS  = -lcoopgamma -lm -s
