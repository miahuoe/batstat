.POSIX:
CC = cc
CFLAGS = --std=c99 -Wall -Wextra -pedantic
LDFLAGS = -l sqlite3

.PHONY: all clean

all: batstatd

batstatd: batstatd.o
	$(CC) $(LDFLAGS) -o batstatd batstatd.o

batstatd.o: batstatd.c

clean:
	rm -f *.o batstatd
