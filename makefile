CC = gcc
CFLAGS = -g -Wall -m32 -pthread

all: cs352proxy

cs352proxy: cs352proxy.h
	$(CC) $(CFLAGS) cs352proxy.c -o cs352proxy

testtap:
	$(CC) $(CFLAGS) backup.c -o backup

clean:
	rm -rf *.o cs352proxy