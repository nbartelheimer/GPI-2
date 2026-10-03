AC_DEFUN([ACX_PORTALS4],[
  AC_ARG_VAR([PORTALS4_CFLAGS], [C compiler flags for Portals4])
  AC_ARG_VAR([PORTALS4_LIBS], [linker flags for Portals4])
  if test "x$with_portals4" != xyes; then
    PORTALS4_CFLAGS="-I$with_portals4/include $PORTALS4_CFLAGS"
    if test -z "$PORTALS4_LIBS"; then
      for portals4_libdir in lib64 lib lib/`$CC -dumpmachine`; do
        if test -f "$with_portals4/$portals4_libdir/libportals.so" -o -f "$with_portals4/$portals4_libdir/libportals.a"; then
          PORTALS4_LIBS="-L$with_portals4/$portals4_libdir -lportals"
          break
        fi
      done
      if test -z "$PORTALS4_LIBS"; then
        AC_MSG_ERROR([No Portals4 library found under $with_portals4; set PORTALS4_LIBS explicitly])
      fi
    fi
  fi
  if test -z "$PORTALS4_LIBS"; then
    PORTALS4_LIBS=-lportals
  fi
  portals4_save_CPPFLAGS=$CPPFLAGS
  portals4_save_LIBS=$LIBS
  CPPFLAGS="$CPPFLAGS $PORTALS4_CFLAGS"
  LIBS="$PORTALS4_LIBS $LIBS"
  AC_MSG_CHECKING([whether Portals4 headers and library are usable])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <portals4.h>]], [[
    ptl_handle_ni_t ni;
    ptl_ni_limits_t limits = {0};
    int ret = PtlInit();
    ret += PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_PHYSICAL | PTL_NI_NO_MATCHING,
                     PTL_PID_ANY, &limits, &limits, &ni);
    ret += PtlNIFini(ni);
    PtlFini();
    return ret;
  ]])], [HAVE_PORTALS4=1], [HAVE_PORTALS4=0])
  CPPFLAGS=$portals4_save_CPPFLAGS
  LIBS=$portals4_save_LIBS
  if test "$HAVE_PORTALS4" = 1; then
    AC_MSG_RESULT([yes])
    AC_SUBST([ac_lib_portals4], [$PORTALS4_LIBS])
  else
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Portals4 requested but unusable; set PORTALS4_CFLAGS and PORTALS4_LIBS or use --with-portals4=PREFIX])
  fi
])
