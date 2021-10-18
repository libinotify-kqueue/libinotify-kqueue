AC_INIT([libinotify],[20180201],[https://github.com/libinotify-kqueue/libinotify-kqueue/],[libinotify])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile libinotify.pc])
AC_GNU_SOURCE
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_CXX
AC_C_INLINE
AC_C_TYPEOF
AC_C_FLEXIBLE_ARRAY_MEMBER
AC_HEADER_STDBOOL


if [test "$ac_cv_prog_cc_c89" = "no"]; then
    AC_MSG_ERROR(C89 compiler support required)
fi


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


AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug], [enable debug build]),
    [
        AC_DEFINE([DEBUG], [1], [debug build])
        DEBUG_CFLAGS="-DDEBUG"
    ],
    [
        DEBUG_CFLAGS="-DNDEBUG"
    ]
)
AC_SUBST([DEBUG_CFLAGS])


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
    AC_DEFINE([BUILD_LIBRARY],[1],[Define to 1 if libinotify is built])
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
AM_CONDITIONAL(HAVE_SYS_TREE_H, [test "$have_sys_tree_h" = "yes"])
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


AC_MSG_CHECKING(for useable stdatomic.h)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <stdatomic.h>
    ],
    [
        atomic_uint t;
        atomic_init(&t, 0);
        atomic_fetch_add(&t, 1);
        atomic_fetch_sub(&t, 1);
        atomic_load(&t);
        return 0;
    ])
],
[
    AC_DEFINE([HAVE_STDATOMIC_H],[1],[Define to 1 if the system has useable stdatomic.h])
    have_stdatomic_h=yes
],
    have_stdatomic_h=no
)
AC_MSG_RESULT($have_stdatomic_h)

if test "$have_stdatomic_h" = "no"; then
    AC_MSG_CHECKING(whether compiler supports atomic operations)
    AC_RUN_IFELSE(
    [
        AC_LANG_PROGRAM(
        [
            @%:@include "compat/stdatomic.h"
        ],
        [
            atomic_uint t;
            atomic_init(&t, 0);
            atomic_fetch_add(&t, 1);
            atomic_fetch_sub(&t, 1);
            return atomic_load(&t);
        ])
    ],
    [
        AC_DEFINE([HAVE_COMPAT_STDATOMIC_H],[1],[Define to 1 if the compiler supports atomic operations with compat/stdatomic.h])
        have_compat_stdatomic_h=yes
    ],
        have_compat_stdatomic_h=no
    )
    AC_MSG_RESULT($have_compat_stdatomic_h)
fi
AM_CONDITIONAL(NO_STDATOMIC, [test "$have_stdatomic_h" = "no" ])
AM_CONDITIONAL(HAVE_COMPAT_STDATOMIC_H, [test "$have_compat_stdatomic_h" = "yes"])


AC_ARG_ENABLE([unsafe_fchdir],
    AS_HELP_STRING([--enable-unsafe-fchdir], [allow unsafe use of fchdir to track of watched directory path changes on its renaming]),
    AS_IF([test "x$enable_unsafe_fchdir" = "xyes"], [
        AC_DEFINE([ENABLE_UNSAFE_FCHDIR],[1],[Allow to use of fchdir to track of watched directory path changes])
    ]),
    enable_unsafe_fchdir=no
)

atfuncs_support=yes
AC_CHECK_FUNCS(openat fdopendir fstatat,,atfuncs_support=no)
AC_CHECK_FUNCS(fdclosedir faccessat)
if test "$atfuncs_support" = "yes"; then
    AC_DEFINE([HAVE_ATFUNCS],[1],[Define to 1 if relative pathname functions detected])
fi
if test "$ac_cv_func_fdopendir" = "no" -o "$ac_cv_func_fdclosedir" = "no" ; then
    AC_CHECK_MEMBER([DIR.dd_fd],
    [
        AC_DEFINE([DIR_HAVE_DD_FD],[1],[Define to 1 if DIR have dd_fd field])
    ],
    [],
    [
        [@%:@include <dirent.h>]
    ])
fi
AM_CONDITIONAL(HAVE_ATFUNCS, [test "$atfuncs_support" = "yes"])
AM_CONDITIONAL(HAVE_OPENAT, [test "$ac_cv_func_openat" = "yes"])
AM_CONDITIONAL(HAVE_FDOPENDIR, [test "$ac_cv_func_fdopendir" = "yes"])
AM_CONDITIONAL(HAVE_FDCLOSEDIR, [test "$ac_cv_func_fdclosedir" = "yes"])
AM_CONDITIONAL(HAVE_FSTATAT, [test "$ac_cv_func_fstatat" = "yes"])
AM_CONDITIONAL(HAVE_FACCESSAT, [test "$ac_cv_func_faccessat" = "yes"])

f_getpath_support=no
if test "$atfuncs_support" = "no"; then
    AC_MSG_CHECKING(for F_GETPATH in fcntl.h)
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM([@%:@include <fcntl.h>], [int a = F_GETPATH;])],
        f_getpath_support=yes
    )
    AC_MSG_RESULT($f_getpath_support)
