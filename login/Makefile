CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: getty

getty: login.o crypt_check.o
	$(CC) $(CFLAGS) -o $@ $^ -lcrypt

login.o: login.c login.h
	$(CC) $(CFLAGS) -c -o $@ $<

crypt_check.o: crypt_check.c login.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f getty *.o

.PHONY: all clean
