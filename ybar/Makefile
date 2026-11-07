CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE -I../libywt \
           $(shell pkg-config --cflags wayland-client xkbcommon freetype2)
LIBS     = -L../libywt -lywt \
           $(shell pkg-config --libs wayland-client xkbcommon freetype2) -lm

SRC = ybar.c widgets.c ipc_client.c
OBJ = $(SRC:.c=.o)

all: ybar

ybar: $(OBJ) ../libywt/libywt.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c ybar.h
	$(CC) $(CFLAGS) -c -o $@ $<

../libywt/libywt.a:
	$(MAKE) -C ../libywt

clean:
	rm -f $(OBJ) ybar

.PHONY: all clean
