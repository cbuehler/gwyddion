# @(#) $Id$

# Put sources that are for some reason disabled yet present in svn checkouts
# here.  This makes them distributed and translations of their messages are
# kept.  If we remove them completely, various bits can get lost.
disabled_sources =

xyzdir = $(pkglibdir)/modules/xyz

xyz_LTLIBRARIES = \
	xyz_drift.la \
	xyz_level.la \
	xyz_raster.la

EXTRA_DIST = \
	$(disabled_sources)

if OS_MSWIN
no_undefined = -no-undefined
endif

AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"Module\"
AM_CFLAGS = @COMMON_CFLAGS@
AM_LDFLAGS = -avoid-version -module $(no_undefined) $(module_libadd)

if MODULE_DEPENDENCIES
module_libadd = \
	$(top_builddir)/app/libgwyapp2.la \
	$(top_builddir)/libdraw/libgwydraw2.la \
	$(top_builddir)/libgwyddion/libgwyddion2.la \
	$(top_builddir)/libgwydgets/libgwydgets2.la \
	$(top_builddir)/libgwymodule/libgwymodule2.la \
	$(top_builddir)/libprocess/libgwyprocess2.la \
	@GTK_LIBS@
endif

xyz_drift_la_SOURCES  = xyz_drift.c
xyz_level_la_SOURCES  = xyz_level.c
xyz_raster_la_SOURCES = xyz_raster.c

clean-local:
	rm -f core.* *~

