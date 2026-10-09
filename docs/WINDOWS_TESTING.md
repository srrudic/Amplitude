# Testing Amplitude on Windows 11

This is the plan for the first test of the Windows builds on a real Windows
machine. It is written for whoever runs it, in practice a Claude Code
session started in this repository on the Windows 11 machine, with Srđan at
the keyboard for the few things a script cannot do.

Read sections 1 to 3 first. Then work through section 4 in order, write the
outcome of every test into the table in section 6, and fix what fails.

**For the second run (version 0.2.0)**, read section 0 right below first:
it says what is new and what to do this time.

Background reading: `docs/DEVELOPMENT.md`, especially section 9 ("Rules for
the Windows build") and the part of section 5 about windows and models.

## 0. The second run: what is new in 0.2.0

**Done on 8 October 2026; the results are in section 6 under "Second run".**
One fault was found and fixed (a web address on the command line did not
play). What follows is the brief that run worked from.

The first run (section 6) tested commit 310c27d. Since then the player has
gained a good deal, all of it written and tested on Linux and, as far as
Windows goes, **only compiled**:

| New | Windows-only code that has never run | Tests |
|---|---|---|
| Fixes from the first run: unplayable files are skipped, windows stay on screen, four-digit bitrate | none | repeat P1 (WAV), P4, D1 and D6 at 200% and 300% |
| Several files opened at once from Explorer; one instance | the named mutex and hand-over in `platform_win32.c` | 4.12 |
| Media keys | the media overlay (`smtc.c`), or hot keys in `platform_win32.c` | 4.13 |
| Jump to file window enlarged, with buttons; magnifier button in the playlist; all three windows shown on first start | none | shots of each, as in 4.4 and 4.5 |
| Streams: internet radio (MP3, AAC, Ogg Vorbis, Opus), HLS, files on the web; the Open location window | all of `src/net_win32.c` (Winsock, WinINet, threads); clipboard reading | 4.14 |
| Audio CDs, disc images, CD-Text, names from MusicBrainz | all of `src/cd_win32.c` (device access, and ASPI for Windows 98) | 4.15 |

What to do, in this order:

1. Build both executables (section 3.1) and record their sizes. The About
   window must say "Version 0.2.1".
2. S1, S2 and S5 again, as a check that the basics survived. S5 matters:
   the new code loads its libraries (wsock32, wininet, wnaspi32) only when
   needed, so the list of imported DLLs must still be the same six.
3. The menu has two more entries, so its coordinates in section 3.3 have
   changed; they are up to date there. Take one shot of the menu first.
4. The repeats named in the first row of the table.
5. Sections 4.12, 4.13, then 4.14 and 4.15. In the last two, do the parts
   marked *scripted* first: they need no internet, no drive and no ears,
   and they are where a crash or a hang in the new Windows code will show.
6. R1 to R5 again with a stream playing and with a disc image playing.
7. Write a "Second run" part into section 6 in the same form as the first.

Two more rules for this run, in addition to section 2:

- **The player now talks to the internet by itself** when a CD or a disc
  image is added: it asks MusicBrainz for the names. Put `cd_names=0` into
  the sandbox's ini for every test except C12 and C14, which are about
  exactly that.
- **Streams and discs make real sound.** The test station plays a short
  beep over and over. `volume=0` in the ini, always (rule 3).

The Linux side has no way to run any of this, so a failure here is as
likely in the new Windows files as it was in `platform_win32.c` the first
time. The portable parts (decoders, the HTTP and HLS logic, CD-Text and
name parsing) have unit tests on Linux (`tests/test_stream.c`,
`tests/test_cd.c`); if something fails in a way that looks portable, say
so, and it can be reproduced there.

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
3. **w64devkit** (used for both runs of this plan; needs nothing
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

Look at the PNG with the Read tool. Expected: a visible window titled
"Amplitude" of 275x116 with the equaliser and the playlist visible below it
(since 0.2.0 a first start shows all three), three hidden ones, and a
screenshot showing the main window in light blue on dark with "AMPLITUDE"
in the title bar.

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
- **Ctrl with a key**: `Send-AmpKey "Amplitude" 0x4C -Ctrl` is Ctrl+L, and
  0x56 in the Open location window is Ctrl+V. Like `-Ctrl` on a click it
  needs the window in front.
- **Getting a window in front** is not always allowed: while the
  notification panel was open, Windows refused every polite request.
  `Set-AmpForeground "Amplitude"` says whether it worked; with `-RealClick`
  it falls back to one click of the real mouse on the title text.
- **Media keys**: `Test-AmpMediaKeys` says whether the four keys are held
  as hot keys (check that it is false before the player starts and true
  after). `Send-AmpMediaKey 0xB3` presses the real key and refuses when
  nobody holds it, so that it cannot start some other player. Where the
  player is in the media overlay instead (4.13), `Get-AmpOverlay` lists
  what the overlay shows, `Send-AmpOverlay pause` presses one of its
  buttons for the player alone, and `Send-AmpMediaKey` presses the real key
  only while the player is the overlay's one and only entry.
- **The menu needs a moment.** Right after the click on the cog there is no
  menu window yet; with `-Wait` under about 150 ms the next call does not
  find it.

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
y 26 in steps of 13. "Size...": Automatic 26, 100% 39, 125% 52, 150% 65,
175% 78, 200% 91, 250% 104, 300% 117, 400% 130.

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
- **W8** *(added for the second run; needs the user or real input)* Open
  the right-click menu, then click on another program's window or on the
  desktop: the menu closes. Before, it stayed open on top of everything.
  (Posted messages cannot show this: the menu closes when Windows tells
  the player that another program was activated.)
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

### 4.12 Opening several files at once *(added after the first run)*

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

### 4.13 Media keys *(added later)*

On Windows 8.1 and later the player joins the media overlay (`src/smtc.c`)
and gets the keys from it; H6 to H9 are about that. Elsewhere the main
window registers the play/pause, stop, next and previous keys as
system-wide hot keys. Either way it also answers `WM_APPCOMMAND`.

- **H1** With another program in front, a play/pause key press (real, or
  `keybd_event` with virtual key 0xB3) pauses and resumes; 0xB0 and 0xB1
  change track; 0xB2 stops. Each press acts once, not twice.
- **H2** The same with the player's own window in front.
- **H3** After changing size from the menu (the windows are recreated) the
  keys still work.
- **H4** *(needs the user)* The buttons of a Bluetooth headset do the same.
- **H5** With another player running that also wants the keys, note which
  one gets them; Amplitude must start and run normally either way.
- **H6** *(overlay)* With a track playing, `Get-AmpOverlay` lists
  `amplitude.exe` as playing music, with the track's title (try one with
  letters outside the code page) and nothing held as hot keys. The entry is
  gone once the player has exited.
- **H7** *(overlay)* `Send-AmpOverlay` with `pause`, `play`, `toggle`,
  `next`, `prev` and `stop`: the player does each, once, and the overlay
  shows the new state and title afterwards.
- **H8** *(overlay)* H3 again: after a change of size the entry is still
  there, with the state and title, and its buttons still work.
- **H9** *(needs the user)* Change the volume with the keyboard while a
  track plays: the overlay beside the volume shows the title and its
  buttons work. The same in the taskbar's quick settings and on the lock
  screen.

### 4.14 Streams *(added later)*

`src/net_win32.c` is new: Winsock for plain addresses, WinINet for secure
ones, both loaded when the first stream opens.

**Scripted, with the local test station.** `tests/stream_server.py` is a
small web server with a station of every kind, on 127.0.0.1 only. It needs
Python 3 on Windows (`py -3 --version`). If there is none, nothing has to
be installed: the "Windows embeddable package" from python.org is a zip,
and the script needs only what is in it; the second run unpacked it into
`.toolchain\python` and used `.toolchain\python\python.exe` in place of
`py -3`. Start it in a window of its own, to live an hour:

    $env:STREAM_SERVER_SECONDS = 3600
    py -3 tests\stream_server.py 18700 tests\media\sfx.mp3 tests\media

Its addresses (all `http://127.0.0.1:18700` plus): `/radio` (MP3 with
titles, "Test Radio", 128 kbps), `/old`, `/redirect`, `/station.pls` (three
other ways to the same), `/radio.aac`, `/radio.ogg`, `/radio.opus`,
`/hls/aac.m3u8`, `/hls/ts.m3u8`, `/hls/mp4.m3u8`, `/hls/video.m3u8` (live
HLS of four kinds), `/hls/vod.m3u8` (HLS that ends after three short
segments), `/file.mp3` (a file), `/nothing` (404) and `/hls/key.m3u8`
(encrypted, refused). The top of the script describes each.

- **T0a** Start the player with `/radio` as its argument (`Start-Amp
  -Arguments`). Within a few seconds two shots a second apart show the
  time advancing, "128" kbps, and the title display showing "Artist -
  First Song" or the second title followed by "(Test Radio)"; the playlist
  entry reads "Test Radio". Stop the player: it must exit at once, and
  the saved playlist holds the address.
- **T0b** The same for each of the other playing addresses, one player
  start each: time advances, nothing crashes. `/radio.ogg` and
  `/radio.opus` are named "Test Radio Ogg"; the HLS ones have no name.
- **T0c** One player with all the addresses as arguments, `/nothing` and
  `/hls/key.m3u8` among them: press Next (`Send-AmpKey "Amplitude" 0x42`,
  the B key) every two seconds, twenty times. The bad ones show "CANNOT
  PLAY" and are passed over; the player never hangs; `Get-AmpResources`
  before and after shows no steady climb in handles; the process exits
  promptly when stopped, even right after a Next.
- **T0d** `/hls/vod.m3u8` first and a file second: when the stream ends
  the file starts by itself.
- **T0e** Stop the server (Ctrl+C in its window) while `/radio` plays:
  the player plays out its buffer and stops or moves on; no hang.
- **T0f** Open location by script: `Send-AmpClick "" 30 34` in the menu
  opens the window titled "Open location" (275x76); `Send-AmpText "Open
  location" "127.0.0.1:18700/radio"` types an address without "http://";
  Enter (`Send-AmpKey "Open location" 0x0D`) closes the window, adds
  `http://127.0.0.1:18700/radio` to the playlist and plays it. Escape
  closes the window without adding anything. (Ctrl+L and Ctrl+V need the
  real Ctrl key: `Send-AmpKey -Ctrl`, see T2.)

**With the internet** (muted; a shot shows whether it plays):

- **T1** S5 again: the executables still import only the six system DLLs.
- **T2** Open location (Ctrl+L): the window opens, typing and Backspace
  work, Ctrl+V pastes an address from the clipboard, Escape closes it.
  By script: `Send-AmpKey "Amplitude" 0x4C -Ctrl`, then, with the user's
  clipboard text saved and put back afterwards, `Set-Clipboard` and
  `Send-AmpKey "Open location" 0x56 -Ctrl`.
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

### 4.15 Audio CDs *(added later; needs a drive and a disc; C1 to C9 and C11 to C13 not yet run anywhere)*

`src/cd_win32.c` (and `src/cd_linux.c`) have only been compiled: no machine
they were written on had a drive, and neither had the Windows 11 machine of
the second run. Everything above them is tested with a disc image (C10 and
C14, which have been run), so what remains to be seen is the drive itself.

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
- **C10** *(scripted, no drive needed)* A disc image. Make one of two
  tracks of silence, with names in it:

      $d = "$env:TEMP\amp-cd"; New-Item -ItemType Directory -Force $d | Out-Null
      fsutil file createnew "$d\disc.bin" (2352 * 75 * 10)
      Set-Content "$d\disc.cue" -Encoding ascii @'
      PERFORMER "Band"
      FILE "disc.bin" BINARY
       TRACK 01 AUDIO
        TITLE "Named on the Disc"
        INDEX 01 00:00:00
       TRACK 02 AUDIO
        INDEX 01 00:05:00
      '@

  Start the player with the `.cue` as its argument and `cd_names=0`. The
  playlist shows "Band - Named on the Disc" (0:05) and "CD Track 02"; the
  time advances; "1411" kbps; track 2 follows track 1 by itself; seeking
  works. The saved playlist holds `cdda://<path of the .cue>/1` and `/2`.
  This runs everything except the drive: the reader thread, the decoder,
  and reading names from the disc.
- **C14** *(scripted, needs the internet)* Names from MusicBrainz for an
  image laid out like a real album. With `cd_names=1`:

      fsutil file createnew "$d\real.bin" (2352 * 95312)
      Set-Content "$d\real.cue" -Encoding ascii @'
      FILE "real.bin" BINARY
       TRACK 01 AUDIO
        INDEX 01 00:00:00
       TRACK 02 AUDIO
        INDEX 01 03:22:63
       TRACK 03 AUDIO
        INDEX 01 07:08:64
       TRACK 04 AUDIO
        INDEX 01 10:19:17
       TRACK 05 AUDIO
        INDEX 01 14:03:39
       TRACK 06 AUDIO
        INDEX 01 17:51:14
      '@

  Within some seconds of starting the player on it, the first entry
  changes from "CD Track 01" to "Ettella Diamant - Rysperdal Gstp" (it did
  on Linux). This is a secure request through WinINet followed by the
  portable parsing. With `cd_names=0` the entries stay as they are.

## 5. What only the user can check

Left over from the second run: a real media key on the keyboard and a
headset's buttons (H4); listening to one station of each kind (T3, T6a,
T6b, T6c) and across a change of song on an Ogg station; T7 (network
unplugged); and all of 4.15 except C10 and C14, which needs an audio CD
in a drive (C1 to C9, C12, C13), and a Windows 98 machine for C11.

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

