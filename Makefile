APP=test
CC=gcc
CFLAGS=-O2 -Wall -ggdb
LDFLAGS=-lpthread
OBJS=worker-sets.o \
     worker-thread.o \
     worker.o \
     controller.o \
     test.o \
     watch.o \
     dep-list.o \
     conversions.o \
     utils.o

test: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(APP)

clean:
	rm -f *~ *.o
