# This Makefile.am is in the public domain
AM_CPPFLAGS = -I$(top_srcdir)/src/include $(POSTGRESQL_CPPFLAGS)

if MINGW
  WINFLAGS = -Wl,--no-undefined -Wl,--export-all-symbols
endif

if USE_COVERAGE
  AM_CFLAGS = --coverage
endif

if HAVE_POSTGRESQL
lib_LTLIBRARIES = libgnunetpostgres.la
endif

libgnunetpostgres_la_SOURCES = \
  postgres.c
libgnunetpostgres_la_LIBADD = -lpq \
 $(top_builddir)/src/util/libgnunetutil.la
libgnunetpostgres_la_LDFLAGS = \
 $(POSTGRESQL_LDFLAGS) \
 $(GN_LIB_LDFLAGS) \
  -version-info 0:0:0

