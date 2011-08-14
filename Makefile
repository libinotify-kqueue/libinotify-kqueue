APP=test
CC=gcc
AR=ar
CFLAGS=-O2 -Wall -ggdb
LDFLAGS=-lpthread
OBJS=worker-sets.o \
     worker-thread.o \
     worker.o \
     controller.o \
     watch.o \
     dep-list.o \
     conversions.o \
     utils.o

test: blob test.o
	$(CC) test.o blob.a $(LDFLAGS) -o $(APP)

blob: $(OBJS)
	$(AR) -r blob.a $(OBJS)

clean:
	rm -f *~ *.o