### Second run, 8 October 2026

By a Claude Code session on the same machine, for version 0.2.0, following
section 0.

Environment: Windows 11 Home 24H2 (10.0.26100), PowerShell 7.6.6, the same
two monitors (primary 1920x1080 at 150%). No WSL, no Python, no CD drive.
Commit tested: 850fba7, and from T0a on with the fix described below.
Executables built here with w64devkit 2.10.0 (GCC 16.2.0, section 3.1
route 3), with no warnings in our own sources: `build\win32\amplitude.exe`
1,065,472 bytes and `build\win64\amplitude.exe` 1,307,648 bytes. **These
are again not the release executables.** The test station ran on the
embeddable Python 3.13.7 unpacked into `.toolchain\python`.

All scripted tests ran on the 64-bit build at `--scale 1` unless noted,
with `volume=0` and, except in C14, `cd_names=0`. Nothing was audible. The
last row is a shorter pass with the 32-bit build.

| Id | Result (pass / fail / not run) | Notes, fix |
|---|---|---|
| helper | pass | The check of section 3.2 worked as it was. Three windows are visible now (main, equaliser, playlist) and three hidden (Jump to file, About, Open location). |
| S1 | pass | 275x116 at 100, 100, equaliser and playlist docked below. |
| S2 | pass | Close button: gone after 134 ms, exit code 0; ini 284 bytes, empty m3u. |
| S5 | pass | Both builds: COMDLG32, GDI32, KERNEL32, msvcrt, SHELL32, USER32 and nothing else. wsock32 and wininet show among the loaded modules only after a stream of that kind was opened; wnaspi32 never (it is asked for on Windows 9x only). |
| About | pass | "Version 0.2.0". |
| menu | pass | 172x206, the fourteen entries at the coordinates of section 3.3. |
| P1 (WAV) | pass | "1411 KBPS". |
| P4 | pass | Good, broken, good on repeat: Next goes from 1 to 3 and from 3 to 1; the broken one is never stopped at. Two broken files and nothing else: "CANNOT PLAY: broken2", stopped, 0% processor. |
| D1 | pass | Fresh start at 200%: the stack is moved up to y = 80 and ends exactly at the work area (1008). At 300%: main and equaliser stacked, the playlist docked to the right of the main window; everything on screen. |
| D6 | pass at 200%; see note at 300% | 100% to 200% from the menu: same process, moved up to y = 80, still docked. 200% to 300% from the menu: the stack is moved up to y = 0, but the playlist stays under the others and reaches from 696 to 1392, 384 pixels below the screen. See "Still open". |
| jump, magnifier | pass | J and the magnifier button both open Jump to file (275x232, with JUMP, ENQUEUE and CLOSE); "tone" narrows fifteen tracks to one; Enter plays it. |
| M1 | pass | Five copies started at once, four times: one process left each time (always the first started), the other four ended with code 0, all five files in the list, the first in the list playing. The player's own file comes third to fifth in the list: the files handed over arrive before it adds its own. |
| M2 | pass | Three more into a playing player: all added (m6, m7, m8), m6 playing. |
| M3 | pass | One more three seconds later: added and playing. |
| M4 | not run | Needs the installer. |
| M5 | not run | No way was found to hold the first copy up before it has a window. |
| H1 | pass | The four keys were free before the player started and held while it ran (`RegisterHotKey` from the test fails with 1409). With another program in front: next 1 to 2 to 3, previous to 2, pause, resume, stop. Each press acted once. Pressed with `keybd_event`. |
| H2 | pass | The same with the player in front: once each. Getting it in front took a real mouse click (section 3.2). |
| H3 | pass | After 100% to 125% to 100% from the menu the keys are still held and work. After ten more changes in R4 too. Freed when the player exits. |
| H4 | not run | Needs the user and a headset. |
| H5 | not run | No other program held the keys. |
| T0a | **fail, fixed, pass** | See "Fixes". After the fix: the time advances, "128 KBPS 48 KHZ", "Artist - Second Song (Test Radio)" in the title, "Test Radio" in the playlist, no knob on the seek bar; exit in 209 ms; the saved list holds the address. |
| T0b | pass | `/old`, `/redirect`, `/station.pls`, `/radio.aac` (64 kbps, "Test Radio AAC"), `/radio.ogg` and `/radio.opus` ("Test Radio Ogg"), the four live HLS ones: all playing after 6 s, each exit under 210 ms. `/hls/vod.m3u8` and `/file.mp3` had played out and stopped by then. The Ogg stations and `/hls/video.m3u8` show "0 KBPS". |
| T0c | pass, with one difference | Fifteen addresses on repeat, Next twenty times: never hung, never died; handles 293 to 294, threads 9 to 11, private memory 5.2 to 6.2 MB; exit 159 ms right after a Next. The difference: Next onto `/nothing` or `/hls/key.m3u8` shows "CANNOT PLAY" and **stays there**; it is not passed over as a broken file is. See "Still open". |
| T0d | pass | When `/hls/vod.m3u8` ends the WAV after it starts by itself. |
| T0e | pass | Server killed while `/radio` played: the player went on for about 23 s on what it had, then moved to the next track. Responding throughout. |
| T0f | pass | "Open location" 275x76; typing and Backspace work; Escape closes it and adds nothing; Enter closes it, adds `http://127.0.0.1:18700/radio` and plays. |
| T1 | pass | As S5. |
| T2 | pass | By script: Ctrl+L opens the window; Ctrl+V pastes `http://127.0.0.1:18700/radio.aac` from a clipboard text with spaces and a line end around it; Enter plays it. The user's clipboard text was put back. |
| T3 | pass, not listened to | `http://ice1.somafm.com/groovesalad-128-mp3`: playing within 7 s, 128 kbps, 44 kHz, song and station in the title. Winsock. |
| T4 | pass | The same at `https://` and `https://somafm.com/groovesalad.pls`: playing, through WinINet. |
| T5 | pass | On the http and the https station: pause 10 s (time blinks) and resume; Stop and Play (starts again from 00:00); Next to a file and Previous back. Closed 1.5 s into a connection to an address that never answers (`http://` and `https://10.255.255.1`): exit in 191 and 185 ms. Next three times through such addresses to a file: plays the file, exit 205 ms. |
| T6 | pass | Unknown host over http and https, `https://expired.badssl.com/`, a refused connection, a 404 on a secure site: "CANNOT PLAY" within 2 s each; Next then plays the file after it. |
| T6a | pass, not listened to | AAC at 128 and AAC+ at 64 kbps: playing, song title shown. |
| T6b | pass, not listened to | `https://radio.plaza.one/ogg` and `/opus`, `http://icecast.err.ee/vikerraadio.opus`: playing, titles shown. No change of song was waited for. |
| T6c | pass | fip (280 kbps), iHeart (24), BBC World Service (transport stream, 101) and the MP4-fragment film sound: each playing after 9 s, 12 s further on 12 s later. fip for twenty minutes: see R5. |
| T7 | not run | Needs the cable out. T0e is the same thing with the local station. |
| T8 | pass | Run afterwards, in the night of 8 October, at 150%, with the handles counted by kind every five minutes. **fip over https for an hour**: 482 handles after a minute, 556 after 30, and 552 to 556 for the 30 minutes after that; private memory 10.1 to 11.2 MB; GDI 13, USER 18; 102 s of processor time (2.8% of one core); exit in 236 ms. **Four 20 s WAVs on repeat for an hour** (a change of track every 20 s): 223 handles for 45 minutes on end, then 252, 282, 307 and 333 in the last fifteen; private memory 6.6 to 7.0 MB. **Nothing playing for 20 minutes**: 217 throughout. Every step up, in both hours, falls within a minute of the screens going to sleep or waking. See "Still open". |
| T9 | not run | Windows XP. |
| C1-C9, C12, C13 | not run | No CD drive on this machine. One part of C6 could be run: "Play audio CD" with no drive at all says "NO AUDIO CD FOUND" and the player carries on. |
| C10 | pass | "Band - Named on the Disc" (0:05) and "CD Track 02"; 1411 kbps; track 2 follows by itself; the list is saved as `cdda://C:\...\disc.cue/1` and `/2`. Seeking was checked on the longer image of C14: a click mid-bar goes from 00:05 to 01:43, and a double click on row 5 plays track 5. |
| C11 | not run | Windows 98. |
| C14 | pass | `cd_names=1`: all six entries named within 8 s, the first "Ettella Diamant - Rysperdal Gstp", as on Linux; wininet loaded. `cd_names=0`: "CD Track 01" and so on, and neither network library loaded. |
| R1 | pass | Of one core, at 150% and at 300%: local MP3 station 2.1% and 4.5%; fip over https 4.4% and 6.6%; disc image 1.0% and 3.8%. |
| R2 | not run | Nothing new to measure. |
| R3 | pass | 200 rounds over thirteen tracks (local stations, an https station, both disc images): GDI 13 and USER 18 throughout. Handles 293 at the start and 476 to 484 from the first https track on, which is what WinINet holds once loaded; level after that. Private memory 6.8 to 8.2 MB. |
| R4 | pass | Ten changes between 100% and 200% with an https station playing: private memory 6.5 MB before, 12.2 at 200%, 7.9 after; handles 478 throughout. With a disc image: 6.1, 11.8, 6.4 MB; handles 254. Still playing afterwards both times. |
| R5 | pass | At 150%, three windows, sleep held off. **fip over https for 20 minutes**: playing at the end (20:08 on the clock), 35 s of processor time (2.9% of one core), private memory 10.2 MB after a minute and 11.2 at the end, GDI 13, USER 18 or 19; exit in 187 ms. **A two-track disc image and two 20 s WAVs on repeat for 12 minutes** (a track change every 5 to 20 s): 13 s of processor time, private memory 8.5 to 8.8 MB, GDI 13, USER 18. The handle count went up in steps of about 30 in both (470 to 628, and 254 to 305); T8 found out why. |
| 32-bit | pass | No file, a Cyrillic WAV, `/radio`, `/radio.aac`, `/radio.opus`, `/hls/mp4.m3u8`, the https station, fip, the expired certificate, the disc image, the names of C14, and M1 once: all as with the 64-bit build. |

#### Fixes made during the second run

**The player: a web address on the command line did not play (T0a).**
`amplitude.exe http://127.0.0.1:18700/radio` showed "CANNOT PLAY: radio",
and the saved list read
`C:\Users\...\Amplitude\http:\127.0.0.1:18700\radio`. `main()` makes every
argument absolute with `plat_absolute_path` and keeps the argument as it is
when that fails. On Linux `realpath` fails for a file that does not exist,
so an address came through untouched; on Windows `GetFullPathName` never
fails for that reason and turns the address into a path under the current
directory. The same would have happened to a `cdda://` argument, and to an
address given to a second copy for the running player. `main()` now
leaves addresses and CD tracks alone, as `add_path` already did. This is
the only change in `src/`; every stream test from T0a on ran with it.

The helper, `tests/windows/wintest.ps1`:

- `Send-AmpKey -Ctrl`, for Ctrl+L and Ctrl+V.
- `Set-AmpForeground`: Windows refused `SetForegroundWindow` for as long as
  the notification panel was open on the desktop, so H2 and T2 could not
  get the player in front until a real click was used.
- `Test-AmpMediaKeys` and `Send-AmpMediaKey`, so that a media key is only
  pressed when it is certain to be the player that takes it.

This document: the Size menu's coordinates (3.3), the new helper functions
(3.2), Python without installing it (4.14), T2 by script.

