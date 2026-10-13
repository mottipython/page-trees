# The Page Trees - a screensaver of the page trees from Neal Stephenson's "Anathem"
VERSION  := 1.1.0
PREFIX   ?= /usr
SYSCONFDIR ?= /etc
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/page-trees

# the pictures in assets/art are built into the program, so it must be compiled from here
build/page-trees: src/trees.c src/stb_image.h $(wildcard assets/art/*)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/trees.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/page-trees
	SDL_VIDEODRIVER=dummy ./build/page-trees --snapshot build/check.bmp --time 200 --size 640x400 && echo "ran a full round"

install: all
	install -Dm755 build/page-trees $(DESTDIR)$(PREFIX)/bin/page-trees
	install -Dm755 packaging/linux/page-trees-screensaver $(DESTDIR)$(PREFIX)/bin/page-trees-screensaver
	install -Dm644 packaging/linux/page-trees.desktop $(DESTDIR)$(PREFIX)/share/applications/page-trees.desktop
	install -Dm644 packaging/linux/page-trees-saver.desktop $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/page-trees.desktop
	install -Dm644 packaging/linux/page-trees-autostart.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/page-trees-screensaver.desktop
	install -Dm644 packaging/page-trees.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/page-trees.png
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/page-trees/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/page-trees/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/page-trees $(DESTDIR)$(PREFIX)/bin/page-trees-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/page-trees.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/page-trees-screensaver.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/page-trees.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/page-trees.png
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/page-trees

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/page-trees
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	echo /etc/xdg/autostart/page-trees-screensaver.desktop > build/deb/DEBIAN/conffiles
	dpkg-deb --root-owner-group --build build/deb dist/page-trees_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=page-trees-$(VERSION)/ -o build/rpm/SOURCES/page-trees-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/page-trees.spec
	cp build/rpm/RPMS/*/page-trees-$(VERSION)-*.rpm dist/

# Windows: the screensaver itself, and a small PageTrees.scr that goes in the
# Windows system folder (so it is listed with the other screensavers) and starts it
windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Isrc -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/page-trees.exe src/trees.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-windres packaging/windows/stub.rc -O coff -o build/win/stub.res
	$(MINGW)-gcc -O2 -Wall -o build/win/PageTrees.scr packaging/windows/stub.c build/win/stub.res -mwindows -static
	$(MINGW)-strip build/win/page-trees.exe build/win/PageTrees.scr
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/page-trees.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean
