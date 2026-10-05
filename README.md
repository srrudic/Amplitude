# Amplitude

A lightweight audio player for old and new computers, written in C99.
Website: https://amplitude.cr.rs

Plays MP3, FLAC, WAV, Ogg Vorbis, Opus, AAC (M4A/MP4 and raw ADTS) and
tracker modules (MOD, XM, S3M, IT). All decoders are compiled in; there are
no runtime dependencies beyond the system's own libraries.

## Building

Linux (needs only `gcc` and `make`; the X11 headers are bundled):

    make
    ./build/linux/amplitude song.mp3

Windows, cross-compiled from Linux:

    make windows      # both: build/win32/amplitude.exe (32-bit)
                      #       build/win64/amplitude.exe (64-bit)
    make windows32    # only the 32-bit one
    make windows64    # only the 64-bit one

The Windows installers, one for each build:

    make installer    # build/amplitude-<version>-win32-setup.exe
                      # build/amplitude-<version>-win64-setup.exe

It is made with NSIS. If `makensis` is not installed, the first run fetches
it into `.toolchain/` (2 MB), the same way as the compiler.

Everything at once (Linux, the `.deb` and `.rpm`, both Windows builds and
the installers):

    make all

The files to publish, collected in `build/dist/` with nothing else around
them: the two installers, the `.deb` and `.rpm`, a portable zip for each Windows build
(`amplitude.exe` and the licence), and the source as a `.tar.gz` (taken from
the last git commit):

    make dist

The 32-bit build runs on every Windows, old and new, 64-bit systems
included; it is the one to use for legacy machines. The 64-bit build is for
current systems only.

Both need the MinGW-w64 compiler. If it is not installed, the first build
downloads the one it needs (60-70 MB each, no root needed) and unpacks it
into `.toolchain/` inside the project, where the two together take about
800 MB. To use a system-wide compiler instead, `sudo apt install mingw-w64`.

Debian package (Debian, Ubuntu, Mint and relatives):

    make deb
    sudo apt install ./build/amplitude_*.deb

RPM package (Fedora, openSUSE, RHEL and relatives):

    make rpm          # build/amplitude-<version>-1.<arch>.rpm

It needs `rpmbuild`. If that is not installed, the first run fetches it into
`.toolchain/` (2 MB), like the other tools. The package holds the same
player as the `.deb`, so it needs glibc 2.34 or newer on the system it is
installed on (Fedora 35, RHEL 9, openSUSE Leap 15.4 and later).

Tests:

    make test       # unit tests
    make test-gui   # drives the real player; needs an X display

Everything needed to build and test is in the tree; nothing is downloaded.

## Usage

    amplitude [--scale N|auto] [--skin file.wsz|default] [--enqueue] [file|folder...]

By default the windows are drawn at 1.2 times their native 275x116 size,
multiplied by the desktop's display scaling (120% on an unscaled desktop,
150% at 125%, 240% at 200%). To change that, right-click the player and pick
a size from "Size..."; the choice is remembered, and "Automatic" returns to
the default. (`--scale 2` and `--scale auto` do the same from the command
line.)

The built-in skin and all titles are drawn directly at the final resolution,
so they stay sharp at any factor. Classic `.wsz` skins are bitmaps: they are
pixel-perfect at whole factors (1, 2, 3) and slightly softened in between,
with the title and playlist text still sharp. Arguments
can be audio files, `.m3u` playlists or folders (added recursively, in name
order). Started without files, Amplitude brings back the previous playlist.

Only one player runs at a time: starting Amplitude again hands its files to
the running one, which plays the first of them, or just adds them with
`--enqueue`.
Starts that come within a second of each other count as one request, so
opening a selection of files from a file manager that starts the program
once per file plays the first and adds the rest. In KDE's file manager the
package adds "Enqueue in Amplitude" to the right-click menu of audio files
and folders; on Windows the installer adds the same entry.

Files, folders and `.wsz` skins can also be dragged onto the windows.
Dropping on the playlist only adds; dropping elsewhere also starts playing.
Right-click any window, or click the cog button in the main window, for the menu: add files or a folder, show or hide
the equaliser and playlist, shuffle, repeat, and choose a skin, a size or a
colour.