fi

atfuncs_are_broken=no
if test "$atfuncs_support" = "no" -a \
        "$f_getpath_support" = "no" -a \
        "$enable_unsafe_fchdir" = "no"; then
    AC_MSG_WARN([Neither POSIX.1-2008 relative pathname functions nor F_GETPATH fcntl have been found. Watched directory renamings are untracked. You can specify --enable-unsafe-fchdir to avoid this])
    atfuncs_are_broken=yes
fi

AC_STRUCT_DIRENT_D_TYPE


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


AC_MSG_CHECKING(for NOTE_EXTEND on moving of a file into the watched subdirectory)
rm -rf iktestdir
mkdir iktestdir
touch iktestfile
AC_RUN_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/types.h>
        @%:@include <sys/event.h>
        @%:@include <sys/time.h>
        @%:@include <stdio.h>
        @%:@include <fcntl.h>
    ],
    [
        int fd, kq;
        struct kevent ev;
        static const struct timespec tout = { 1, 0 };

        if ((fd = open("iktestdir", O_RDONLY)) == -1) return 1;
        if ((kq = kqueue()) == -1) return 1;
        EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_ONESHOT,
            NOTE_WRITE | NOTE_EXTEND, 0, 0);
        if (kevent(kq, &ev, 1, NULL, 0, &tout) == -1) return 1;
        if (rename ("iktestfile", "iktestdir/iktestfile") == -1) return 1;
        if (kevent(kq, NULL, 0, &ev, 1, &tout) != 1) return 1;
        return (ev.fflags & NOTE_EXTEND) ? 0 : 1;
    ])
],
[
    AC_DEFINE([HAVE_NOTE_EXTEND_ON_MOVE_TO],[1],[Define to 1 if move_to sets NODE_EXTEND flag])
    AC_MSG_RESULT(yes)
],
    AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for NOTE_EXTEND on moving of a file from the watched subdirectory)
AC_RUN_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <sys/types.h>
        @%:@include <sys/event.h>
        @%:@include <sys/time.h>
        @%:@include <stdio.h>
        @%:@include <fcntl.h>
    ],
    [
        int fd, kq;
        struct kevent ev;
        static const struct timespec tout = { 1, 0 };

        if ((fd = open("iktestdir", O_RDONLY)) == -1) return 1;
        if ((kq = kqueue()) == -1) return 1;
        EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_ONESHOT,
            NOTE_WRITE | NOTE_EXTEND, 0, 0);
        if (kevent(kq, &ev, 1, NULL, 0, &tout) == -1) return 1;
        if (rename ("iktestdir/iktestfile", "iktestfile") == -1) return 1;
        if (kevent(kq, NULL, 0, &ev, 1, &tout) != 1) return 1;
        return (ev.fflags & NOTE_EXTEND) ? 0 : 1;
    ])
],
[
    AC_DEFINE([HAVE_NOTE_EXTEND_ON_MOVE_FROM],[1],[Define to 1 if move_from sets NODE_EXTEND flag])
    AC_MSG_RESULT(yes)
],
    AC_MSG_RESULT(no)
)


dnl Need a run-time check to build under linuxolator
AC_MSG_CHECKING(for O_PATH in fcntl.h)
AC_RUN_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <fcntl.h>
        @%:@include <unistd.h>
    ],
    [
        int fd;
        if ((fd = open("iktestdir", O_PATH)) == -1) return 1;
        return lseek(fd, 0, SEEK_SET) != -1;
    ])
],
[
    AC_DEFINE([HAVE_O_PATH],[1],[Define to 1 if open supports O_PATH flag])
    have_o_path=yes
],
    have_o_path=no
)
AC_MSG_RESULT($have_o_path)


dnl Need a run-time check to build under linuxolator
if test "$ac_cv_func_openat" = "yes"; then
    AC_MSG_CHECKING(for O_EMPTY_PATH in fcntl.h)
    AC_RUN_IFELSE(
    [
        AC_LANG_PROGRAM([@%:@include <fcntl.h>],[return openat(open("iktestdir", O_RDONLY), "", O_RDONLY | O_EMPTY_PATH) < 0;])
    ],
    [
        AC_DEFINE([HAVE_O_EMPTY_PATH],[1],[Define to 1 if openat supports O_EMPTY_PATH flag])
        AC_MSG_RESULT(yes)
    ],
        AC_MSG_RESULT(no)
    )
fi


dnl Need a run-time check to build under linuxolator
if test "$ac_cv_func_faccessat" = "yes"; then
    AC_MSG_CHECKING(for AT_EMPTY_PATH in fcntl.h)
    AC_RUN_IFELSE(
    [
        AC_LANG_PROGRAM(
        [
            @%:@include <fcntl.h>
            @%:@include <unistd.h>
        ],
        [
           return faccessat(open("iktestdir", O_RDONLY), "", R_OK, AT_EACCESS | AT_EMPTY_PATH) < 0;
        ])
    ],
    [
        AC_DEFINE([HAVE_AT_EMPTY_PATH],[1],[Define to 1 if faccessat supports AT_EMPTY_PATH flag])
        AC_MSG_RESULT(yes)
    ],
        AC_MSG_RESULT(no)
    )
