UNAME=$(shell uname)
APP=tests
CXX=g++
CXXFLAGS=-I.. -O2 -Wall -ggdb
LDFLAGS=-lpthread


# Uncomment this line to enable debug logging.
# The tests suite should be rebuilt completely for that.
# CXXFLAGS+=-DENABLE_LOGGING

# Uncomment this line to enable a tricky moves test.
# It fails even on Linux, so I have to debug it. Anyway,
# notifications_dir_test.o should be rebuilt to use it.
# CXXFLAGS+=-DTESTS_MOVES_TRICKY

ifeq ($(UNAME),Linux)
	CXXFLAGS+=-std=c++0x
endif

ifeq ($(UNAME),NetBSD)
	LDFLAGS+=../blob.a
endif

CORE_OBJS=\
	core/log.o \
	core/event.o \
	core/action.o \
	core/request.o \
	core/response.o \
	core/inotify_client.o \
	core/consumer.o \
	core/journal.o \
	core/test.o

TEST_OBJS=\
	start_stop_test.o \
	start_stop_dir_test.o \
	fail_test.o \
	notifications_test.o \
	notifications_dir_test.o \
	update_flags_test.o \
	update_flags_dir_test.o \
	tests.o

tests-app: $(TEST_OBJS) $(CORE_OBJS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(CORE_OBJS) $(LDFLAGS) -o $(APP)

run:
	@./tests

clean:
	rm -f *~ *.o core/*~ core/*.o
