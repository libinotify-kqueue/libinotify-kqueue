AC_INIT([package], [version])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AC_PROG_LIBTOOL
AC_PROG_CXX
AC_CONFIG_FILES([Makefile])

case "$host_os" in
*linux*)
   OS=Linux
   ;;
*freebsd*)
   OS=FreeBSD
   ;;
esac

AM_CONDITIONAL(LINUX, [test "$OS" = "Linux"])
AM_CONDITIONAL(FREEBSD, [test "$OS" = "FreeBSD"])


AC_ARG_ENABLE([perrors],
    AS_HELP_STRING([--enable-perrors], [enable error messages in the library]),
    AC_DEFINE([ENABLE_PERRORS],[1],[Enable error messages])
)


kqueue_support=no
AC_CHECK_HEADERS([sys/event.h],
[
    AC_CHECK_FUNCS(kqueue,,AC_MSG_ERROR(No kqueue detected in your system!))
    AC_CHECK_FUNCS(kevent,,AC_MSG_ERROR(No kevent detected in your system!))
    kqueue_support=yes
],
[
    AM_COND_IF(LINUX,
    [
        echo "Host system in GNU/Linux, enabling target "test" only"
        kqueue_support=no
    ],
    AC_MSG_ERROR(No sys/kqueue.h found in your system!))
])

AM_CONDITIONAL(BUILD_LIBRARY, [test "$kqueue_support" = "yes"])


AC_OUTPUT
