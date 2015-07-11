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


AC_MSG_CHECKING(for NODE_EXTEND on renaming of file in subdirectory)
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
    AC_DEFINE([HAVE_NOTE_EXTEND_ON_SUBFILE_RENAME],[1],[Define to 1 if rename set NODE_EXTEND flag])
    AC_MSG_RESULT(yes)
],
    AC_MSG_RESULT(no)
)
rm -rf iktestdir


AC_CHECK_MEMBER([struct dirent.d_type],
[
    AC_DEFINE([DIRENT_HAVE_D_TYPE],[1],[Define to 1 if dirent have d_type field])
],
[],
[
    [@%:@include <dirent.h>]
])


# There are two ways we can reread directory opened already. Namely:
# 1. Open directory by realtive path "." to file descriptor than read & close
# 2. Rewind directory than read it one more time and leave opened
# Former one is more reliable but latter is prefered when system has NOTE_OPEN
# or NOTE_CLOSE events and we dont want that kqueue catches open and closedirs
# issued by directory diffing routines.
#
# Define this if we can reuse filedes opened once before or leave undefined if
# we have to execute open/close pair on every directory read.

if test "$ac_cv_func_fdopendir" = "yes" -a "$have_o_evtonly" = "no" -a \
    \( "$have_note_open" = "yes" -o "$have_note_close" = "yes" \) ; then
    AC_DEFINE([DIRECTORY_LISTING_REWINDS], [1], [Define to 1 if reusing of watched dir filedes is necessary])
fi


CFLAGS="${CFLAGS} ${PTHREAD_CFLAGS}"
LIBS="${LIBS} ${PTHREAD_LIBS}"
AC_MSG_CHECKING(for SIGPIPE recipient)
AC_RUN_IFELSE(
[
    AC_LANG_PROGRAM(
    [[
        #include <pthread.h>
        #include <signal.h>
        #include <unistd.h>

        static volatile int catched = 0;
        void sigpipe_handler(int sig) { catched = 1; }
        void *test(void *args) {
            sigset_t set;
            int io[2];
            sigemptyset(&set);
            sigaddset(&set, SIGPIPE);
            pthread_sigmask(SIG_BLOCK, &set, NULL);
            pipe(io);
            close(io[0]);
            write(io[1], io, 1);
            return NULL;
        }
    ]],
    [[
        pthread_t thread;
        signal(SIGPIPE, sigpipe_handler);
        if (pthread_create(&thread, NULL, test, NULL) == -1) return 1;
        if (pthread_join(thread, NULL) != 0) return 1;
        return catched;
    ]])
],
    AC_MSG_RESULT(thread)
,[
    AC_DEFINE([SIGPIPE_RECIPIENT_IS_PROCESS],[1],[Define to 1 if SIGPIPE is process-wide signal])
    AC_MSG_RESULT(process)
]
)


AC_OUTPUT
