## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@

bin_PROGRAMS = grsync
bin_SCRIPTS = grsync-batch

grsync_SOURCES = \
	main.c \
	support.h \
	callbacks.c callbacks.h

grsync_LDADD = @PACKAGE_LIBS@ $(INTLLIBS)

