## Process this file with automake to produce Makefile.in
include $(top_srcdir)/Makefile.decl

libgdkincludedir = $(includedir)/gtk-3.0/gdk

AM_CPPFLAGS =					\
	-DG_LOG_DOMAIN=\"Gdk\"			\
	-DGDK_COMPILATION			\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/gdk			\
	-I$(top_builddir)/gdk			\
	$(GTK_DEBUG_FLAGS)			\
	$(GDK_DEP_CFLAGS)			\
	-I/usr/include/mircommon 		\
	-I/usr/include/mirclient

LDADDS = $(GDK_DEP_LIBS)

noinst_LTLIBRARIES =				\
	libgdk-mir.la

libgdk_mir_la_SOURCES =				\
	gdkapplaunchcontext-mir.c		\
	gdkcursor-mir.c				\
	gdkdevice-mir.c				\
	gdkdisplay-mir.c			\
        gdkdisplay-mir.h			\
	gdkdnd-mir.c				\
	gdkkeys-mir.c				\
	gdkscreen-mir.c				\
	gdkselection-mir.c			\
	gdkwindow-mir.c				\
        gdkeventsource.c			\
        gdkmireventlistener.c                   \
	gdkmir.h				\
	gdkprivate-mir.h

libgdkinclude_HEADERS =				\
	gdkmir.h

-include $(top_srcdir)/git.mk
