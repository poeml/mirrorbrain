prefix      = /usr/local
exec_prefix = $(prefix)
bindir      = $(exec_prefix)/bin
mandir      = $(prefix)/share/man
sysconfdir  = $(prefix)/etc
datadir     = $(prefix)/share
docdir      = $(datadir)/doc/packages

BINDIR =	$(PREFIX)/bin
MANDIR =	$(PREFIX)/share/man

INSTALL =	install
RONN =		ronn-1.9


null-rsync.1: null-rsync.1.ronn
	$(RONN) --roff $<
	$(RONN) --html $<

install: null-rsync null-rsync.1
	$(INSTALL) -d $(DESTDIR)$(bindir)
	$(INSTALL) -d $(DESTDIR)$(mandir)/man1
	$(INSTALL) -d $(DESTDIR)$(docdir)/mirrorbrain-tools
	$(INSTALL) null-rsync $(DESTDIR)$(bindir)
	$(INSTALL) -m 0644 null-rsync.1 $(DESTDIR)$(mandir)/man1
	$(INSTALL) -m 0644 null-rsync.1.html README.md $(DESTDIR)$(docdir)/mirrorbrain-tools

