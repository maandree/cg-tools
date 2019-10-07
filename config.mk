PREFIX = /usr
MANPREFIX = $(PREFIX)/share/man

PKGNAME = cg-tools

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D'PKGNAME="$(PKGNAME)"' 
CFLAGS   = -std=c99 -Wall -Og -g
LDFLAGS  = -lcoopgamma -lm
