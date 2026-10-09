Amplitude Player @VERSION@ for Linux (@ARCH@)
https://amplitude.cr.rs

This is the player by itself, with nothing to install. To start it, run the
file "amplitude" in this folder, from a file manager or a terminal:

    ./amplitude
    ./amplitude ~/Music

It needs a graphical desktop and nothing else that a desktop Linux does not
already have. It was built for systems from about 2021 on (glibc 2.34 or
later: Debian 12, Ubuntu 22.04, Fedora 35 and newer).

Settings and the playlist are kept in ~/.config/amplitude. To keep them in
this folder instead, so that the player can be carried around on a USB
stick with everything it remembers, create an empty file here called
"amplitude.ini":

    touch amplitude.ini

To have it in the applications menu and offered for audio files, copy the
program somewhere on your PATH and the other two files to where the desktop
looks for them:

    cp amplitude ~/.local/bin/
    cp amplitude.desktop ~/.local/share/applications/
    mkdir -p ~/.local/share/icons/hicolor/128x128/apps
    cp amplitude.png ~/.local/share/icons/hicolor/128x128/apps/

(The .deb and .rpm packages on the website do all of that by themselves.)

Amplitude Player is free software under the GNU General Public License,
version 3 or later; see LICENSE. It is provided as is, without warranty.
The source code is at https://github.com/srrudic/Amplitude
