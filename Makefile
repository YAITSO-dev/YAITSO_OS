CC      = gcc
CFLAGS  = -Wall -Wextra   -std=c11 -O2 -Iinclude \
          -D_POSIX_C_SOURCE=200809L \
          -DYAITSO_VERSION='"0.3.4"'
TARGET  = yaitso
SRC     = src/main.c src/commands.c src/builtin.c src/util.c \
          src/history.c src/utf8.c src/config.c src/shell.c \
          src/readline.c src/exec.c src/jobs.c src/glob.c src/ext.c \
          src/suggest.c src/posargs.c src/expand.c src/env.c
OBJ     = $(SRC:.c=.o)

ISO     = yaitso-0.3.4.iso

.NOTPARALLEL:

all: tools ytui_init $(TARGET) yat yhttpd init svc getty users installer ybox net yctl

gui: libywt ycomp yterm ybar ylaunch start-desktop
	$(MAKE) -C libywt
	$(MAKE) -C ycomp
	$(MAKE) -C yterm
	$(MAKE) -C ybar
	$(MAKE) -C ylaunch
	$(MAKE) -C start-desktop

tools:
	$(MAKE) -C tools

ytui_init: tools
	$(MAKE) -C ytui

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

# Makefile в зависимостях: смена -DYAITSO_VERSION должна пересобирать все
# объекты, иначе часть из них остаётся со старой версией.
%.o: %.c include/yaitso.h Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

yat:
	$(MAKE) -C yat
yhttpd:
	$(MAKE) -C yhttpd
init:
	$(MAKE) -C init
svc:
	$(MAKE) -C svc
start-desktop:
	$(MAKE) -C start-desktop
getty:
	$(MAKE) -C login
users:
	$(MAKE) -C users
installer:
	$(MAKE) -C installer
ybox:
	$(MAKE) -C ybox
net:
	$(MAKE) -C net
yctl: ytui_init
	$(MAKE) -C yaitso-control

libywt:
	$(MAKE) -C libywt
ycomp: libywt
	$(MAKE) -C ycomp
yterm: libywt
	$(MAKE) -C yterm
ybar: libywt
	$(MAKE) -C ybar
ylaunch: libywt
	$(MAKE) -C ylaunch

glibc: tools
	GLIBC_PREBUILT=0 sh build/build-glibc.sh

glibc-libs: glibc
	@for p in packages/glibc-*/; do sh build/build-pkg.sh "$$p"; done

glibc-all: glibc-libs
	sh build/build-pkg.sh packages/patchelf/
	sh build/build-pkg.sh packages/firefox/

repo: all
	@for p in packages/*/; do \
		case "$$p" in \
			packages/glibc/|packages/glibc-*/|packages/firefox/|packages/patchelf/) continue ;; \
		esac; \
		sh build/build-pkg.sh "$$p"; \
	done
	sh build/build-repo.sh

repo-gui: all gui
	@for p in packages/*/; do \
		case "$$p" in \
			packages/glibc/|packages/glibc-*/|packages/firefox/|packages/patchelf/) continue ;; \
		esac; \
		sh build/build-pkg.sh "$$p"; \
	done
	sh build/build-repo.sh

