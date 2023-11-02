## Process this file with automake to produce Makefile.in

SUBDIRS = src po
man_MANS = grsync.1 grsync-batch.1

EXTRA_DIST = \
	autogen.sh \
	grsync.glade \
	grsync.conf \
	grsync.desktop.in \
	grsync.xml.in \
	it.opbyte.grsync.service.in \
	grsync.spec.in \
	$(man_MANS) \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	src/grsync-batch \
	pixmaps

distclean-local:
	rm -rf *.cache *~

am__tar = @am__tar@ --exclude=".svn"

Applicationsdir = $(datadir)/applications
Applications_DATA = grsync.desktop

Gladedir = $(datadir)/grsync
Glade_DATA = $(XMLFILE)

mimedir = $(datadir)/mime
mimepackagesdir = $(mimedir)/packages
dist_mimepackages_DATA = grsync.xml

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = pixmaps/grsync.png

pixmapbusydir = $(datadir)/pixmaps
pixmapbusy_DATA = pixmaps/grsync-busy.png

mimeicondir = $(datadir)/icons/hicolor/48x48/mimetypes/
mimeicon_DATA = pixmaps/application-x-grsync-session.png
