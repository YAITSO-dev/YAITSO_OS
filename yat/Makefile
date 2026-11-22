CARGO ?= cargo
FLAGS  = --release --offline --frozen
PREFIX ?= /usr

all:
	$(CARGO) build $(FLAGS)

vendor:
	@if [ -d vendor ] && [ -n "$$(ls -A vendor 2>/dev/null)" ]; then \
		echo "vendor/ уже существует"; exit 0; \
	fi; \
	set -e; \
	if [ -f .cargo/config.toml ]; then mv .cargo/config.toml .cargo/config.toml.off; fi; \
	trap 'if [ -f .cargo/config.toml.off ]; then mv .cargo/config.toml.off .cargo/config.toml; fi' EXIT; \
	$(CARGO) vendor vendor/

clean:
	$(CARGO) clean

install: all
	install -Dm755 target/release/yat        $(DESTDIR)$(PREFIX)/bin/yat
	install -Dm755 target/release/yat-keygen $(DESTDIR)$(PREFIX)/bin/yat-keygen
	install -Dm755 target/release/yat-sign   $(DESTDIR)$(PREFIX)/bin/yat-sign

test:
	$(CARGO) test $(FLAGS)

fmt:
	$(CARGO) fmt

.PHONY: all vendor clean install test fmt
