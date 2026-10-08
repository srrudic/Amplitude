# Testing Amplitude on Windows 11

This is the plan for the first test of the Windows builds on a real Windows
machine. It is written for whoever runs it, in practice a Claude Code
session started in this repository on the Windows 11 machine, with Srđan at
the keyboard for the few things a script cannot do.

Read sections 1 to 3 first. Then work through section 4 in order, write the
outcome of every test into the table in section 6, and fix what fails.

Background reading: `docs/DEVELOPMENT.md`, especially section 9 ("Rules for
the Windows build") and the part of section 5 about windows and models.

## 1. Why this exists

Everything for Windows was written and cross-compiled on Linux and **has
never been run**. An attempt to use Wine there failed. Until this plan has
been worked through, assume any of it may be broken, including basics such
as "the window appears".

What has been checked on Linux: the same portable code passes its unit
tests, an end-to-end GUI test, and a run under the address and leak
sanitizers. So failures here are most likely in `src/platform_win32.c`, in
the installer script, or in something the Linux tests cannot see (fonts at
other DPIs, drawing, file names).

Windows XP and Windows 98 are tested separately, by hand, by Srđan. This
plan is for Windows 11 only.

## 2. Rules

1. **Never touch the user's own Amplitude.** On Windows a second copy finds
   the first by window class and title and hands its files over, whatever
   settings folder it uses. Before starting a test player, make sure no
   Amplitude window is open (`Assert-NoOtherAmp` does this and stops if one
   is). If one is open, ask; do not close it yourself.
2. **Never use the user's settings.** Always run a copy made by
   `New-AmpSandbox`. It puts `amplitude.ini` next to the executable, which
   makes the player portable: settings and playlist then live in that
   folder and `%APPDATA%\Amplitude` is not read or written.
3. **Mute.** Start with `volume=0` in the sandbox's `amplitude.ini` unless
   the test is about hearing something. Tell the user before anything
   audible.
4. **The installer changes the machine** (Program Files, registry, file
   associations for `.mp3` and others). Run section 4.9 only when the user
   has agreed, and always finish it with the uninstall and the checks that
   everything was put back.
5. **Report what happened, not what should have.** A test that could not be
   run is "not run", with the reason. A helper that did not work is a
   finding too.
6. **A failure is a bug to fix**, in the code or in this document. Fix,
   rebuild, re-run that test and the ones near it, and note the fix in
   section 6.

## 3. Setting up

### 3.1 Getting the executables

The Makefile cross-compiles from Linux. On Windows 11, in order of
preference:

1. **WSL** (Debian or Ubuntu): `make windows` inside WSL, from the same
   checkout. The first run downloads the compiler into `.toolchain/`. The
   results appear in `build\win32\amplitude.exe` and
   `build\win64\amplitude.exe`, visible from Windows. `make installer`
   builds the two setup programs the same way.
2. **MSYS2**: untried. The Makefile expects the cross-compiler names
   (`i686-w64-mingw32-gcc`, `x86_64-w64-mingw32-gcc`); these can be
   overridden with `WIN_CC=`, `WIN_WINDRES=`, `WIN64_CC=`, `WIN64_WINDRES=`.
   If this route is needed, write down what it took in section 6.
3. **w64devkit** (used for the first run of this plan; needs nothing
   installed). Unpack the x64 and x86 releases of
   https://github.com/skeeto/w64devkit into `.toolchain\w64-x64` and
   `.toolchain\w64-x86`. Each holds `gcc`, `windres`, `objdump`, `make` and a
   shell, and links against msvcrt. Build each player with that kit's `bin`
   first in `PATH`:

       make windows64 WIN_CC=gcc WIN_WINDRES=windres WIN64_CC=gcc WIN64_WINDRES=windres
       make windows32 WIN_CC=gcc WIN_WINDRES=windres WIN64_CC=gcc WIN64_WINDRES=windres

   (x64 kit for the first, x86 kit for the second.) The compiler is newer
   than the one the release builds use, so sizes differ a little. `make
   installer` does not work this way: `tools/local-nsis.sh` unpacks a Debian
   package.
4. **Copy `build/dist/`** from the Linux machine. Fine for testing, but then
   fixes cannot be rebuilt here.

Record which route was used and the size and timestamp of each executable.

### 3.2 The helper script

`tests/windows/wintest.ps1` starts a sandboxed player, finds its windows,
posts mouse and keyboard messages to them, takes screenshots and reads
settings. It was written blind; the first run of this plan proved it and
added to it (section 6). Start with the same check:

    . .\tests\windows\wintest.ps1
    New-AmpSandbox -Bits 64 -Ini "volume=0"
    Start-Amp -Scale 1
    Get-AmpWindows | Format-Table
    Save-AmpShot "Amplitude" "$env:TEMP\amp-main.png"
    Stop-Amp

Look at the PNG with the Read tool. Expected: one visible window titled
"Amplitude" of 275x116, four hidden ones, and a screenshot showing the main
window in light blue on dark with "AMPLITUDE" in the title bar.

What else the helper has, and what to know when using it:

- **A new PowerShell for every command** (as in Claude Code) loses the
  helper's variables. `Start-Amp` writes the player down in
  `%TEMP%\amplitude-test.state`; dot-source the script again and call
  `Connect-Amp` to carry on with the same player.
- `Send-AmpDoubleClick`, `Send-AmpDrag`, `Save-AmpShotAll` (all visible
  windows and the menu in one picture) and `Compare-AmpShots` (how many
  pixels differ, and where).
- **The menu** is the window with the empty title: `Send-AmpClick "" 30 114`.
  Close it as a click outside would: `Send-AmpClick "" -20 -20`.
- **Dragging a title bar**: use `-Steps 1`. The player moves the window under
  the pointer, so every further posted move, given in window coordinates,
  moves it again.
- **Ctrl and Shift** are read by the player with `GetKeyState`, so
  `Send-AmpClick -Ctrl` presses the real key and brings the window to the
  front for the length of the click.
- `New-AmpSandbox -Keep` keeps the settings and playlist of the last run;
  `Start-Amp -Scale 0` passes no `--scale`. A layout written into the ini by
  hand is only used together with `has_layout=1`.

Things that may need fixing in the helper:

- **DPI.** The script calls `SetProcessDPIAware()` so its coordinates are
  real pixels. If window sizes come back scaled (for example 220x93 at 125%
  display scaling) or screenshots are cropped, that call did not take
  effect; fall back to setting the display to 100% for the scripted tests
  and say so.
- **Posted input.** Clicks are posted as window messages. The player takes
  mouse capture on button-down; if posted clicks are ignored, switch the
  helper to real input (`SendInput`), which needs the window in front.
- **Screenshots** are copied from the screen, so the window must not be
  covered. If another window keeps coming to the front, move the player.

### 3.3 Coordinates

All coordinates below are logical pixels of the classic layout. At
`-Scale 1` they are real pixels. `Send-AmpClick` multiplies by the scale.

Main window (275x116):

| Element | x, y | Element | x, y |
|---|---|---|---|
| close | 268, 7 | minimise | 258, 7 |
| previous | 27, 97 | play | 50, 97 |
| pause | 73, 97 | stop | 96, 97 |
| next | 119, 97 | open | 146, 97 |
| shuffle | 175, 97 | repeat | 197, 97 |
| cog (menu) | 225, 97 | logo (About) | 255, 96 |
| EQ toggle | 230, 64 | PL toggle | 253, 64 |
| visualiser | 60, 50 | seek bar | 140, 76 |

Equaliser (275x116): ON 20, 24; AUTO 50, 24; PRESETS 230, 24; the "0" label
50, 68; close 268, 7.

Playlist (width W, height H; 275x232 by default): ADD 24, H-22; REM 53,
H-22; SEL 82, H-22; MISC 111, H-22; LIST W-36, H-22; first row 100, 27, then
10 per row; resize grip W-7, H-7; close W-7, 7. Small controls at y = H-12:
previous W-143, play W-134, pause W-125, stop W-116, next W-107, open W-98.

Main menu at 100% (popup window, empty title; rows are 13 high, separators
5): Add files 8, Add folder 21, Open location 34, Play audio CD 47, Jump to file 60,
Equalizer 78, Playlist 91, Shuffle 109, Repeat 122, Skins 140, Size 153, Color 166,
About 184, Exit 197 (all at
x = 30). Submenus start with "< Back" at y 8, a separator, then entries from
y 26 in steps of 13.

### 3.4 Test files

`tests/media/` holds six short clips (AAC, M4A, FLAC, MP3, Opus, Vorbis).
**They hold about 40 ms of sound each**, less than one 50 ms audio buffer:
a clip has finished, and handed over to the next track, long before a
screenshot can be taken, and the time never leaves 00:00. A player that
shows "stopped" with the kbps and kHz filled in has played the clip. Use
the long WAV wherever a test needs to see something playing.

Make a scratch music folder outside the repository with:

- copies of those six;
- copies with names in other scripts: `Тихи град.mp3`, `Čaj šećer.flac`,
  `Ελληνικά.ogg`, `日本語.mp3` (the last has no glyphs in the player's
  fonts: it must still play, with placeholder boxes for the title);
- a subfolder with a Cyrillic name holding two more files;
- a `.txt` and a `.jpg`, which must be ignored;
- a long WAV (a minute or more) for the CPU and endurance tests; any tone
  will do, PowerShell or WSL can generate one;
- a broken file: the first 64 bytes of `sfx-vorbis.ogg` saved as
  `broken.ogg`.

## 4. Tests

Each test has an id for the results table. "Shot" means take a screenshot
and look at it. "Ini" means stop the player and read the sandbox's
`amplitude.ini`.

### 4.1 Start and stop

- **S1** Start the 64-bit build with no arguments. Main window appears,
  275x116 at scale 1. Shot. (Since the first run of this plan, a start with
  no saved layout also shows the equaliser and playlist, docked below.)
- **S2** Close with the close button. The process exits within a second;
  `amplitude.ini` now has content; an `amplitude.m3u` exists.
- **S3** The same for the 32-bit build.
- **S4** `amplitude.exe --help` exits at once. (It is a GUI program, so the
  text may not reach the console; note what happens.)
- **S5** Both executables import only COMDLG32, GDI32, KERNEL32, msvcrt,
  SHELL32 and USER32 (`dumpbin /imports`, or `objdump -p` from WSL).
- **S6** Start with no `--scale`: the size is 120% times the display
  scaling (125% display gives 150%). Record display scaling and window size.

### 4.2 File names and Unicode (the original bug report)

- **U1** Start with the Cyrillic-named MP3 as argument. The title shows in
  Cyrillic, not "??????", and the time counts up. Shot.
- **U2** Start with the scratch folder as argument. All audio files are
  added, including those in the Cyrillic subfolder; the `.txt` and `.jpg`
  are not. Stop; the saved playlist (`Get-AmpPlaylist`) lists them with
  correct names.
- **U3** Restart with no arguments: the playlist comes back and every entry
  still plays (double-click a Cyrillic one: `Send-AmpClick` twice on its row
  within 400 ms).
- **U4** The Greek and accented names display correctly; the Japanese one
  plays and shows placeholder boxes. Shot of the playlist.
- **U5** Put the sandbox itself in a folder with a Cyrillic name and repeat
  S1 and S2: settings are saved there.
- **U6** *(needs the user)* Add the same files through "Add files..." and
  "Add folder...", and by dragging them from Explorer onto the player.

### 4.3 Playback and formats

- **P1** For each of the six formats plus a WAV: start with the file; the
  title, kbps and kHz are shown. For the long WAV the time advances between
  two shots a few seconds apart. The six clips are over too soon for that
  (section 3.4): with `repeat=1` in the ini some shots catch them playing.
- **P2** Play, pause, stop, next, previous by click; the display follows.
- **P3** Two short files in the list: the second starts when the first
  ends, with the title changing (gapless hand-over).
- **P4** `broken.ogg`: the title shows "CANNOT PLAY: ..." and the player
  stays alive. This path had a double-close bug that was fixed on Linux.
- **P5** Seek by clicking the seek bar mid-track; the time jumps.
- **P6** *(needs the user, audible)* Sound is clean, with no stutter or
  crackle, including while dragging windows. The audio buffer was raised to
  50 ms recently; listen for any lag when using the equaliser, and watch
  whether the spectrum still moves smoothly.
- **P7** *(needs the user)* With the sound device disabled in Windows, the
  player starts, shows "NO AUDIO DEVICE", and can be closed normally.

### 4.4 Drawing

The Windows drawing code was rewritten to send only changed rows of a
window to the screen and has never run. Look hard here.

- **D1** Shots of main, equaliser and playlist at scale 1, 1.5, 2 and 3.
  Compare with `website/img/player.png` (rendered at 200% on Linux by the
  same drawing code): layout, colours and text must match.
- **D2** While playing, two shots a second apart differ in the time and
  spectrum and nowhere else.
- **D3** Cover the player with another window, uncover it: it repaints
  completely. Minimise and restore: the same. Drag it half off the screen
  and back: the same. Take a shot after each and compare with one from
  before. A stale or black area means the "stale" flag is not being set
  (`WM_PAINT` in `wnd_proc`, `plat_window_present`).
- **D4** Open the equaliser and playlist while nothing is playing. Move a
  slider (click a few points in a slider's column); select playlist rows.
  Each change shows at once. These two windows are redrawn only when their
  model changes, so a change that does not show is a missing field in the
  model or a missing `redraw` flag.
- **D5** Resize the playlist by dragging the grip (posted down, moves, up);
  it redraws at the new size with no garbage at the edges.
- **D6** Change size from the menu (cog, "Size...", 200%): all windows are
  recreated at the new size, in the same places, still docked. Ini:
  `scale_percent=200`.
- **D7** Change colour from the menu ("Color...", Amber): every window
  turns amber at once. Ini: `color=#FFB347`.
- **D8** Text sharpness: at the machine's natural size (S6) and at 2.25
  (`--scale 2.25`), letters are crisp, not doubled pixels. Shot, zoom in.
  This was the third item of the original bug report.

### 4.5 Windows, docking, menus

- **W1** Toggle EQ and PL from the main window. They appear docked below
  the main window; dragging the main window by its title bar moves all
  three together.
- **W2** Right-click the main window: the menu opens at the pointer. Click
  the cog: the same menu opens under it. Click outside: it closes.
- **W3** *(needs the user if there is one monitor)* With two monitors and
  the player on the second, the menu opens on the second monitor. With the
  player near the bottom edge, the menu stays above the taskbar. This was a
  reported bug.
- **W4** Each playlist button opens its menu above the button: ADD, REM,
  SEL, MISC, LIST.
- **W5** Click the logo: the About window opens with version, author and
  the link. Escape closes it. *(User:)* clicking the link opens
  https://amplitude.cr.rs in the browser.
- **W6** Press J (or Ctrl+J) in the main window: jump to file opens. Type
  part of a title with `Send-AmpText`; the list narrows; Enter plays it.
  Known limit: text typed here is limited to the system code page.
- **W7** Minimise button minimises all windows; restoring brings them back.

### 4.6 Playlist commands

With eight or more tracks loaded:

- **L1** SEL: Select all, Select none, Invert selection. Shot after each.
- **L2** REM: Remove selected removes them; Crop keeps only the selection;
  Remove missing files removes an entry whose file was deleted on disk;
  Remove all empties the list and stops playback.
- **L3** MISC: Sort by title, by file name, by folder and file; Reverse;
  Randomize. While a track plays it keeps playing and stays marked.
- **L4** Delete key removes the selection; Ctrl+click and Shift+click
  select as described in `docs/USAGE.md`.
- **L5** *(needs the user for the dialogs)* LIST: Save list writes a
  `.m3u` (the dialog offers "Playlists (*.m3u)"), Open list replaces the
  list with it, New list empties it. Save and reopen a list containing the
  Cyrillic names.

### 4.7 Equaliser and settings

- **E1** ON and AUTO show a small light when on. PRESETS opens the list;
  choosing "Rock" moves the sliders. The "0" label resets them.
- **E2** Stop the player; Ini has `eq_on`, `eq_auto`, the eleven `eq`
  values, `volume`, `balance`, `shuffle`, `repeat`, `vis_mode`, window
  layout. Restart: everything comes back as it was, windows in the same
  places.
- **E3** Shuffle and repeat buttons show their light when on; the same
  entries in the menu are ticked.

### 4.8 Single instance

- **I1** With the test player running, start the same executable again
  with a file: the second process exits at once and the first starts
  playing that file.
- **I2** The same with `--enqueue`: the file is added and playback is not
  interrupted.
- **I3** With the player minimised, a second start restores it.

### 4.9 Skins

- **K1** *(needs a `.wsz` file from the user)* "Skins...", "Load skin...":
  the dialog has a "Skins (*.wsz)" file type; the skin loads. The cog, the
  icon shuffle/repeat buttons and the logo give way to the skin's own
  buttons. "Default skin" switches back.
- **K2** With a classic skin, click each of the small playlist controls
  and the LIST button. Their positions were set from memory of the format;
  note any that are off and by how much.
- **K3** Start with `--skin <file>` and with the `.wsz` as a plain
  argument; both load it.

### 4.10 Installer *(only with the user's agreement; needs elevation)*

For each of `amplitude-<version>-win32-setup.exe` and `-win64-setup.exe`:

- **N1** *(user)* Run it normally once and click through: welcome, licence,
  options (Start menu shortcut ticked, desktop shortcut unticked, "Open
  audio files with Amplitude" ticked), folder, install. Note anything odd
  in the wording or layout.
- **N2** After installing, check:
  - `amplitude.exe`, `LICENSE.txt`, `uninstall.exe` in the install folder
    (`Program Files\Amplitude` for 64-bit, `Program Files (x86)\Amplitude`
    for 32-bit);
  - `Amplitude.lnk` in the all-users Start menu;
  - `HKLM\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Amplitude`
    with DisplayName, DisplayVersion, Publisher, URLInfoAbout;
  - `HKLM\Software\Classes\Amplitude.AudioFile\shell\open\command` and
    `...\shell\enqueue\command` pointing at the installed executable;
  - `HKLM\Software\Classes\.mp3\OpenWithProgids` containing
    `Amplitude.AudioFile`, and the same for `.flac`, `.ogg`, `.m3u`;
  - `HKLM\Software\RegisteredApplications` has an `Amplitude` value.
- **N3** *(user)* Amplitude appears under Settings, Apps, Default apps, and
  in the "Open with" menu of an MP3. Windows 11 does not let an installer
  change the default itself; that is expected.
- **N4** *(user)* The installed player starts from the Start menu, and its
  settings go to `%APPDATA%\Amplitude` (there is no ini beside it).
- **N5** Install the other bitness over it: one entry in "Installed apps",
  not two.
- **N6** Uninstall. Check that the files, the shortcut and every registry
  entry from N2 are gone, that `.mp3` has its previous default class back
  (compare `HKLM\Software\Classes\.mp3` default value with what it was
  before N1; record it before installing), and that `%APPDATA%\Amplitude`
  was left alone.
- **N7** The 64-bit installer on 32-bit Windows shows a message and stops.
  Not testable on Windows 11; mark "not run".

### 4.11 Performance and resources

- **R1** `Measure-AmpCpu 20` while playing the long WAV with all three
  windows open, at scale 1.5 and 3. On Linux the figures were about 1.6%
  and 2.4% of one core. A figure many times higher means the changed-rows
  logic or the sprite cache is not working on Windows.
- **R2** The same with nothing playing: close to zero.
- **R3** `Get-AmpResources` after start, then after 200 repetitions of:
  open and close the menu, toggle EQ and PL, change track. GDI and USER
  object counts and handles must level off, not climb. `GetDC`/`ReleaseDC`
  in `plat_window_present` and the popup window are the places to look if
  they climb.
- **R4** Change size ten times between 100% and 200%, then
  `Get-AmpResources`: memory returns to about where it was.
- **R5** Leave it playing a long list on repeat for 30 minutes; memory and
  handle counts at the end are close to those after the first minute.

### 4.12 Opening several files at once *(added after the first run; not yet run)*

Explorer opens a selection by starting the program once per file. Two
things were added for that: starts arriving within a second of each other
are one request (the first file plays, the rest are added), and copies
started in the same moment agree through a named mutex on which one becomes
the player, the others waiting up to three seconds for its window.

- **M1** With no player running, start five copies at once, each with one
  file (`Start-Process` five times in a row without waiting). Exactly one
  process remains, its list holds all five files, and it is playing one of
  the first to arrive, not the last. Repeat a few times; the order in which
  the five arrive is not guaranteed.
- **M2** With the player running and playing, do the same with three
  files: all three are added, the first of them plays.
- **M3** A single start two seconds after that plays its file.
- **M4** *(needs the installer, 4.10)* Select five MP3s in Explorer and
  press Enter: one player, five tracks, playing.
- **M5** Start a copy while the first is deliberately held up before it
  has a window (hard to arrange; note if not done). The second must not
  hang for more than three seconds.

### 4.13 Media keys *(added later; not yet run)*

The main window registers the play/pause, stop, next and previous keys as
system-wide hot keys, and also answers `WM_APPCOMMAND`.

- **H1** With another program in front, a play/pause key press (real, or
  `keybd_event` with virtual key 0xB3) pauses and resumes; 0xB0 and 0xB1
  change track; 0xB2 stops. Each press acts once, not twice.
- **H2** The same with the player's own window in front.
- **H3** After changing size from the menu (the windows are recreated) the
  keys still work.
- **H4** *(needs the user)* The buttons of a Bluetooth headset do the same.
- **H5** With another player running that also wants the keys, note which
  one gets them; Amplitude must start and run normally either way.

### 4.14 Streams *(added later; not yet run on Windows)*

`src/net_win32.c` is new and has only been compiled: Winsock for plain
addresses, WinINet for secure ones, both loaded when the first stream opens.

- **T1** S5 again: the executables still import only the six system DLLs.
- **T2** Open location (Ctrl+L): the window opens, typing and Backspace
  work, Ctrl+V pastes an address from the clipboard, Escape closes it.
- **T3** A plain station, `http://ice1.somafm.com/groovesalad-128-mp3`:
  "Buffering..." and then sound within a few seconds; the playlist entry
  becomes the station's name; the title display shows the current song;
  the bitrate reads 128; the seek bar has no knob.
- **T4** The same station at `https://`: this is the WinINet path. Also
  `https://somafm.com/groovesalad.pls`, which is a station file on a
  secure site.
- **T5** Pause for ten seconds and resume; Stop and Play (connects again);
  Next to a file and back; close the player while it is still connecting.
  None of these may hang or crash, and the process must exit promptly.
- **T6** An address that does not exist and one with a certificate error
  (`https://expired.badssl.com/`): each ends in "CANNOT PLAY" within some
  seconds, with the player still usable.
- **T6a** AAC stations: `http://ice1.somafm.com/groovesalad-128-aac` and
  the AAC+ one, `http://ice1.somafm.com/groovesalad-64-aac`. Both play
  like T3, with clean sound and the song title shown.
- **T6b** Ogg stations: `https://radio.plaza.one/ogg` (Vorbis) and
  `https://radio.plaza.one/opus`, or `http://icecast.err.ee/vikerraadio.opus`.
  Listen across a change of song: the sound must carry on cleanly and the
  title change.
- **T6c** HLS: `https://stream.radiofrance.fr/fip/fip.m3u8` and
  `https://stream.revma.ihrhls.com/zc185/hls.m3u8`. Each fetches many
  small files in a row, over WinINet when the address is secure; let one
  play for ten minutes. The BBC World Service
  (`http://a.files.bbci.co.uk/ms6/live/3441A116-B12E-4D2F-ACA8-C1984642FA4B/audio/simulcast/hls/nonuk/pc_hd_abr_v2/ak/bbc_world_service.m3u8`)
  uses transport stream segments, and
  `https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel-fmp4.ism/.m3u8`
  MP4 fragments (a film; its sound plays, and ends after twelve minutes).
- **T7** Unplug the network (or disable the adapter) while a station
  plays: it runs out, and the player moves on or stops without hanging.
- **T8** Leave a station playing for an hour; memory and handle counts as
  in R5.
- **T9** *(XP, by hand)* T3 should work there too; T4 probably will not,
  because XP's encryption is too old for most servers. Note what is shown.

### 4.15 Audio CDs *(added later; needs a drive and a disc; not yet run anywhere)*

`src/cd_win32.c` (and `src/cd_linux.c`) have only been compiled: no machine
they were written on had a drive. Everything above them is tested with a
disc image, so what remains to be seen is the drive itself.

- **C1** With an audio CD in the drive, "Play audio CD" in the menu: the
  playlist gains one "CD Track NN" entry per track with the right lengths
  (compare with another player), and track 1 starts within a few seconds.
- **C2** The sound is clean: no clicks, no stutter, right speed and pitch,
  left and right not swapped. The bitrate reads 1411.
- **C3** Seek around within a track and jump between tracks; each takes
  at most a moment. Let one track run into the next: no gap, no click.
- **C4** Pause for a minute (the drive may spin down) and resume.
- **C5** In Explorer, open the disc and drop "Track02.cda" on the player,
  and double-click one if .cda is associated: that track is added.
- **C6** Eject the disc while it plays: sound stops, or silence follows;
  the player must not hang, and Next must still work. Then "Play audio
  CD" with the tray empty: the display says "NO AUDIO CD FOUND".
- **C7** A data CD or DVD in the drive: "NO AUDIO CD FOUND". A mixed disc
  (music plus a data track), if one is at hand: only the music is listed,
  and the last music track ends where it should.
- **C8** Close the player while a CD track plays: the process ends at once.
- **C9** *(Linux)* The same as C1 to C8; the drive is `/dev/sr0`. If
  nothing is found, check that `ls -l /dev/sr0` shows the user may read it.
- **C11** *(Windows 98, by hand)* C1 to C8 there. This is the ASPI path,
  which no other system uses. Note which ASPI is installed (the one that
  came with Windows, or Nero's or Adaptec's) and whether "Play audio CD"
  finds the disc. With two CD drives, check that a track dropped from the
  second drive's Explorer window plays from that drive and not the first.
- **C12** Names: with a well-known album in the drive and the internet
  reachable, the "CD Track NN" entries turn into "Artist - Title" within
  a few seconds. With "Look up CD track names" unticked in the MISC menu
  (and after a restart, still unticked) they stay as they are. With the
  network cable out, nothing hangs.
- **C13** CD-Text: a disc that has it (many albums from the late 1990s
  on, above all Sony's, and discs burned with titles entered) shows its
  names with "Look up CD names online" unticked and the network
  unplugged. Windows XP and later, and Windows 98 through ASPI; not
  expected to work on Windows 2000. A disc without CD-Text, or a drive
  that cannot read it, must simply keep "CD Track NN" with no delay
  worth noticing.
- **C10** A disc image: drop a `.cue` with one `.bin` on the player.

## 5. What only the user can check

Collected from above, for one sitting: U6 (dialogs, drag and drop), P6
(sound quality), P7 (no audio device), W3 (second monitor), W5 (link opens),
L5 (save and open dialogs), K1 (a real skin), N1 to N4 (installer wizard and
Windows settings), and a general impression of sharpness and size at the
machine's natural scaling.

## 6. Results

First run, 4 October 2026, by a Claude Code session on the Windows machine.

Environment: Windows 11 Home 24H2 (10.0.26100), PowerShell 7.6. Two
monitors: the primary 1920x1080 at 150% scaling (144 dpi), and a second one
to its left that a system-DPI-aware program sees as 3072x1728. Commit tested:
310c27d. Executables built here with w64devkit 2.10.0 (GCC 16.2.0, section
3.1 route 3): `build\win32\amplitude.exe` 1,024,000 bytes (19:57) and
`build\win64\amplitude.exe` 1,262,080 bytes (20:06). **These are not the
release executables**, which the Linux cross-compiler builds.

All scripted tests ran on the 64-bit build at `--scale 1` unless noted;
S3, U5 and one P4 run used the 32-bit build. The player's code needed no
change.

| Id | Result (pass / fail / not run) | Notes, screenshot, fix |
|---|---|---|
| helper | pass | Worked on the first try: 275x116, four hidden windows, correct picture. DPI call took effect, posted clicks are accepted, screenshots are right. One real fault (key-up message) and several additions; see below. |
| S1 | pass | 275x116 at scale 1; light blue on dark, "AMPLITUDE" in the title bar. |
| S2 | pass | Close button: process gone after 168 ms, exit code 0; `amplitude.ini` 268 bytes, `amplitude.m3u` present (empty list, 0 bytes). |
| S3 | pass | 32-bit: the same, gone after 134 ms. |
| S4 | pass | Exits in 85 ms with code 0. The usage line does reach PowerShell, both directly and when redirected to a file. |
| S5 | pass | Both: COMDLG32, GDI32, KERNEL32, msvcrt, SHELL32, USER32 and nothing else (`objdump -p` from w64devkit). |
| S6 | pass | Display scaling 150%: window 495x209, which is 180%. `scale_percent=0` stays in the ini. |
| U1 | pass | Title "1. Тихи град (0:00)", 97 kbps, 48 kHz, spectrum and seek bar moving. The time stays 00:00 because the clip is 40 ms long. |
| U2 | pass | 14 entries: everything in the folder and the Cyrillic subfolder, `broken.ogg` included, the `.txt` and `.jpg` left out. The saved list has every name right. |
| U3 | pass | List comes back. A 90 s WAV named `Дуга песма.wav` in a folder `Ћирилица`, double-clicked: title shown, time 00:01 then 00:03. The six clips with Cyrillic, Greek, Latin-with-accents and Japanese names all decode (frame counts logged from a temporary debug build). |
| U4 | pass | "Čaj šećer", "Ελληνικά", "Други", "Први", "Тихи град" drawn correctly; the Japanese name is three placeholder boxes and plays. |
| U5 | pass | Sandbox in `%TEMP%\Амплитуда тест`, 64-bit and 32-bit: starts, closes, ini and m3u written there. `%APPDATA%\Amplitude` (the user's own, from earlier in the day) was not touched. |
| U6 | not run | Needs the user (dialogs, drag and drop). |
| P1 | pass | AAC, AAC in MP4, M4A, FLAC, MP3, Opus, Vorbis: title, kbps and kHz shown, all decoded to the end. WAV: 00:02 then 00:05. A 1411 kbps WAV shows "999". |
| P2 | pass | Pause (time blinks, position kept), resume, stop, play, next, next, previous twice: display follows each. |
| P3 | pass | Two clips as arguments end on "2. Тихи град", `track=1`. In the endurance run (R5) the list moves on from track to track by itself. |
| P4 | pass | "CANNOT PLAY: broken"; pressing play twice more changes nothing; exit code 0. Also 32-bit with broken, good, broken as arguments. See "Still open" for what happens in the middle of a list. |
| P5 | pass | Click mid-bar: 00:45, two seconds later 00:47. Dragging the knob: 1:22. |
| P6 | not run | Needs the user. Nobody was told to listen. |
| P7 | not run | Needs the user. |
| D1 | pass | 100%, 150%, 200%, 300% match `website/img/player.png` in layout, colours and text. At 300% (and at 200% with all three windows) the stack is taller than a 1080p screen; see "Still open". |
| D2 | pass | 144 pixels differ, all inside x 21..98, y 33..81: time, spectrum and seek knob. |
| D3 | pass | Covered by a topmost window and uncovered, minimised and restored, dragged to x = -150 and back: each time 0 pixels differ from the shot before. |
| D4 | pass | Four sliders moved and the curve followed; row selection moved; all shown at once with nothing playing. |
| D5 | pass | 275x232 to 350x290, then to 300x203; clean edges both times; `pl_w=300`, `pl_h=203` saved. |
| D6 | pass | Same process, three windows now 550 wide, same top-left corner, still docked. `scale_percent=200`. |
| D7 | pass | All three windows amber at once. `color=#FFB347`. |
| D8 | pass | 180% (natural) and 225%: every letter is drawn from a larger font at single-pixel sharpness; no doubled pixels. At 225% the "KBPS"/"KHZ" labels are widely spaced. |
| W1 | pass | Equaliser and playlist appear at y+116 and y+232. Dragging the title bar by +200, +50 moved all three by exactly that. |
| W2 | pass | Right click at 150, 50: menu's corner exactly there. Cog: menu under it. A click outside closes it, both times. |
| W3 | pass | Done by script, since the machine has two monitors (layout set in the ini). Player on the second monitor: both menus open there, also at its right edge and its bottom. Player at the bottom of either monitor: the menu is moved up to end exactly at the work area (1008 on the primary), clear of the taskbar. |
| W4 | pass | ADD, REM, SEL, MISC, LIST each open their menu directly above the button row. |
| W5 | pass, link not run | About shows "Version 0.2.0", the author and the link; Escape closes it. Clicking the link needs the user. |
| W6 | pass | J opens the window with an empty box (posted and real key press); "tone" narrows 15 tracks to "15. long-tone"; Enter plays it and closes the window. |
| W7 | pass | Minimise: only the minimised main window is left; restore brings all three back where they were. |
| L1 | pass | All, none, invert (one row left out). |
| L2 | pass | Remove selected 11 to 10; Remove missing 10 to 9 after deleting a file; Crop to rows 2-4 leaves those 3; Remove all while playing: empty list, stopped, title back to "AMPLITUDE". Each count read from the saved list. |
| L3 | pass | Title, file name, folder and file, Reverse, Randomize: order changes each time, the playing track stays marked, its number in the main title follows, the time runs on. |
| L4 | pass | Click row 2, Ctrl+click row 5: both selected. Shift+click row 8: rows 5-8. Delete: 15 to 11 entries. |
| L5 | not run | Needs the user (dialogs). |
| E1 | pass | Lights on ON and AUTO; PRESETS lists 18 entries; Rock moves the sliders and the curve; "0" flattens them. |
| E2 | pass | Ini has all of them (`eq=0,667,400,-467,-667,-267,333,733,933,933,933`, `balance=75`, `vis_mode=1`, layout). After restart: same sliders, lights, balance, windows at 440,300 docked as before. |
| E3 | pass | Lights on both buttons; "Shuffle" and "Repeat" ticked in the menu. |
| I1 | pass | Second process gone in 43 ms; the first plays "2. Дуга песма". |
| I2 | pass | Two files added as 3 and 4; track 2 keeps playing, time runs on. |
| I3 | pass | Minimised, then a second start: restored, file added. One process throughout. |
| K1 | not run | No `.wsz` file on the machine. |
| K2 | not run | The same. |
| K3 | not run | The same. |
| N1 | not run | The user chose to skip the installer for now. |
| N2 | not run | The same. Nothing was installed. |
| N3 | not run | The same. |
| N4 | not run | The same. |
| N5 | not run | The same. |
| N6 | not run | The same. |
| N7 | not run | Not testable on Windows 11. |
| R1 | pass | 1.41% of one core at 150%, 3.75% at 300% (Linux: 1.6% and 2.4%). At 300% part of the stack is below the screen. |
| R2 | pass | 0% at 150%, 0.39% at 300%. |
| R3 | pass | 200 repetitions: GDI 13 and USER 17 throughout; handles 254, 254, 255, 254, 248, 248; private memory 3.8 to 4.3 MB. |
| R4 | pass | Ten changes: private memory 4.3 MB before, 7.2-7.4 MB at 200%, 3.7 MB at the end; GDI 13, USER 17, handles 248 throughout. |
| R5 | pass | Second attempt: 16 tracks on repeat (three 90 s WAVs and 13 clips, no broken file), three windows at 150%, sleep held off. After 1 minute: 6.4 MB private, GDI 13, USER 17, 255 handles. After 5: 7.0 MB, 281 handles. From 10 to 30 minutes: 6.6-6.8 MB, GDI 13, USER 17, 275 handles, unchanged. 32.7 s of processor time in all (1.1% of one core). Still playing at the end, on its seventh pass through the list. |

### Fixes made during testing

The player: none. Nothing in `src/` failed.

The helper, `tests/windows/wintest.ps1`:

- **`Send-AmpKey` typed an extra character.** It posted the key-up message
  with a zero `lParam`; `TranslateMessage` in the player then produced a
  second `WM_CHAR`, so J opened the jump window with "j" already in it. The
  key-up now carries its "was down, released" bits. Checked against a real
  key press, which gives an empty box. W6 re-run.
- **Lost state between commands.** Each tool call is a new PowerShell.
  `Start-Amp` now records the player in a file and `Connect-Amp` picks it up.
- **`-Ctrl`/`-Shift` did nothing**, because the player reads the keys with
  `GetKeyState` and not from the mouse message. They now press the real key
  around the click. L4 depends on this.
- **Double clicks were too slow**: two `Send-AmpClick` calls are about 380 ms
  apart, too close to the player's limit of 400 ms. `Send-AmpDoubleClick` added.
- **`--scale 1.5` could become `1,5`** on a machine with a comma as decimal
  mark; the number is now formatted invariantly. Arguments with spaces are
  quoted.
- Added `Send-AmpDrag`, `Save-AmpShotAll`, `Compare-AmpShots`,
  `Set-AmpScale`, `New-AmpSandbox -Dir` and `-Keep`, `Start-Amp -Scale 0`.

This document: the w64devkit route (3.1), notes on the helper (3.2), the
length of the test clips (3.4, P1).

Two things that looked like bugs and were not:

- After double-clicking playlist rows the title always named the last
  track. The clips are 40 ms long, so each had already handed over to the
  next.
- With `repeat=1` and a single clip, Vorbis and MP4 seemed never to play.
  A debug build showed them looping correctly, every pass shorter than one
  audio buffer, so the display never caught them playing.

### Still open afterwards

Not run, and what each would take:

- **U6, P6, P7, L5, the link in W5**: the user at the keyboard for ten
  minutes with a sandboxed player.
- **K1 to K3**: a real `.wsz` file.
- **N1 to N6**: the user's agreement, `makensis` for Windows (the Makefile's
  own download is a Debian package), and elevation.
- **The release executables.** What ran here was compiled by another, newer
  compiler. Copy `build/dist/` from the Linux machine and repeat at least
  S1, S2, S5, U1, P1 and D1 on those files.

Seen along the way; none is specific to Windows. The first three were fixed
on Linux afterwards and **have not been re-run on Windows**: P4 with a broken
file in the middle of a list, D1 and D6 at 200% and 300%, and P1 with a WAV
are the tests to repeat.

- *Fixed:* when playback moves on by itself, or Next is pressed, files that
  cannot be played are passed over (at most once round the list).
- *Fixed:* the stack of windows is moved up when it reaches below the usable
  screen area, and a window that still would not fit is docked beside the
  main window the first time it opens. The usable area comes from
  `plat_screen_rect`, which on Windows is the monitor's work area.
- *Fixed:* the built-in skin shows four digits of bitrate; classic skins
  show four-digit rates as hundreds ("14H").

As first noted:

- **An unplayable file stops the list.** With `broken.ogg` second in a list
  on repeat, playback ended there with "CANNOT PLAY: broken" and stayed
  stopped (this ended the first attempt at R5 after 90 seconds). Skipping to
  the next track would be kinder; it must not loop for ever when every file
  is bad.
- **Large sizes run off the screen.** New windows open at 100, 100 and the
  docked stack is not kept inside the work area: at 300% on 1080p the
  playlist starts at y = 796 and is 696 high, and at 200% its buttons are
  under the taskbar. The menu is kept on screen (W3); the windows are not.
- **A WAV shows "999 KBPS"** for 1411.
- **Sleep.** The machine slept for 22 minutes during the first R5 attempt,
  with the player stopped. It was alive and responding afterwards. Sleep
  while playing was not tried.
- **Something audible happened without warning**: in E2 the volume slider
  was clicked to 67% while the 441 Hz tone played, for about a second,
  before the player was stopped. Rule 3 was broken there; the test should
  set the volume in the ini instead.
