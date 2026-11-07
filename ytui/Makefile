CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

SRC = ytui.c
OBJ = $(SRC:.c=.o)

all: libytui.a

libytui.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

%.o: %.c libytui.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) libytui.a

.PHONY: all clean
