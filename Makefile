# Amplitude build
#
#   make            native Linux build         -> build/linux/amplitude
#   make windows    both Windows cross builds  -> build/win32/amplitude.exe (32-bit)
#                                                 build/win64/amplitude.exe (64-bit)
#   make windows32  only the 32-bit one
#   make windows64  only the 64-bit one
#                   (they fetch the MinGW-w64 compiler first if none is installed)
#   make installer  Windows installers         -> build/amplitude-<ver>-win32-setup.exe
#                                                 build/amplitude-<ver>-win64-setup.exe
#                   (fetches NSIS first if not installed)
#   make all        everything: Linux, the Debian and RPM packages, both
#                   Windows builds and the Windows installers
#   make dist       the files to publish       -> build/dist/ (see below)
#   make deb        Debian package             -> build/amplitude_<ver>_<arch>.deb
#   make rpm        RPM package                -> build/amplitude-<ver>-1.<arch>.rpm
#                   (fetches rpmbuild first if not installed)
#   make test       unit tests, built with sanitizers (plays a few seconds, muted)
#   make test-gui   drives a real player window; needs a running X display
#   make fonts      regenerate src/font_data.c from third_party/fonts
#   make website    regenerate the images in website/img
#   make clean
#
# Third-party decoder sources live in third_party/ (see third_party/fetch.sh).
# Generated files (src/font_data.c, src/icon_data.c, the icons) are kept in
# the tree; tools/genfont.py and tools/genlogo.py recreate them.

VERSION        := 0.2.4
RELEASE_DATE   := 2026-10-04
DEB_MAINTAINER ?= Srđan Rudić <blaster7th@gmail.com>

COMMON_SRC := main.c config.c tags.c presets.c theme.c playlist.c ui.c ui_eq.c ui_playlist.c ui_jump.c ui_about.c ui_url.c ui_dialog.c ui_menu.c gfx.c font_data.c audio.c stream.c cd.c cdnames.c \
              skin.c skin_default.c zip.c \
              codec.c codec_vorbis.c codec_opus.c codec_aac.c codec_mod.c codec_cd.c
