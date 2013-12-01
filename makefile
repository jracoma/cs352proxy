CC = gcc
CFLAGS = -g -Wall -m32 -pthread

all: cs352proxy.o helpers.o

cs352proxy.o: cs352proxy.h
	$(CC) $(CFLAGS) cs352proxy.c -o cs352proxy

helpers.o: cs352proxy.h helpers.h
	$(CC) $(CFLAGS) helpers.c

testtap:
	$(CC) $(CFLAGS) backup.c -o backup

clean:
	rm -rf *.o cs352proxy