One thing that looked like a fault of the Windows code and was not: in R3
the menu failed to open about once in seven rounds. The main thread turned
out to be busy for 1.2 to 2.2 s whenever the track changed **to** the test
station's `/hls/ts.m3u8`. That station sends 40 ms of MP3 a second, and the
MP3 decoder, set up on the main thread, waits for more. Every other change
of track takes about 0.1 s, and real HLS stations (fip, BBC, the MP4 one)
start with no such wait. Portable code, and a starved stream that only the
test station produces.

#### Still open after the second run

Not run, and what each would take:

- **All of 4.15 on a real drive** (C1 to C9, C12, C13): a machine with a CD
  drive and a disc. `cd_win32.c` was read through for this run (the control
  codes, the layout of the table of contents, the raw read request and its
  fifteen sectors at a time) and nothing wrong was found, but apart from
  "no drive found" it has still never run.
- **Listening** (T3, T6a, T6b, T6c, a change of song on an Ogg station),
  **T7**, **H4**: the user.
- **M4, and the installer as a whole** (4.10); **K1 to K3**; **U6, P6, P7,
  L5** from the first run: as before.
- **The release executables**: as before, what ran was built here.
- **A few handles are left behind each time the sound devices change.**
  This is what the rising count in R5 was, and T8 settled it. The count
  does not rise with time, nor with segments fetched or tracks changed: it
  steps up by 25 to 30 at the moments the Windows audio log
  (Microsoft-Windows-Audio, Operational) records "Audio device state
  changed", which on this machine is every time the screens go to sleep or
  wake, since they carry a sound output. Between such moments it is level
  for as long as was watched (45 minutes). What stays behind each time is
  a thread pool with its timers and completion ports, an ALPC port, some
  events and two sections; no threads, no files, no memory to speak of.
  Neither network library nor the CD code is involved (it happens with
  plain files). Not found out: whether it is miniaudio's switching to the
  new device that leaves them or Windows' own sound components inside the
  process, and whether a player that is not playing does it too (no such
  moment fell into the 20 idle minutes). At a hundred screen sleeps a day
  it is under 3000 handles, far from any limit: a thing to know, not a
  thing to fix before a release.

