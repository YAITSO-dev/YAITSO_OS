CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE \
           $(shell pkg-config --cflags openssl 2>/dev/null)
LIBS     = $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)

SRC = yat.c ed25519.c http.c yat_meta.c
OBJ = $(SRC:.c=.o)
BIN = yat yat-keygen yat-sign

all: $(BIN)

yat: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

yat-keygen: yat-keygen.c ed25519.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

yat-sign: yat-sign.c ed25519.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c yat.h ed25519.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) *.o

.PHONY: all clean