| Key        | Action          |
|------------|-----------------|
| Z          | Previous track  |
| X          | Play            |
| C          | Pause / resume  |
| V          | Stop            |
| B          | Next track      |
| L          | Open file       |
| S          | Toggle shuffle  |
| R          | Toggle repeat   |
| J, Ctrl+J  | Jump to file    |
| G          | Show/hide equaliser |
| E          | Show/hide playlist  |
| Left/Right | Seek 5 seconds  |
| Up/Down    | Volume          |

**Media keys.** Play/pause, stop, next and previous keys work whichever
program has the keyboard, and so do the buttons of Bluetooth headphones and
the desktop's own media controls. On Linux this goes through the desktop's
media-player interface (MPRIS), which also lets the panel show what is
playing; where there is none, the keys are taken from the X server
directly. On Windows they are registered as system-wide hot keys.

While the volume, balance, seek or an equaliser slider is held, the title
display shows its value ("Volume: 75%", "Balance: 20% left", "Seek: 1:23 /
3:42 (37%)", "EQ: 1 kHz: +4.5 dB") in place of the track name. A volume
step from the arrow keys or the mouse wheel shows the volume there for a
second.

In the playlist window: Up/Down move the selection, Enter plays it, Delete
removes the selected tracks, a double click plays a track and the mouse
wheel scrolls. Ctrl+click toggles a track, Shift+click selects a range and
Ctrl+A selects everything. Drag the bottom right corner to resize.
The ADD button offers "Add files..." (several can be picked at once) and
"Add folder...", which adds every audio file in the folder and all its
subfolders, in name order.

The other buttons along the bottom each open a menu too:

| Button | Menu |
|--------|------|
| REM    | Remove selected, Crop to selected, Remove missing files, Remove all |
| SEL    | Select all, Select none, Invert selection |
| MISC   | Sort by title, by file name, or by folder and file; Reverse list; Randomize list; Queue selected to play next; Clear queue |
| LIST   | New list, Open list..., Save list... (as `.m3u`) |

**Play queue.** To hear something next without changing the list, queue
it: select tracks in the playlist and press Q (or MISC, "Queue selected to
play next"), or in jump to file press Shift+Enter on a match. Queued tracks
show their place as `[1]`, `[2]`, ... and are played in that order as soon
as the current track ends, before shuffle or list order continues. Q or
Shift+Enter again takes a track back out; "Clear queue" empties it. The
queue is not saved when the player closes.

Next to them are a small set of transport controls (previous, play, pause,
stop, next, open), the length of the selected tracks over the length of the
whole list, and the position in the current track.

Jump to file opens a search box over the playlist: type any words from a
title (case and accents do not matter), move with Up/Down, and press Enter to play the
highlighted track or Esc to cancel.

Click the visualiser to switch between spectrum analyser, oscilloscope and
off. Tracks follow each other without a gap.

In the equaliser, PRESETS opens a list of ready-made settings. Clicking the
"0" label left of the sliders resets everything to flat; "+12" and "-12" set
all bands to the maximum or minimum. With AUTO on, each track that starts
gets the preset matching its genre tag (Rock, Pop, Classical, Techno, ...);
tracks without a recognisable genre leave the sliders as they are.

Drag a window by its title bar. Windows snap to each other, and windows
docked to the main window move along with it.

## Settings

Volume, balance, shuffle, repeat, equaliser, skin, colour, scale, window layout and
the playlist are saved on exit and restored on the next start:

- Linux: `$XDG_CONFIG_HOME/amplitude/` (normally `~/.config/amplitude/`)
- Windows: `%APPDATA%\Amplitude\`, or next to `amplitude.exe` on systems
  without that folder (Windows 9x). For a portable install, create an empty
  `amplitude.ini` next to the executable and everything stays there.

`--scale` and `--skin` given on the command line are remembered;
`--scale auto` and `--skin default` undo them.

## Colour

The built-in look takes its colour from one setting: text, digits, the
visualiser, slider marks and the logo all follow it, in shades worked out
from the colour you pick. "Color..." in the right-click menu offers light
blue (the default), green, amber, yellow, red, pink, violet and white.

Any other colour can be set by editing `color=#RRGGBB` in `amplitude.ini`
while the player is closed. Classic skins keep their own artwork; the colour
then only affects the menu, the jump-to-file window and titles drawn with
the built-in fonts. The application icon does not change.

## Titles

Artist and title come from the file's tags (ID3v1/v2, Vorbis comments in
FLAC/Ogg/Opus, MP4 metadata, module names), falling back to the file name.

Text is Unicode throughout. A few small public-domain bitmap fonts are
built in, covering Latin (with all European accents), Greek and Cyrillic, so
the same titles display on every system, including ones with no Unicode
fonts of their own; the size that fits the current magnification is picked
automatically. Scripts they lack (Chinese, Japanese, Korean, Arabic, ...)
show a placeholder per character. In the main window a classic skin's own
lettering is used when it can spell the title and the magnification is a
whole factor; otherwise a built-in font takes over in the skin's text colour.

The font data in `src/font_data.c` is generated by `tools/genfont.py`.

## Skins

Amplitude loads classic 2.x skins (`.wsz` archives). Pick one with "Load
skin..." under "Skins..." in the menu, pass one with `--skin`,
list a `.wsz` file among the arguments, drop one on the player, or pick one
from the right-click menu. The menu lists the skins in the `skins` folder
inside the settings directory and those next to the skin currently in use.
Parts a skin does not provide fall back to the built-in look. No third-party
skins are bundled.

## Developing

`docs/DEVELOPMENT.md` explains how the code is organised and how each part
works, with recipes for common changes and a list of what is still missing.

## Layout

    src/platform.h        window/input/timing interface
    src/platform_x11.c    Linux backend
    src/platform_win32.c  Windows backend (Unicode on NT-based systems, ANSI on 9x)
    src/audio.c           playback engine (miniaudio) and equaliser DSP
    src/gfx.c             resolution-independent drawing and text rendering
    src/font_data.c       generated Unicode bitmap fonts (public domain)
    src/skin.c            .wsz skin loading (BMP decoding, viscolor.txt)
    src/skin_default.c    built-in skin, as drawing code
    src/zip.c             ZIP reader and inflate
    src/ui.c              main window layout and drawing
    src/ui_eq.c           equaliser window
    src/ui_playlist.c     playlist window
    src/ui_about.c        About window
    src/ui_dialog.c       frame and title bar shared by the two small windows
    src/util.h            small shared helpers (paths, clamp, array length)
    src/mpris.c           Linux: media keys and controls over D-Bus (MPRIS)
    src/ui_menu.c         right-click popup menu
    src/ui_jump.c         jump-to-file window
    src/playlist.c        track list, M3U loading and saving
    src/tags.c            artist/title tag reading
    src/config.c          saved settings
    src/presets.c         equaliser presets and genre matching
    assets/               logo (SVG) and icons, generated by tools/genlogo.py
    src/main.c            window management, input handling, main loop
    src/codec*.c          decoders beyond miniaudio's built-in WAV/FLAC/MP3
    third_party/          bundled libraries, fetched by third_party/fetch.sh

## Licence

Copyright © 2026 Srđan Rudić.

Amplitude is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 3 or (at your option)
any later version. It is provided as is, without warranty. See `LICENSE` for the full
text.

Bundled third-party code keeps its own licence:

| Library     | Used for            | Licence                    |
|-------------|---------------------|----------------------------|
| miniaudio   | output, WAV/FLAC/MP3| public domain / MIT-0      |
| stb_vorbis  | Ogg Vorbis          | public domain / MIT        |
| libogg, libopus, opusfile | Opus  | BSD 3-clause               |
| libfaad2    | AAC                 | GPL 2 or later             |
| minimp4     | MP4 container       | CC0                        |
| libxmp-lite | tracker modules     | MIT                        |
| misc-fixed fonts | title text     | public domain              |
