#!/bin/sh
# Sets up NSIS, the program that builds the Windows installer, inside the
# project, without root and without installing anything system-wide.
#
#     tools/local-nsis.sh
#
# "make installer" runs this by itself when makensis is not installed. It
# downloads the Debian/Ubuntu packages (about 2 MB) and unpacks them into
# .toolchain/nsis. Delete .toolchain/ to remove it.
#
# Needs a Debian-based system and network access once. If you can install
# packages normally, "sudo apt install nsis" does the same job and this
# script is not needed.
set -eu
cd "$(dirname "$0")/.."

DEST=.toolchain/nsis

command -v apt >/dev/null && command -v dpkg-deb >/dev/null || {
    echo "local-nsis: this needs apt and dpkg-deb (Debian, Ubuntu and relatives)" >&2
    exit 1
}

rm -rf "$DEST" .toolchain/nsis-download
mkdir -p "$DEST" .toolchain/nsis-download
echo "nsis: downloading packages"
(cd .toolchain/nsis-download && apt download nsis nsis-common)
echo "nsis: unpacking"
for package in .toolchain/nsis-download/*.deb; do
    dpkg-deb -x "$package" "$DEST"
done
rm -rf .toolchain/nsis-download

# makensis looks for its data in /usr/share/nsis unless NSISDIR says
# otherwise; the Makefile sets that.
if NSISDIR="$PWD/$DEST/usr/share/nsis" "$DEST/usr/bin/makensis" -VERSION >/dev/null 2>&1; then
    echo "nsis: ready ($(NSISDIR="$PWD/$DEST/usr/share/nsis" "$DEST/usr/bin/makensis" -VERSION))"
else
    echo "local-nsis: makensis was unpacked but does not run" >&2
    exit 1
fi
