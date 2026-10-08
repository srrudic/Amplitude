# Using Amplitude

The command line, keyboard, settings and skins in detail. For building and
for how the code works, see `DEVELOPMENT.md`.

## Running it

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

**Streams.** Internet radio stations and audio files on a web server play
like any other entry. Give the address to "Open location..." (Ctrl+L, or in
the menu; Ctrl+V pastes), to "Add location..." in the playlist's ADD menu,
or on the command line; or open or drop the `.pls` or `.m3u` file a station
offers. The playlist shows the station's name once it has connected, and
the title display shows the song the station says it is playing. While a
stream connects or refills its buffer the display says "Buffering...".
A stream has no length and cannot be wound; pausing keeps a few seconds
buffered, and Stop followed by Play connects afresh. Stations sending MP3,
AAC (including AAC+), Ogg Vorbis or Opus work, as do HLS streams (addresses
ending in `.m3u8`) and MP3, FLAC, WAV and Ogg files on a web server. Not
supported: encrypted HLS streams, FLAC in Ogg, `.m4a` files on the web, and
the long-abandoned Windows Media and RealAudio streams. Given the address
of an HLS video, the player plays its sound. Secure
(`https`) addresses use the system's own encryption: OpenSSL on Linux, and
on Windows whatever that version of Windows provides, which on XP and
older is too old for many of today's servers.

**Play queue.** To hear something next without changing the list, queue
it: select tracks in the playlist and press Q (or MISC, "Queue selected to
play next"), or in jump to file use the ENQUEUE button, Ctrl+Q or Shift+Enter on a match. Queued tracks
show their place as `[1]`, `[2]`, ... and are played in that order as soon
as the current track ends, before shuffle or list order continues. Doing the same again takes a track back out; "Clear queue" empties it. The
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
