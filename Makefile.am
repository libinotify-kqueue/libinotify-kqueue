############################################################
#	The library
#-----------------------------------------------------------

ACLOCAL_AMFLAGS = -I m4

if BUILD_LIBRARY
lib_LTLIBRARIES = libinotify.la

nobase_include_HEADERS = sys/inotify.h

libinotify_la_SOURCES = \
    utils.c \
    dep-list.c \
    event-queue.c \
    inotify-watch.c \
    watch-set.c \
    watch.c \
    worker-thread.c \
    worker.c \
    controller.c

if !HAVE_ATFUNCS
libinotify_la_SOURCES += compat/atfuncs.c
endif

if !HAVE_OPENAT
libinotify_la_SOURCES += compat/openat.c
endif

if !HAVE_FDOPENDIR
libinotify_la_SOURCES += compat/fdopendir.c
endif

if !HAVE_FSTATAT
libinotify_la_SOURCES += compat/fstatat.c
endif

if NO_STDATOMIC
libinotify_la_SOURCES += compat/ik_atomic.c
endif

libinotify_la_CFLAGS = -I. -DNDEBUG @PTHREAD_CFLAGS@
libinotify_la_LDFLAGS = @PTHREAD_LIBS@ -export-symbols libinotify.sym
endif

inotify_test_SOURCES = inotify-test.c
inotify_test_LDADD = libinotify.la
noinst_PROGRAMS = inotify-test

pkgconfigdir = $(libdir)/pkgconfig
nodist_pkgconfig_DATA = libinotify.pc

############################################################
#	Test suite
#-----------------------------------------------------------

EXTRA_PROGRAMS = check_libinotify

test: check_libinotify
	@echo Running test suite...
	@./check_libinotify

.PHONY: test

check_libinotify_SOURCES = \
    tests/core/log.cc \
    tests/core/event.cc \
    tests/core/action.cc \
    tests/core/request.cc \
    tests/core/response.cc \
    tests/core/inotify_client.cc \
    tests/core/consumer.cc \
    tests/core/journal.cc \
    tests/core/test.cc \
    tests/start_stop_test.cc \
    tests/start_stop_dir_test.cc \
    tests/fail_test.cc \
    tests/notifications_test.cc \
    tests/notifications_dir_test.cc \
    tests/update_flags_test.cc \
    tests/update_flags_dir_test.cc \
    tests/open_close_test.cc \
    tests/symlink_test.cc \
    tests/bugs_test.cc \
    tests/event_queue_test.cc \
    tests/tests.cc

check_libinotify_CXXFLAGS = @PTHREAD_CFLAGS@
check_libinotify_LDFLAGS = @PTHREAD_LIBS@

if LINUX
check_libinotify_CXXFLAGS += -std=c++0x
endif

if !HAVE_PTHREAD_BARRIER
check_libinotify_SOURCES += compat/pthread_barrier.c
endif

if BUILD_LIBRARY
check_libinotify_LDADD = libinotify.la
endif

noinst_programs = check_libinotify