A first attempt at R5 ended when the player's window was closed by hand
after 17 minutes; its figures were the same as the second attempt's, minute
for minute.

Seen along the way. None of it is specific to Windows; all three are in
portable code and will be the same on Linux:

- **A stream that cannot be played stops the list** (T0c). Broken files
  are passed over since the first run, in `load_track`'s callers; a stream
  fails later, in `stream_news`, which only shows the message. Either pass
  it over there as well or change what T0c expects.
- **Changing to 300% while running puts the playlist below the screen**
  (D6). A window is docked beside the main one only the first time it is
  shown; when the size changes the old layout is kept and merely moved up.
- **"0 KBPS"** for Ogg stations and for HLS without a stated bandwidth
  (T0b); blank would look better.

All three were fixed on Linux afterwards and **have not been re-run on
Windows**; T0b, T0c and D6 at 300% are the tests to repeat.

- *Fixed:* a stream that fails is passed over when the list reached it by
  itself or by Next (at most once round the list), and left showing
  "CANNOT PLAY" when it was picked by hand, as with files.
- *Fixed:* after a change of size the playlist is docked beside the main
  window if the stack no longer fits on the screen.
- *Fixed:* a stream that states no bitrate shows a measured one, from
  about eight seconds in (and corrected once after a minute). Until then
  the field still reads 0.

**Changed on Linux after the media overlay was added, and not yet run on
Windows:** every library that is loaded while the player runs (wsock32 and
wininet for streams, combase and ole32 for the overlay, wnaspi32 for CDs on
Windows 98, and the fallback in `optional()`) is now asked for by its full
path in the system directory (`src/win32_library.h`) instead of by name.
By name, a library the system lacks, such as combase.dll before Windows 8,
would be looked for in the current directory too, which is the folder of
the file the player was started with. The tests that show the libraries
are still found: T0a (a plain stream), T4 (a secure one), H6 and H7 (the
overlay), and C11 on Windows 98. S5 as always.

Also not yet run: when nothing is loaded any more (the playlist emptied
with "New list" while a track's title was in the overlay), the overlay's
title is now cleared and the change applied, where before it was cleared
without the call that makes it show. `Get-AmpOverlay` after that should
list no title for the player.