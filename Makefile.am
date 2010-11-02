if HAVE_JACK
jackdir = $(libdir)/$(PACKAGE)
pkglib_LTLIBRARIES = jack.la
AM_CFLAGS = $(CFLAGS) $(JACK_CFLAGS) -std=c99
jack_la_SOURCES = jack.c bio2jack.c bio2jack.h
jack_la_LDFLAGS = -module -ljack -ldl -L/usr/lib
endif
