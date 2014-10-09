AC_INIT([package], [version])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
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


AX_PTHREAD([], AC_MSG_ERROR(No pthread library found in your system!))


kqueue_support=no
AC_CHECK_HEADERS([sys/event.h],
[
    AC_CHECK_FUNCS(kqueue,,AC_MSG_ERROR(No kqueue detected in your system!))
    AC_CHECK_FUNCS(kevent,,AC_MSG_ERROR(No kevent detected in your system!))
    kqueue_support=yes
],
[
    if [test "$OS" = "Linux"]; then
        echo "Host system in GNU/Linux, enabling target \"test\" only"
        kqueue_support=no
    else
        AC_MSG_ERROR(No sys/kqueue.h found in your system!)
    fi
])
AM_CONDITIONAL(BUILD_LIBRARY, [test "$kqueue_support" = "yes"])


AC_MSG_CHECKING(for SLIST_FOREACH_SAFE in sys/queue.h)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/queue.h>
    ],
    [
        @%:@ifndef SLIST_FOREACH_SAFE
        @%:@error
        @%:@endif
        return 0;
    ])
],
    have_sys_queue_h=yes,
    have_sys_queue_h=no
)
AC_MSG_RESULT($have_sys_queue_h)


AC_MSG_CHECKING(for SLIST_REMOVE_AFTER in sys/queue.h)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/queue.h>
    ],
    [
        @%:@ifndef SLIST_REMOVE_AFTER
        @%:@error
        @%:@endif
        return 0;
    ])
],
    AC_MSG_RESULT(yes),
[
    AC_MSG_RESULT(no)
    have_sys_queue_h=no
],
)
if test "$have_sys_queue_h" = "yes"; then
    AC_DEFINE([HAVE_SYS_QUEUE_H],[1],[Define to 1 if the system has useable sys/queue.h])
fi


AC_MSG_CHECKING(for RB_FOREACH_SAFE in sys/tree.h)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/tree.h>
    ],
    [
        @%:@ifndef RB_FOREACH_SAFE
        @%:@error
        @%:@endif
        return 0;
    ])
],
    have_sys_tree_h=yes,
    have_sys_tree_h=no
)
AC_MSG_RESULT($have_sys_tree_h)
if test "$have_sys_tree_h" = "yes"; then
    AC_DEFINE([HAVE_SYS_TREE_H],[1],[Define to 1 if the system has useable sys/tree.h])
fi


AC_MSG_CHECKING(for pthread_barrier)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <pthread.h>
    ],
    [
        pthread_barrier_t barrier;
        pthread_barrier_init(&barrier, NULL, 1);
        pthread_barrier_wait(&barrier);
        pthread_barrier_destroy(&barrier);
        return 0;
    ])
],
[
    AC_DEFINE([HAVE_PTHREAD_BARRIER],[1],[Define to 1 if the system has pthread_barrier])
    have_pthread_barrier=yes
],
    have_pthread_barrier=no
)
AC_MSG_RESULT($have_pthread_barrier)
AM_CONDITIONAL(HAVE_PTHREAD_BARRIER, [test "$have_pthread_barrier" = "yes"])

AC_ARG_ENABLE([unsafe_fchdir],
    AS_HELP_STRING([--enable-unsafe-fchdir], [allow unsafe use of fchdir to track of watched directory path changes on its renaming]),
[
    AC_DEFINE([ENABLE_UNSAFE_FCHDIR],[1],[Allow to use of fchdir to track of watched directory path changes])
    enable_unsafe_fchdir=yes
],
    enable_unsafe_fchdir=no
)

atfuncs_support=yes
AC_CHECK_FUNCS(openat fdopendir fstatat,,atfuncs_support=no)
if test "$atfuncs_support" = "yes"; then
    AC_DEFINE([HAVE_ATFUNCS],[1],[Define to 1 if relative pathname functions detected])
fi
AM_CONDITIONAL(HAVE_ATFUNCS, [test "$atfuncs_support" = "yes"])
AM_CONDITIONAL(HAVE_OPENAT, [test "$ac_cv_func_openat" = "yes"])
AM_CONDITIONAL(HAVE_FDOPENDIR, [test "$ac_cv_func_fdopendir" = "yes"])
AM_CONDITIONAL(HAVE_FSTATAT, [test "$ac_cv_func_fstatat" = "yes"])

AC_MSG_CHECKING(for F_GETPATH in fcntl.h)
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([@%:@include <fcntl.h>], [int a = F_GETPATH;])],
    f_getpath_support=yes,
    f_getpath_support=no
)
AC_MSG_RESULT($f_getpath_support)

if test "$atfuncs_support" = "no" -a \
        "$f_getpath_support" = "no" -a \
        "$enable_unsafe_fchdir" = "no"; then
    AC_MSG_WARN([Neither POSIX.1-2008 relative pathname functions nor F_GETPATH fcntl have been found. Watched directory renamings are untracked. You can specify --enable-unsafe-fchdir to avoid this])
fi


AC_MSG_CHECKING(for O_SYMLINK in fcntl.h)
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([@%:@include <fcntl.h>], [int a = O_SYMLINK;])],
    have_o_symlink=yes,
    have_o_symlink=no
)
AC_MSG_RESULT($have_o_symlink)

if test "$have_o_symlink" = "no"; then
    AC_MSG_WARN([O_SYMLINK open(2) flag is not supported on your system. Watching for symbolic links is unavailable])
fi


AC_MSG_CHECKING(for O_EVTONLY in fcntl.h)
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([@%:@include <fcntl.h>], [int a = O_EVTONLY;])],
    have_o_evtonly=yes,
    have_o_evtonly=no
)
AC_MSG_RESULT($have_o_evtonly)

if test "$have_o_evtonly" = "yes"; then
    AC_DEFINE([HAVE_O_EVTONLY],[1],[Define to 1 if O_EVTONLY defined in fcntl.h])
else
    AC_MSG_WARN([O_EVTONLY open(2) flag is not supported on your system. Unmounting of fs where watch located is unavailable])
fi


AC_MSG_CHECKING(for NOTE_OPEN in sys/event.h)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/types.h>
        @%:@include <sys/event.h>
    ],
    [
        int a = NOTE_OPEN;
    ])
],
    have_note_open=yes,
    have_note_open=no
)
AC_MSG_RESULT($have_note_open)


AC_MSG_CHECKING(for NOTE_CLOSE in sys/event.h)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/types.h>
        @%:@include <sys/event.h>
    ],
    [
        int a = NOTE_CLOSE;
    ])
],
    have_note_close=yes,
    have_note_close=no
)
AC_MSG_RESULT($have_note_close)


AC_MSG_CHECKING(for NOTE_READ in sys/event.h)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/types.h>
        @%:@include <sys/event.h>
    ],
    [
        int a = NOTE_READ;
    ])
],
    have_note_read=yes,
    have_note_read=no
)
AC_MSG_RESULT($have_note_read)


AC_CHECK_MEMBER([struct dirent.d_type],
[
    AC_DEFINE([DIRENT_HAVE_D_TYPE],[1],[Define to 1 if dirent have d_type field])
],
[],
[
    [@%:@include <dirent.h>]
])


AC_OUTPUT
