############################################################
#	The library
#-----------------------------------------------------------

ACLOCAL_AMFLAGS = -I m4

if BUILD_LIBRARY
lib_LTLIBRARIES = libinotify.la

nobase_include_HEADERS = sys/inotify.h

libinotify_la_SOURCES = \
    utils.c \
    compat.c \
    conversions.c \
    dep-list.c \
    inotify-watch.c \
    watch-set.c \
    watch.c \
    worker-thread.c \
    worker.c \
    controller.c

libinotify_la_CFLAGS = -I. -DNDEBUG
if FREEBSD
libinotify_la_LDFLAGS = -pthread
else
libinotify_la_LDFLAGS = -lpthread
endif
endif

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
    tests/tests.cc

if LINUX
check_libinotify_CXXFLAGS = -std=c++0x
check_libinotify_SOURCES += compat.c
endif

if BUILD_LIBRARY
check_libinotify_LDADD = libinotify.la
endif

if FREEBSD
check_libinotify_LDFLAGS = -pthread
else
check_libinotify_LDFLAGS = -lpthread
endif


noinst_programs = check_libinotify
