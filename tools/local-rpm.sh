#!/bin/sh
# Sets up rpmbuild, the program that builds the .rpm package, inside the
# project, without root and without installing anything system-wide.
#
#     tools/local-rpm.sh
#
# "make rpm" runs this by itself when rpmbuild is not installed. It
# downloads the Debian/Ubuntu packages (about 2 MB) and unpacks them into
# .toolchain/rpm. Delete .toolchain/ to remove it.
#
# Needs a Debian-based system and network access once. If you can install
# packages normally, "sudo apt install rpm" does the same job and this script
# is not needed. (On Fedora, openSUSE and relatives rpmbuild is in the
# rpm-build package.)
set -eu
cd "$(dirname "$0")/.."

DEST=.toolchain/rpm
LIBS="$PWD/$DEST/usr/lib/x86_64-linux-gnu"

command -v apt >/dev/null && command -v dpkg-deb >/dev/null || {
    echo "local-rpm: this needs apt and dpkg-deb (Debian, Ubuntu and relatives)" >&2
    exit 1
}

rm -rf "$DEST" .toolchain/rpm-download
mkdir -p "$DEST" .toolchain/rpm-download
echo "rpm: downloading packages"
# rpmbuild itself, its libraries, and the three libraries those need that a
# desktop system does not normally have.
(cd .toolchain/rpm-download && apt download rpm rpm-common librpm10 librpmbuild10 librpmio10 librpmsign10 \
    liblua5.3-0 librpm-sequoia-1 libfsverity0)
echo "rpm: unpacking"
for package in .toolchain/rpm-download/*.deb; do
    dpkg-deb -x "$package" "$DEST"
done
rm -rf .toolchain/rpm-download

# rpmbuild looks for its libraries and settings under /usr unless told
# otherwise; the Makefile sets the same two variables.
if LD_LIBRARY_PATH="$LIBS" RPM_CONFIGDIR="$PWD/$DEST/usr/lib/rpm" "$DEST/usr/bin/rpmbuild" --version >/dev/null 2>&1; then
    echo "rpm: ready ($(LD_LIBRARY_PATH="$LIBS" "$DEST/usr/bin/rpmbuild" --version))"
else
    echo "local-rpm: rpmbuild was unpacked but does not run" >&2
    exit 1
fi
