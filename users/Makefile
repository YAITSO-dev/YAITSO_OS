CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: useradd userdel passwd su sudo

useradd: useradd.c
	$(CC) $(CFLAGS) -o $@ $<
userdel: userdel.c
	$(CC) $(CFLAGS) -o $@ $<
passwd: passwd.c
	$(CC) $(CFLAGS) -o $@ $< -lcrypt
su: su.c
	$(CC) $(CFLAGS) -o $@ $< -lcrypt
sudo: sudo.c
	$(CC) $(CFLAGS) -o $@ $< -lcrypt

clean:
	rm -f useradd userdel passwd su sudo

.PHONY: all clean
