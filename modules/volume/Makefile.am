# @(#) $Id$

# Put sources that are for some reason disabled yet present in svn checkouts
# here.  This makes them distributed and translations of their messages are
# kept.  If we remove them completely, various bits can get lost.
disabled_sources =

volumedir = $(pkglibdir)/modules/volume

volume_LTLIBRARIES = \
	volume_calibrate.la \
	volume_extract.la \
	volume_fdfit.la \
	volume_invert.la \
	volume_kmeans.la \
	volume_kmedians.la \
	volume_linestat.la \
	volume_slice.la \
	volume_zcal.la

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

volume_calibrate_la_SOURCES = volume_calibrate.c
volume_extract_la_SOURCES   = volume_extract.c
volume_fdfit_la_SOURCES     = volume_fdfit.c
volume_invert_la_SOURCES    = volume_invert.c
volume_kmeans_la_SOURCES    = volume_kmeans.c
volume_kmedians_la_SOURCES  = volume_kmedians.c
volume_linestat_la_SOURCES  = volume_linestat.c
volume_slice_la_SOURCES     = volume_slice.c
volume_zcal_la_SOURCES      = volume_zcal.c

clean-local:
	rm -f core.* *~

