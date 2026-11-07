CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE -I../ytui
LIBS     = -L../ytui -lytui

all: yaitso-control

yaitso-control: yaitso-control.c ../ytui/libytui.a
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

../ytui/libytui.a:
	$(MAKE) -C ../ytui

clean:
	rm -f yaitso-control

.PHONY: all clean
