CC=gcc
CFLAGS=-Wall -std=gnu99

.PHONY: all
all: test1 test2

test1: test1.o
	$(CC) $(CFLAGS) -o test1 test1.o

test2: test2.o
	$(CC) $(CFLAGS) -o test2 test2.o

test1.o: test1.c
	$(CC) $(CFLAGS) -c test1.c

test2.o: test2.c
	$(CC) $(CFLAGS) -c test2.c

.PHONY: clean
clean:
	rm -rf test1 test2 *.o
