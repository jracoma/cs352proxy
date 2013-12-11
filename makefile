CC = gcc
CFLAGS = -g -Wall -m32 -pthread

all: cs352proxy.o

cs352proxy.o: cs352proxy.h
	$(CC) $(CFLAGS) cs352proxy.c -o cs352proxy

clean:
	rm -rf *.o cs352proxy