AC_INIT([x11spice], 1.2)
AM_INIT_AUTOMAKE([subdir-objects])

PKG_CHECK_MODULES(XCB, xcb)
PKG_CHECK_MODULES(DAMAGE, xcb-damage)
PKG_CHECK_MODULES(XTEST, xcb-xtest)
PKG_CHECK_MODULES(SHM, xcb-shm)
PKG_CHECK_MODULES(UTIL, xcb-util)
PKG_CHECK_MODULES(XKB, xcb-xkb)
PKG_CHECK_MODULES(XFIXES, xcb-xfixes)
PKG_CHECK_EXISTS(gtk+-2.0, GTK_VERSION=2.0, GTK_VERSION=3.0)
PKG_CHECK_MODULES(GTK, gtk+-$GTK_VERSION)
PKG_CHECK_MODULES(SPICE, spice-server)
PKG_CHECK_MODULES(SPICE_PROTOCOL, spice-protocol)
PKG_CHECK_MODULES(GLIB2, glib-2.0)
PKG_CHECK_MODULES(PIXMAN, pixman-1)

AM_CONDITIONAL([HAVE_GTEST], [pkg-config --atleast-version=2.38 glib-2.0])

# Fedora likes to harden builds.  We cannot use the hardening flags on the
#  spice-video-dummy, as -znow causes Xorg conventions to fail.
#  We use these flags to pass the hardening options only to x11spice.
AC_SUBST([X11SPICE_ONLY_CFLAGS])
AC_SUBST([X11SPICE_ONLY_LDFLAGS])

AC_ARG_ENABLE([dummy], AS_HELP_STRING([--enable-dummy], [Builds the spice-video-dummy driver]), [dummy=true])
AM_CONDITIONAL([DUMMY], test x$dummy = xtrue)

AS_IF([test "x$dummy" = "xtrue"], [
  AC_CONFIG_SUBDIRS([spice-video-dummy])
])

AC_PROG_SED
# To use gcov, make sure you have the appropriate autoconf macros,
# and uncomment this line, and the matching one in src/Makefile.am.
#AX_CODE_COVERAGE()

AC_CHECK_HEADERS(libaudit.h)
AC_CHECK_LIB(audit, audit_open)

AC_PROG_CC
AC_CONFIG_FILES(Makefile src/Makefile src/tests/Makefile)
AC_OUTPUT
