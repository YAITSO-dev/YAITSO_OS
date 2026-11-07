CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

SRC = pbkdf2.c chacha.c
OBJ = $(SRC:.c=.o)

all: libuser.a

libuser.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

%.o: %.c libuser.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) libuser.a

.PHONY: all clean
