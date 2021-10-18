############################################################
#	The library
#-----------------------------------------------------------

ACLOCAL_AMFLAGS = -I m4

if BUILD_LIBRARY
lib_LTLIBRARIES = libinotify.la

nobase_include_HEADERS = sys/inotify.h

libinotify_la_SOURCES = \
    compat.h \
    utils.c \
    utils.h \
    dep-list.c \
    dep-list.h \
    event-queue.c \
    event-queue.h \
    inotify-watch.c \
    inotify-watch.h \
    watch-set.c \
    watch-set.h \
    watch.c \
    watch.h \
    worker-thread.c \
    worker-thread.h \
    worker.c \
    worker.h \
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

if !HAVE_FDCLOSEDIR
libinotify_la_SOURCES += compat/fdclosedir.c
endif

if !HAVE_FSTATAT
libinotify_la_SOURCES += compat/fstatat.c
endif

if NO_STDATOMIC
if HAVE_COMPAT_STDATOMIC_H
libinotify_la_SOURCES += compat/stdatomic.h
else
libinotify_la_SOURCES += compat/ik_atomic.c compat/ik_atomic.h
endif
endif

libinotify_la_SOURCES += compat/tree.h

libinotify_la_CFLAGS = -I. @DEBUG_CFLAGS@ @PTHREAD_CFLAGS@ -Wall -Werror
libinotify_la_LDFLAGS = @PTHREAD_LIBS@ -export-symbols libinotify.sym
endif

inotify_test_SOURCES = inotify-test.c
inotify_test_LDADD = libinotify.la
kqueue_test_SOURCES = kqueue-test.c
noinst_PROGRAMS = inotify-test kqueue-test

pkgconfigdir = $(libdir)/pkgconfig
nodist_pkgconfig_DATA = libinotify.pc

EXTRA_DIST = \
    libinotify.sym

dist_man_MANS = libinotify.3
MAN_LINKS = \
	inotify_init.3 \
	inotify_init1.3 \
	inotify_add_watch.3 \
	inotify_rm_watch.3 \
	inotify_set_param.3 \
	inotify_event.3

install-data-hook: $(MAN_LINKS)

$(MAN_LINKS):
	(cd $(DESTDIR)$(man3dir) && rm -f $@ && $(LN_S) libinotify.3 $@)


############################################################
#	Test suite
#-----------------------------------------------------------

EXTRA_PROGRAMS = check_libinotify

test: check_libinotify
	@echo Running test suite...
	@./check_libinotify

.PHONY: test

check_libinotify_SOURCES = \
    tests/core/core.hh \
    tests/core/platform.hh \
    tests/core/log.cc \
    tests/core/log.hh \
    tests/core/event.cc \
    tests/core/event.hh \
    tests/core/action.cc \
    tests/core/action.hh \
    tests/core/request.cc \
    tests/core/request.hh \
    tests/core/response.cc \
    tests/core/response.hh \
    tests/core/inotify_client.cc \
    tests/core/inotify_client.hh \
    tests/core/consumer.cc \
    tests/core/consumer.hh \
    tests/core/journal.cc \
    tests/core/journal.hh \
    tests/core/test.cc \
    tests/core/test.hh \
    tests/start_stop_test.cc \
    tests/start_stop_test.hh \
    tests/start_stop_dir_test.cc \
    tests/start_stop_dir_test.hh \
    tests/fail_test.cc \
    tests/fail_test.hh \
    tests/notifications_test.cc \
    tests/notifications_test.hh \
    tests/notifications_dir_test.cc \
    tests/notifications_dir_test.hh \
    tests/update_flags_test.cc \
    tests/update_flags_test.hh \
    tests/update_flags_dir_test.cc \
    tests/update_flags_dir_test.hh \
    tests/open_close_test.cc \
    tests/open_close_test.hh \
    tests/symlink_test.cc \
    tests/symlink_test.hh \
    tests/bugs_test.cc \
    tests/bugs_test.hh \
    tests/event_queue_test.cc \
    tests/event_queue_test.hh \
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
