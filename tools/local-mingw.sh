#!/bin/sh
# Sets up the MinGW-w64 cross-compiler for the Windows builds inside the
# project, without root and without installing anything system-wide.
#
#     tools/local-mingw.sh [i686] [x86_64]
#
# With no argument both compilers are fetched: i686 for "make windows32"
# and x86_64 for "make windows64". Each is a download of about
# 60-70 MB, unpacked into .toolchain/mingw. The Makefile runs this script by
# itself when a compiler is missing, and uses what it finds there. Delete
# .toolchain/ to remove everything.
#
# Needs a Debian-based system and network access once. If you can install
# packages normally, "sudo apt install mingw-w64" does the same job and this
# script is not needed.
set -eu
cd "$(dirname "$0")/.."

DEST=.toolchain/mingw
[ $# -gt 0 ] || set -- i686 x86_64

command -v apt >/dev/null && command -v dpkg-deb >/dev/null || {
    echo "local-mingw: this needs apt and dpkg-deb (Debian, Ubuntu and relatives)" >&2
    exit 1
}

# Only one copy at a time: "make -j windows windows64" may start two.
mkdir -p .toolchain
while ! mkdir .toolchain/lock 2>/dev/null; do sleep 1; done
trap 'rmdir .toolchain/lock' EXIT

for arch in "$@"; do
    case "$arch" in
        i686)   package_arch=i686 ;;
        x86_64) package_arch=x86-64 ;;
        *) echo "local-mingw: unknown architecture '$arch' (use i686 or x86_64)" >&2; exit 1 ;;
    esac
    triple="$arch-w64-mingw32"
    if "$DEST/usr/bin/$triple-gcc" --version >/dev/null 2>&1; then
        echo "$triple: already set up"
        continue
    fi

    # The two compilers share their headers (mingw-w64-common). Once those
    # are in place they are left alone: with "make -j" the other compiler
    # may already be at work, and unpacking the same files again would pull
    # them out from under it for a moment.
    shared="gcc-mingw-w64-base mingw-w64-common"
    [ ! -f "$DEST/usr/share/mingw-w64/include/_mingw.h" ] || shared=""

    rm -rf .toolchain/download
    mkdir -p "$DEST" .toolchain/download
    echo "$triple: downloading packages"
    # shellcheck disable=SC2086  # $shared is a list of words on purpose
    (cd .toolchain/download && apt download \
        "gcc-mingw-w64-$package_arch-win32" "gcc-mingw-w64-$package_arch-win32-runtime" \
        "binutils-mingw-w64-$package_arch" "mingw-w64-$package_arch-dev" $shared)
    echo "$triple: unpacking"
    for package in .toolchain/download/*.deb; do
        dpkg-deb -x "$package" "$DEST"
    done
    rm -rf .toolchain/download

    # A normal installation provides these unsuffixed names through the
    # alternatives system; here they are plain links.
    for tool in gcc cpp; do
        ln -sf "$triple-$tool-win32" "$DEST/usr/bin/$triple-$tool"
    done

    if "$DEST/usr/bin/$triple-gcc" --version >/dev/null 2>&1; then
        echo "$triple: ready ($("$DEST/usr/bin/$triple-gcc" --version | head -1))"
    else
        echo "local-mingw: $triple was unpacked but does not run; it may need shared" >&2
        echo "libraries (libisl, libmpc, libmpfr) that a native gcc normally brings" >&2
        exit 1
    fi
done