repo-full: all gui glibc-all
	@for p in packages/*/; do sh build/build-pkg.sh "$$p"; done
	sh build/build-repo.sh

repo-sign: repo
	sh build/repo-sign.sh

repo-serve: repo-sign
	./yhttpd/yhttpd --root repo --port 8080

repo-sync: repo-sign
	@if [ -z "$$HOST" ]; then echo "usage: make repo-sync HOST=user@host"; exit 1; fi
	HOST="$$HOST" DEST="$${DEST:-/var/www/yaitso-repo/}" DRY="$${DRY:-0}" sh build/repo-sync.sh

# Сборка пакетов и бинарников должна идти от обычного пользователя:
# у root другой PATH и, как правило, старый cargo без edition2024.
# root нужен только для bootstrap/mkiso, поэтому sudo стоит внутри рецепта.
not-root:
	@if [ "$$(id -u)" = "0" ]; then \
		echo "iso: запускайте без sudo — make iso-gui" >&2; \
		exit 1; \
	fi

iso: not-root all repo
	sh build/repo-sign.sh
	sudo bash build/bootstrap.sh
	sudo bash build/mkiso.sh
	sudo chown $$(id -u):$$(id -g) $(ISO)

iso-gui: not-root all gui repo-gui
	sh build/repo-sign.sh
	sudo bash build/bootstrap.sh
	sudo bash build/mkiso.sh
	sudo chown $$(id -u):$$(id -g) $(ISO)

iso-full: not-root all gui repo-full
	sh build/repo-sign.sh
	sudo bash build/bootstrap.sh
	sudo bash build/mkiso.sh
	sudo chown $$(id -u):$$(id -g) $(ISO)

qemu: $(ISO)
	sh build/run-qemu.sh bios

qemu-uefi: $(ISO)
	sh build/run-qemu.sh uefi

qemu-gui: $(ISO)
	sh build/run-qemu.sh gui

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@sh tests/smoke.sh
	@sh tests/shell.sh

test-yat:
	$(MAKE) -C yat test

test-glibc: tools
	@sh tests/glibc.sh

check: all test test-yat
	@echo "check: OK"

debug: CFLAGS += -g -O0 -fsanitize=address,undefined
debug: clean
debug: $(TARGET)

format:
	-clang-format -i include/*.h src/*.c yhttpd/*.c ybox/*.c \
		init/*.c login/*.c users/*.c installer/*.c net/*.c svc/*.c \
		ytui/*.c yaitso-control/*.c lib/*.c tools/*.c \
		libywt/*.c ycomp/*.c yterm/*.c ybar/*.c ylaunch/*.c
	-$(MAKE) -C yat fmt

analyze:
	-cppcheck --enable=warning,performance --error-exitcode=1 --inline-suppr \
		-Iinclude -Ilibywt src/ ybox/ yhttpd/ init/ svc/ login/ \
		users/ installer/ net/ ytui/ yaitso-control/ lib/ tools/ \
		libywt/ ycomp/ yterm/ ybar/ ylaunch/

clean:
	rm -f $(TARGET) $(OBJ)
	-$(MAKE) -C tools           clean
	-$(MAKE) -C yat             clean
	-$(MAKE) -C yhttpd          clean
	-$(MAKE) -C init            clean
	-$(MAKE) -C svc             clean
	-$(MAKE) -C login           clean
	-$(MAKE) -C users           clean
	-$(MAKE) -C installer       clean
	-$(MAKE) -C ybox            clean
	-$(MAKE) -C net             clean
	-$(MAKE) -C ytui            clean
	-$(MAKE) -C yaitso-control  clean
	-$(MAKE) -C libywt          clean
	-$(MAKE) -C ycomp           clean
	-$(MAKE) -C yterm           clean
	-$(MAKE) -C ybar            clean
	-$(MAKE) -C ylaunch         clean

clean-repo:
	rm -f repo/*.yat repo/index.txt repo/index.sig

clean-glibc:
	rm -rf build/glibc-build
	rm -f repo/glibc-*.yat
	rm -f repo/firefox-*.yat repo/patchelf-*.yat

clean-iso:
	sudo rm -rf build/rootfs build/iso build/disk.img build/OVMF_VARS.fd build/core.img $(ISO)

clean-all: clean clean-repo clean-glibc clean-iso

.PHONY: all gui tools yat yhttpd init svc getty users installer ybox net ytui yctl \
        libywt ycomp yterm ybar ylaunch glibc glibc-libs glibc-all repo repo-gui \
        repo-full repo-sign repo-serve repo-sync iso iso-gui iso-full not-root qemu qemu-uefi \
        qemu-gui run test test-yat test-glibc check debug format analyze clean \
        clean-repo clean-glibc clean-iso clean-all