HEADERS    := $(wildcard src/*.h)

CFLAGS  ?= -Os
CFLAGS  += -std=gnu99 -Wall -Wextra -ffunction-sections -DAMPLITUDE_VERSION='"$(VERSION)"'
LDFLAGS += -Wl,--gc-sections -s

# --- Third-party decoders (compiled from source, linked statically) ----------
TP := third_party
include $(TP)/opus/celt_sources.mk $(TP)/opus/silk_sources.mk $(TP)/opus/opus_sources.mk

# Decoding only: the encoder front ends and their analysis tables stay out.
OPUS_SRC := $(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FLOAT) \
            $(filter-out %_encoder.c,$(OPUS_SOURCES))
TP_SRC := stb_vorbis.c minimp4.c ogg/src/bitwise.c ogg/src/framing.c \
          $(addprefix opus/,$(OPUS_SRC)) \
          opusfile/src/info.c opusfile/src/internal.c opusfile/src/opusfile.c \
          $(patsubst $(TP)/%,%,$(wildcard $(TP)/libxmp-lite/src/*.c $(TP)/libxmp-lite/src/loaders/*.c)) \
          $(patsubst $(TP)/%,%,$(wildcard $(TP)/faad2/libfaad/*.c))

TP_CFLAGS      := -Os -std=gnu99 -w -ffunction-sections
OGG_FLAGS      := -I$(TP)/ogg/include
OPUS_FLAGS     := -DOPUS_BUILD -DVAR_ARRAYS -DHAVE_LRINTF -DHAVE_LRINT -DPACKAGE_VERSION='"1.5.2"' \
                  -I$(TP)/opus/include -I$(TP)/opus/celt -I$(TP)/opus/silk -I$(TP)/opus/silk/float -I$(TP)/opus
OPUSFILE_FLAGS := -I$(TP)/opusfile/include -I$(TP)/ogg/include -I$(TP)/opus/include
XMP_FLAGS      := -DLIBXMP_CORE_PLAYER -DLIBXMP_STATIC -I$(TP)/libxmp-lite/include/libxmp-lite \
                  -I$(TP)/libxmp-lite/src
FAAD_FLAGS     := -DPACKAGE_VERSION='"2.11.1"' -DHAVE_STDINT_H -DHAVE_STRING_H -DHAVE_MEMCPY -DHAVE_LRINTF \
                  -DSTDC_HEADERS -I$(TP)/faad2/include -I$(TP)/faad2/libfaad
# (stb_vorbis is built whole: files use its "pull" interface, streams its "push" one.)
STB_FLAGS      :=

# What our own sources need to find the decoder headers.
CODEC_INCLUDES := $(OPUSFILE_FLAGS) -DLIBXMP_STATIC -I$(TP)/libxmp-lite/include/libxmp-lite -I$(TP)/faad2/include

# --- Per-platform rules --------------------------------------------------------
# $(1) = build directory, $(2) = compiler, $(3) = extra flags for our sources
define PLATFORM_RULES
$(1)/%.o: src/%.c $$(HEADERS) $$(TP)/miniaudio.h
	@mkdir -p $$(@D)
	$(2) $$(CFLAGS) $(3) $$(CPPFLAGS) $$(CODEC_INCLUDES) -c -o $$@ $$<

$(1)/tp/ogg/%.o: $$(TP)/ogg/%.c
	@mkdir -p $$(@D)
	$(2) $$(TP_CFLAGS) $$(OGG_FLAGS) -c -o $$@ $$<

$(1)/tp/opus/%.o: $$(TP)/opus/%.c
	@mkdir -p $$(@D)
	$(2) $$(TP_CFLAGS) $$(OPUS_FLAGS) -c -o $$@ $$<

$(1)/tp/opusfile/%.o: $$(TP)/opusfile/%.c
	@mkdir -p $$(@D)
	$(2) $$(TP_CFLAGS) $$(OPUSFILE_FLAGS) -c -o $$@ $$<

$(1)/tp/libxmp-lite/%.o: $$(TP)/libxmp-lite/%.c
	@mkdir -p $$(@D)
	$(2) $$(TP_CFLAGS) $$(XMP_FLAGS) -c -o $$@ $$<

$(1)/tp/faad2/%.o: $$(TP)/faad2/%.c
	@mkdir -p $$(@D)
	$(2) $$(TP_CFLAGS) $$(FAAD_FLAGS) -c -o $$@ $$<

$(1)/tp/%.o: $$(TP)/%.c
	@mkdir -p $$(@D)
	$(2) $$(TP_CFLAGS) $$(STB_FLAGS) -c -o $$@ $$<
endef

objects = $(patsubst %.c,$(1)/%.o,$(COMMON_SRC) $(2)) $(patsubst %.c,$(1)/tp/%.o,$(TP_SRC))

# --- Linux ------------------------------------------------------------------
LINUX_DIR  := build/linux
LINUX_OBJ  := $(call objects,$(LINUX_DIR),platform_x11.c mpris.c net_posix.c cd_linux.c icon_data.c)
# X11 headers are bundled and the program links against the runtime library
# that every desktop has, so no development package is needed. With
# libx11-dev installed, "make X11_CFLAGS= X11_LIBS=-lX11" uses the system's.
X11_CFLAGS ?= -isystem $(TP)/x11/include
X11_LIBS   ?= -l:libX11.so.6
LINUX_LIBS := $(X11_LIBS) -lm -lpthread -ldl
$(eval $(call PLATFORM_RULES,$(LINUX_DIR),$(CC),-fdata-sections $(X11_CFLAGS)))

# --- Windows (MinGW-w64) -------------------------------------------------------
# Two builds from the same sources: 32-bit (i686), which runs on everything
# from legacy systems to current ones, and 64-bit (x86_64).
#
# The cross-compilers: ones installed on the system (package mingw-w64) are
# used if present. Otherwise the first build fetches its own copy into
# .toolchain/ with tools/local-mingw.sh (60-70 MB each, no root needed) and
# uses that from then on.
WIN_CC        ?= i686-w64-mingw32-gcc
WIN_WINDRES   ?= i686-w64-mingw32-windres
WIN64_CC      ?= x86_64-w64-mingw32-gcc
WIN64_WINDRES ?= x86_64-w64-mingw32-windres
LOCAL_MINGW := $(CURDIR)/.toolchain/mingw/usr/bin
ifeq ($(shell command -v $(WIN_CC) 2>/dev/null),)
WIN_TOOLCHAIN := $(LOCAL_MINGW)/i686-w64-mingw32-gcc
endif
ifeq ($(shell command -v $(WIN64_CC) 2>/dev/null),)
WIN64_TOOLCHAIN := $(LOCAL_MINGW)/x86_64-w64-mingw32-gcc
endif
ifneq ($(WIN_TOOLCHAIN)$(WIN64_TOOLCHAIN),)
export PATH := $(LOCAL_MINGW):$(PATH)
endif
WIN_DIR   := build/win32
WIN64_DIR := build/win64
WIN_OBJ   := $(call objects,$(WIN_DIR),platform_win32.c smtc.c net_win32.c cd_win32.c) $(WIN_DIR)/resources.o
WIN64_OBJ := $(call objects,$(WIN64_DIR),platform_win32.c smtc.c net_win32.c cd_win32.c) $(WIN64_DIR)/resources.o
WIN_LIBS := -lgdi32 -lcomdlg32 -lshell32
WIN_LDFLAGS := -mwindows -static-libgcc
# No -fdata-sections here: on PE targets it moves zero-initialised data out
# of .bss and into the file, which bloats the executable.
$(eval $(call PLATFORM_RULES,$(WIN_DIR),$(WIN_CC),))
$(eval $(call PLATFORM_RULES,$(WIN64_DIR),$(WIN64_CC),))

# The version number is compiled into these, so they follow the Makefile.
$(foreach dir,$(LINUX_DIR) $(WIN_DIR) $(WIN64_DIR),$(dir)/ui_about.o $(dir)/stream.o): Makefile

# Plain "make" builds for the machine it runs on; "make all" builds every
# release file.
.PHONY: all linux windows windows32 windows64 installer deb rpm dist clean test test-gui fonts website soak
.DEFAULT_GOAL := linux
all: linux deb rpm windows installer
linux: $(LINUX_DIR)/amplitude
windows: windows32 windows64
windows32: $(WIN_DIR)/amplitude.exe
windows64: $(WIN64_DIR)/amplitude.exe

$(LINUX_DIR)/amplitude: $(LINUX_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LINUX_LIBS)

# Set only when a compiler is not installed: everything for that build then
# waits for the local copy to be fetched.
ifdef WIN_TOOLCHAIN
$(WIN_OBJ): | $(WIN_TOOLCHAIN)

$(WIN_TOOLCHAIN):
	@echo "No 32-bit MinGW-w64 compiler installed: fetching one into .toolchain/ (once)"
	tools/local-mingw.sh i686
endif
ifdef WIN64_TOOLCHAIN
$(WIN64_OBJ): | $(WIN64_TOOLCHAIN)

$(WIN64_TOOLCHAIN):
	@echo "No 64-bit MinGW-w64 compiler installed: fetching one into .toolchain/ (once)"
	tools/local-mingw.sh x86_64
endif

# The icon embedded in the executable.
$(WIN_DIR)/resources.o: packaging/amplitude.rc packaging/amplitude.ico
	@mkdir -p $(@D)
	$(WIN_WINDRES) -I packaging -o $@ $<

$(WIN64_DIR)/resources.o: packaging/amplitude.rc packaging/amplitude.ico
	@mkdir -p $(@D)
	$(WIN64_WINDRES) -I packaging -o $@ $<

$(WIN_DIR)/amplitude.exe: $(WIN_OBJ)
	$(WIN_CC) $(LDFLAGS) $(WIN_LDFLAGS) -o $@ $^ $(WIN_LIBS)

$(WIN64_DIR)/amplitude.exe: $(WIN64_OBJ)
	$(WIN64_CC) $(LDFLAGS) $(WIN_LDFLAGS) -o $@ $^ $(WIN_LIBS)

# --- Windows installer (NSIS) ---------------------------------------------------
# One setup program holding both builds; see packaging/installer.nsi. makensis
# from the system (package nsis) is used if present, otherwise a copy is
# fetched into .toolchain/ with tools/local-nsis.sh (2 MB, no root needed).
MAKENSIS ?= makensis
LOCAL_NSIS := $(CURDIR)/.toolchain/nsis/usr
ifeq ($(shell command -v $(MAKENSIS) 2>/dev/null),)
MAKENSIS := NSISDIR=$(LOCAL_NSIS)/share/nsis $(LOCAL_NSIS)/bin/makensis
NSIS_TOOLCHAIN := $(LOCAL_NSIS)/bin/makensis

$(NSIS_TOOLCHAIN):
	@echo "NSIS is not installed: fetching it into .toolchain/ (once)"
	tools/local-nsis.sh
endif
INSTALLER32 := build/amplitude-$(VERSION)-win32-setup.exe
INSTALLER64 := build/amplitude-$(VERSION)-win64-setup.exe

installer: $(INSTALLER32) $(INSTALLER64)

# $(1) = 32 or 64, $(2) = the player it packs
define INSTALLER_RULE
build/amplitude-$$(VERSION)-win$(1)-setup.exe: $(2) packaging/installer.nsi packaging/amplitude.ico LICENSE | $$(NSIS_TOOLCHAIN)
	$$(MAKENSIS) -V2 -NOCD -DVERSION=$$(VERSION) -DBITS=$(1) -DOUTFILE=$$@ packaging/installer.nsi
endef
$(eval $(call INSTALLER_RULE,32,$(WIN_DIR)/amplitude.exe))
$(eval $(call INSTALLER_RULE,64,$(WIN64_DIR)/amplitude.exe))

# --- Debian package -----------------------------------------------------------
DEB_ARCH := $(shell dpkg --print-architecture 2>/dev/null)
DEB_ROOT := build/deb/amplitude_$(VERSION)_$(DEB_ARCH)

deb: linux
	rm -rf $(DEB_ROOT)
	install -Dm755 $(LINUX_DIR)/amplitude $(DEB_ROOT)/usr/bin/amplitude
	install -Dm644 packaging/amplitude.desktop $(DEB_ROOT)/usr/share/applications/amplitude.desktop
	install -Dm644 packaging/amplitude-enqueue.desktop $(DEB_ROOT)/usr/share/kio/servicemenus/amplitude-enqueue.desktop
	install -Dm644 assets/amplitude.svg $(DEB_ROOT)/usr/share/icons/hicolor/scalable/apps/amplitude.svg
	for size in 16 24 32 48 64 128 256; do \
	    install -Dm644 assets/icons/amplitude-$$size.png \
	        $(DEB_ROOT)/usr/share/icons/hicolor/$${size}x$${size}/apps/amplitude.png; \
	done
	install -Dm644 packaging/copyright $(DEB_ROOT)/usr/share/doc/amplitude/copyright
	install -d -m755 $(DEB_ROOT)/usr/share/metainfo $(DEB_ROOT)/DEBIAN
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@DATE@/$(RELEASE_DATE)/' packaging/amplitude.metainfo.xml \
	    > $(DEB_ROOT)/usr/share/metainfo/amplitude.metainfo.xml
	chmod 644 $(DEB_ROOT)/usr/share/metainfo/amplitude.metainfo.xml
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(DEB_ARCH)/' \
	    -e 's/@MAINTAINER@/$(DEB_MAINTAINER)/' packaging/control.in > $(DEB_ROOT)/DEBIAN/control
	dpkg-deb --build --root-owner-group $(DEB_ROOT) build/amplitude_$(VERSION)_$(DEB_ARCH).deb

# --- Tests ---------------------------------------------------------------------
# Every unit test links the whole portable code base (everything except
# main.c and the platform layer), compiled with the address and
# undefined-behaviour sanitizers, against the normal third-party objects.
TEST_DIR    := build/test
TEST_CFLAGS := -g -O1 -std=gnu99 -Wall -Wextra -fsanitize=address,undefined -Isrc -Itests
TEST_NAMES  := test_gfx test_data test_tags test_skin test_render test_codecs test_audio test_stream test_cd
TEST_CORE   := $(patsubst %.c,$(TEST_DIR)/core/%.o,$(filter-out main.c,$(COMMON_SRC))) $(TEST_DIR)/core/stubs.o $(TEST_DIR)/core/net_posix.o $(TEST_DIR)/core/cd_linux.o
TEST_TP     := $(patsubst %.c,$(LINUX_DIR)/tp/%.o,$(TP_SRC))

$(TEST_DIR)/core/%.o: src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(CODEC_INCLUDES) -c -o $@ $<

# miniaudio's own code trips the undefined-behaviour checker.
$(TEST_DIR)/core/audio.o: TEST_CFLAGS := $(filter-out -fsanitize=%,$(TEST_CFLAGS)) -fsanitize=address

$(TEST_DIR)/core/stubs.o: tests/stubs.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

$(TEST_DIR)/test_%: tests/test_%.c tests/test.h tests/inflate_vectors.h $(TEST_CORE) $(TEST_TP)
	$(CC) $(TEST_CFLAGS) $(CODEC_INCLUDES) -o $@ $< $(TEST_CORE) $(TEST_TP) -lm -lpthread -ldl

# AUDIO_TEST_ENV is put in front of the audio test only. On a machine with
# no sound server (a build server) the system's sound libraries leak a
# little when they fail to connect, which the leak checker reports as ours:
#     make test AUDIO_TEST_ENV=ASAN_OPTIONS=detect_leaks=0
AUDIO_TEST_ENV ?=

test: $(TEST_NAMES:%=$(TEST_DIR)/%)
	@for t in $(TEST_NAMES); do echo "$$t"; \
	    if [ $$t = test_audio ] || [ $$t = test_stream ] || [ $$t = test_cd ]; then env $(AUDIO_TEST_ENV) $(TEST_DIR)/$$t tests/media || exit 1; \
	    else $(TEST_DIR)/$$t tests/media || exit 1; fi; \
	done; echo "all tests passed"

$(TEST_DIR)/drive: tests/drive.c
	@mkdir -p $(@D)
	$(CC) -g -std=gnu99 -Wall -Wextra $(CPPFLAGS) $(X11_CFLAGS) -o $@ $< $(filter -L%,$(LDFLAGS)) $(X11_LIBS)

$(TEST_DIR)/mkwav: tests/mkwav.c tests/test.h
	@mkdir -p $(@D)
	$(CC) -g -std=gnu99 -Wall -Wextra -Itests -o $@ $< -lm

test-gui: linux $(TEST_DIR)/drive $(TEST_DIR)/mkwav
	tests/gui_test.sh $(LINUX_DIR)/amplitude $(TEST_DIR)/drive $(TEST_DIR)/mkwav

# A long run under a script that keeps the player busy, watching for leaks
# of memory, threads and file handles. See tests/soak.sh.
SOAK_MINUTES ?= 30
soak: linux $(TEST_DIR)/drive $(TEST_DIR)/mkwav
	tests/soak.sh $(LINUX_DIR)/amplitude $(TEST_DIR)/drive $(TEST_DIR)/mkwav $(SOAK_MINUTES)

# --- Generated sources ----------------------------------------------------------
FONT_DIR := third_party/fonts

fonts:
	tools/genfont.py 5x7=$(FONT_DIR)/5x7.bdf 6x10=$(FONT_DIR)/6x10.bdf \
	    7x13=$(FONT_DIR)/7x13.bdf:$(FONT_DIR)/6x10.bdf 9x15=$(FONT_DIR)/9x15.bdf:$(FONT_DIR)/6x10.bdf \
	    10x20=$(FONT_DIR)/10x20.bdf:$(FONT_DIR)/6x10.bdf > src/font_data.c

# The images in website/img: screenshots rendered by the player's own drawing
# code (no display needed), plus the background, link preview and logo copies.
WEBSHOT_SRC := tools/webshots.c tests/stubs.c src/cd.c src/cd_linux.c $(addprefix src/,ui.c ui_eq.c ui_playlist.c ui_jump.c ui_dialog.c \
    ui_menu.c playlist.c tags.c theme.c gfx.c font_data.c skin.c skin_default.c zip.c)

website: $(WEBSHOT_SRC)
	@mkdir -p build/website
	$(CC) -std=gnu99 -Wall -Wextra -O1 -Isrc -o build/webshots $(WEBSHOT_SRC) -lm
	build/webshots build/website
	tools/genwebsite.py build/website

# --- RPM package --------------------------------------------------------------
# For Fedora, openSUSE, RHEL and relatives; see packaging/amplitude.spec.
# rpmbuild from the system is used if present, otherwise a copy is fetched
# into .toolchain/ with tools/local-rpm.sh (2 MB, no root needed).
RPMBUILD ?= rpmbuild
LOCAL_RPM := $(CURDIR)/.toolchain/rpm/usr
ifeq ($(shell command -v $(RPMBUILD) 2>/dev/null),)
RPMBUILD := LD_LIBRARY_PATH=$(LOCAL_RPM)/lib/x86_64-linux-gnu RPM_CONFIGDIR=$(LOCAL_RPM)/lib/rpm \
            $(LOCAL_RPM)/bin/rpmbuild
RPM_TOOLCHAIN := $(LOCAL_RPM)/bin/rpmbuild

$(RPM_TOOLCHAIN):
	@echo "rpmbuild is not installed: fetching it into .toolchain/ (once)"
	tools/local-rpm.sh
endif
RPM_ARCH := $(shell uname -m)
RPM := build/amplitude-$(VERSION)-1.$(RPM_ARCH).rpm

rpm: linux | $(RPM_TOOLCHAIN)
	rm -rf build/rpm
	mkdir -p build/rpm
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@DATE@/$(RELEASE_DATE)/' packaging/amplitude.metainfo.xml \
	    > build/rpm/amplitude.metainfo.xml
	$(RPMBUILD) -bb --quiet --target $(RPM_ARCH) --define "version $(VERSION)" --define "srcroot $(CURDIR)" \
	    --define "_topdir $(CURDIR)/build/rpm" --define "_dbpath $(CURDIR)/build/rpm/db" --define "packager_name $(DEB_MAINTAINER)" packaging/amplitude.spec
	cp build/rpm/RPMS/$(RPM_ARCH)/amplitude-$(VERSION)-1.$(RPM_ARCH).rpm $(RPM)

# --- Release files ---------------------------------------------------------------
# Everything that goes on the download page, and nothing else, in build/dist:
#   amplitude-<ver>-win32-setup.exe, -win64-setup.exe    the Windows installers
#   amplitude-<ver>-win32.zip    portable: amplitude.exe, the licence, a README, and an empty
#                                amplitude.ini, which keeps the settings in that folder
#   amplitude-<ver>-win64.zip
#   amplitude_<ver>_<arch>.deb
#   amplitude-<ver>-1.<arch>.rpm
#   amplitude-<ver>-linux-<arch>.tar.gz   portable: the program, its icon and menu entry, a README,
#                                and an empty amplitude.ini, which keeps the settings in that folder
#   amplitude-<ver>.tar.gz       the source, as last committed to git
DIST := build/dist
LINUX_PORTABLE := amplitude-$(VERSION)-linux-$(RPM_ARCH)

dist: all
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp $(INSTALLER32) $(INSTALLER64) build/amplitude_$(VERSION)_$(DEB_ARCH).deb $(RPM) $(DIST)/
	for bits in 32 64; do \
	    mkdir -p $(DIST)/amplitude-$(VERSION)-win$$bits && \
	    cp build/win$$bits/amplitude.exe $(DIST)/amplitude-$(VERSION)-win$$bits/ && \
	    cp LICENSE $(DIST)/amplitude-$(VERSION)-win$$bits/LICENSE.txt && \
	    sed -e 's/@VERSION@/$(VERSION)/' -e "s/@BITS@/$$bits/" -e 's/$$/\r/' packaging/README-windows.txt \
	        > $(DIST)/amplitude-$(VERSION)-win$$bits/README.txt && \
	    : > $(DIST)/amplitude-$(VERSION)-win$$bits/amplitude.ini && \
	    (cd $(DIST) && zip -q -r -9 amplitude-$(VERSION)-win$$bits.zip amplitude-$(VERSION)-win$$bits) && \
	    rm -r $(DIST)/amplitude-$(VERSION)-win$$bits || exit 1; \
	done
	mkdir -p $(DIST)/$(LINUX_PORTABLE)
	install -m755 $(LINUX_DIR)/amplitude $(DIST)/$(LINUX_PORTABLE)/amplitude
	install -m644 LICENSE packaging/amplitude.desktop $(DIST)/$(LINUX_PORTABLE)/
	install -m644 assets/icons/amplitude-128.png $(DIST)/$(LINUX_PORTABLE)/amplitude.png
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(RPM_ARCH)/' packaging/README-linux.txt > $(DIST)/$(LINUX_PORTABLE)/README.txt
	: > $(DIST)/$(LINUX_PORTABLE)/amplitude.ini
	tar -C $(DIST) --owner=0 --group=0 --numeric-owner -czf $(DIST)/$(LINUX_PORTABLE).tar.gz $(LINUX_PORTABLE)
	rm -r $(DIST)/$(LINUX_PORTABLE)
	git archive --format=tar.gz --prefix=amplitude-$(VERSION)/ -o $(DIST)/amplitude-$(VERSION).tar.gz HEAD .
	@ls -l $(DIST)

clean:
	rm -rf build