fi
rm -rf iktestdir
rm iktestfile


AC_MSG_CHECKING(for O_SYMLINK in fcntl.h)
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([@%:@include <fcntl.h>], [int a = O_SYMLINK;])],
    have_o_symlink=yes,
    have_o_symlink=no
)
AC_MSG_RESULT($have_o_symlink)


AC_MSG_CHECKING(for O_EVTONLY in fcntl.h)
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([@%:@include <fcntl.h>], [int a = O_EVTONLY;])],
    have_o_evtonly=yes,
    have_o_evtonly=no
)
AC_MSG_RESULT($have_o_evtonly)


# There are three ways we can reread directory opened already. Namely:
# 1. Copy fd with fopenat() directory by realtive path "." to file descriptor
#    than issue fdopendir, readddir and closedir
# 2. Copy fd with dup() than do rewinddir, fdopendir, readddir and closedir
#    dup() is not necessary if fdclosedir() is supported
# 3. Rewind directory than read it one more time and leave opened
# Former one is more reliable but latter is prefered when system has NOTE_OPEN
# or NOTE_CLOSE events and we dont want that kqueue catches open and closedirs
# issued by directory diffing routines.

AC_ARG_ENABLE([opendir],
    AS_HELP_STRING([--enable-opendir=no|dup|open], [Do opendir on each directory read @<:@default=dup@:>@]),
    ,
    AS_IF([test "$have_o_path" = "yes"],
          [enable_opendir=open],
          AS_IF([test "$ac_cv_func_fdopendir" = "yes" -a "$ac_cv_func_fdclosedir" = "yes"],
                [enable_opendir=no],
                AS_IF([test "$ac_cv_func_fdopendir" = "no"],
                      AS_IF([test "$atfuncs_are_broken" = "yes"],
                            [enable_opendir=no],
                            [enable_opendir=open]
                      ),
                      [enable_opendir=dup]
                )
          )
    )
)
case $enable_opendir in
no)
    readdir_does_opendir=0
    ;;
dup)
    readdir_does_opendir=1
    ;;
open)
    readdir_does_opendir=2
    ;;
*)
    AC_MSG_ERROR([Wrong --enable-opendir value: '$enable_opendir'])
    ;;
esac
AC_DEFINE_UNQUOTED([READDIR_DOES_OPENDIR],
    [$readdir_does_opendir],
    [Define to 1/2 if opendir is necessary for each directory read]
)

if test "$have_o_path" = "yes" -a "$readdir_does_opendir" != "2"; then
    AC_MSG_WARN([Setting opendir to other value than open effectivelly disables O_PATH support])
    have_o_path=no
fi

if test "$have_o_symlink" = "no" -a "$have_o_path" = "no"; then
    AC_MSG_WARN([Neither O_PATH nor O_SYMLINK open(2) flags are supported on your system. Watching for symbolic links is unavailable])
fi

if test "$have_o_evtonly" = "no" -a "$have_o_path" = "no"; then
    AC_MSG_WARN([Neither O_PATH nor O_EVTONLY open(2) flags are supported on your system. Unmounting of fs where watch located is unavailable])
fi


AC_ARG_ENABLE([skip-subfiles],
    AS_HELP_STRING([--enable-skip-subfiles=fstype@<:@,fstype@:>@], [List of filesystem types where opening of subfiles is not performed]),
    [
        skip_subfiles=yes
        subfiles_fs_list=`echo "\"$enable_skip_subfiles\"" | $SED -e 's/,/", "/g'`
        AC_DEFINE_UNQUOTED([SKIP_SUBFILES],[$subfiles_fs_list],[List of filesystem types where opening of subfiles is not performed])
    ],
    [skip_subfiles=no]
)


AS_IF([test "$skip_subfiles" = "yes" -o "$ac_cv_func_fdopendir" = "no"],
[
    AC_CHECK_FUNC(statfs,
    [
        AC_CHECK_MEMBERS([struct statfs.f_fstypename, struct statfs.f_mntonname],
        [
            AC_DEFINE([HAVE_STATFS], [1], [Define to 1 if you have the `statfs' function.])
        ],
        [
            AC_CHECK_FUNC(statvfs,
            [
                AC_CHECK_MEMBERS([struct statvfs.f_fstypename, struct statvfs.f_mntonname],
                    AC_DEFINE([HAVE_STATVFS], [1], [Define to 1 if you have the `statvfs' function.]),
                    AC_MSG_ERROR(No means for detecting filesystem type found. Remove --enable-skip-subfiles option!),
                    [@%:@include <sys/statvfs.h>]
                )
            ])
        ],
        [
            @%:@include <sys/param.h>
            @%:@include <sys/mount.h>
        ])
    ])
])


AC_OUTPUT
