#!/bin/sh
# End-to-end test of the real player: starts it with scratch settings,
# drives it with synthetic input (tests/drive.c) and checks its windows and
# the settings it saves on exit.
#
#   tests/gui_test.sh <amplitude> <drive> <mkwav>      (or: make test-gui)
#
# Needs an X display. The test player uses its own settings directory, which
# also makes it a separate instance from any player already running, and the
# driver only touches windows of the process started here. It plays digital
# silence, so nothing is heard.
set -u
PLAYER=$(realpath "$1"); DRIVE=$(realpath "$2"); MKWAV=$(realpath "$3")

WORK=$(mktemp -d /tmp/amplitude-gui-XXXXXX)
export XDG_CONFIG_HOME="$WORK/config"
INI="$XDG_CONFIG_HOME/amplitude/amplitude.ini"
M3U="$XDG_CONFIG_HOME/amplitude/amplitude.m3u"
failed=0
DRIVE_PID=

cleanup() {
    [ -n "$DRIVE_PID" ] && kill "$DRIVE_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

# ok <description> <command...>: runs the command and reports
ok() {
    what=$1; shift
    if "$@" >/dev/null 2>&1; then echo "ok    $what"; else echo "FAIL  $what"; failed=1; fi
}
setting() { grep -qx "$1" "$INI"; }
tracks() { [ "$(wc -l < "$M3U")" -eq "$1" ]; }

start() {       # start <player arguments...>
    "$PLAYER" "$@" 2>"$WORK/stderr" &
    DRIVE_PID=$!
    export DRIVE_PID
    sleep 1.2
}
quit() {        # clicks the close button (main window at 100%: 268,7) and waits
    "$DRIVE" click 0 268 7 0 >/dev/null
    wait "$DRIVE_PID" 2>/dev/null
    DRIVE_PID=
}
drive() { "$DRIVE" "$@" || failed=1; }

# A small library: two albums, a nested folder, a Unicode name, non-audio files.
mkdir -p "$WORK/music/Album A" "$WORK/music/Album B/CD2" "$WORK/more" "$WORK/bin"
"$MKWAV" "$WORK/music/Album A/01 alpha.wav" 30 440 0
"$MKWAV" "$WORK/music/Album A/02 Šećer.wav" 30 440 0
"$MKWAV" "$WORK/music/Album B/01 gamma.wav" 30 440 0
"$MKWAV" "$WORK/music/Album B/CD2/01 delta.wav" 30 440 0
"$MKWAV" "$WORK/more/extra one.wav" 30 440 0
"$MKWAV" "$WORK/more/extra two.wav" 30 440 0
echo notes > "$WORK/music/Album A/notes.txt"
echo cover > "$WORK/music/cover.jpg"

# Stands in for the desktop's file dialogs with fixed answers.
cat > "$WORK/bin/zenity" <<FAKE
#!/bin/sh
case "\$*" in *--directory*) echo "$WORK/more" ;; *) echo "$WORK/more/extra one.wav" ;; esac
FAKE
chmod +x "$WORK/bin/zenity"
PATH="$WORK/bin:$PATH"

echo "--- start with a folder: recursive add, layout, docking"
start --scale 1 "$WORK/music"
drive size 0 275 116  hidden 1  hidden 2  hidden 3
drive key 0 g 0  size 1 275 116  below 0 1
drive key 0 e 0  size 2 275 232  below 1 2
drive mark 0  mark 2  drag 0 100 5 60 40  moved 0 60 40  moved 2 60 40  below 0 1  below 1 2

echo "--- playlist: resize, selection, removal"
drive drag 2 268 225 50 58  size 2 325 290
drive click 2 100 37 0  click 2 100 47 1  key 2 Delete 0         # rows 2 and 3 (shift-click), delete

echo "--- menus: right click, outside click closes, ADD > folder"
drive rclick 0 50 50 500 300  popup yes  menu 400 5  popup no
drive click 0 225 97 0  popup yes  menu 400 5  popup no           # the cog opens the same menu

echo "--- about: a click on the logo opens it, Escape closes it"
drive hidden 4  click 0 255 96 0  size 4 250 138  key 4 Escape 0  hidden 4
drive click 2 24 268 0  popup yes  menu 30 21  wait 600  popup no
# REM, SEL, MISC and LIST each open a menu; a click outside closes it again
drive click 2 53 268 0  popup yes  menu 400 5  popup no
drive click 2 82 268 0  popup yes  menu 400 5  popup no
drive click 2 111 268 0  popup yes  menu 400 5  popup no
drive click 2 289 268 0  popup yes  menu 400 5  popup no

echo "--- equaliser: preset, then reset with the 0 label"
drive click 1 230 24 0  popup yes  menu 30 177  click 1 50 68 0

echo "--- drag and drop onto the playlist, jump to file"
drive drop 2 "file://$WORK/music/Album%20A/01%20alpha.wav"
drive key 0 j 4  size 3 275 148  key 3 d 0  key 3 e 0  key 3 l 0  key 3 Return 0  hidden 3
drive key 0 v 0

echo "--- a second start hands its file to the running player"
"$PLAYER" --enqueue "$WORK/more/extra two.wav"
ok "the second instance exited at once" test $? -eq 0
sleep 0.5
quit

echo "--- what was saved"
# 4 from the folder - 2 deleted + 2 from ADD folder + 1 dropped + 1 enqueued
ok "playlist has 6 tracks" tracks 6
ok "nested folders were searched" grep -q "CD2/01 delta.wav" "$M3U"
ok "non-audio files were skipped" sh -c "! grep -q -e notes.txt -e cover.jpg '$M3U'"
ok "jump to file chose 'delta'" setting "track=1"
ok "playlist size saved" sh -c "grep -qx pl_w=325 '$INI' && grep -qx pl_h=290 '$INI'"
ok "equaliser was reset to flat" setting "eq=0,0,0,0,0,0,0,0,0,0,0"
ok "window layout saved" sh -c "grep -qx eq_visible=1 '$INI' && grep -qx pl_visible=1 '$INI' && grep -qx eq_y=116 '$INI'"
ok "nothing on stderr" test ! -s "$WORK/stderr"

echo "--- restart: settings, playlist and layout come back"
start
drive size 0 275 116  size 1 275 116  size 2 325 290  below 0 1  below 1 2
quit
ok "playlist kept across the restart" tracks 6

echo "--- colour change from the menu"
start
drive rclick 0 50 50 500 300  menu 30 127  popup yes  menu 30 52  popup no     # Color... > Amber
quit
ok "colour saved" setting "color=#FFB347"

echo "--- size change while running"
start
drive rclick 0 50 50 500 300  menu 30 113  popup yes  menu 30 91  wait 500     # Size... > 200%
drive size 0 550 232  size 1 550 232  below 0 1
"$DRIVE" click 0 536 14 0 >/dev/null; wait "$DRIVE_PID" 2>/dev/null; DRIVE_PID=
ok "size saved" setting "scale_percent=200"

echo "--- a file that cannot be played is passed over"
# Fresh settings; three files of which the middle one is recognised as Ogg
# Vorbis but is broken. Playback must reach the third and end there.
export XDG_CONFIG_HOME="$WORK/config-skip"
INI="$XDG_CONFIG_HOME/amplitude/amplitude.ini"
mkdir -p "$WORK/skip"
"$MKWAV" "$WORK/skip/1 first.wav" 1 440 0
printf 'OggS\000\002\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\001\036\001vorbis and no more than this, sadly' > "$WORK/skip/2 broken.ogg"
"$MKWAV" "$WORK/skip/3 third.wav" 1 440 0
start --scale 1 "$WORK/skip"
sleep 3.5
quit
ok "playback moved past the broken file to the last track" setting "track=2"
ok "all three are still in the list" sh -c "[ \$(wc -l < '$XDG_CONFIG_HOME/amplitude/amplitude.m3u') -eq 3 ]"

echo "--- a queued track plays next, ahead of list order"
export XDG_CONFIG_HOME="$WORK/config-queue"
INI="$XDG_CONFIG_HOME/amplitude/amplitude.ini"
mkdir -p "$WORK/queue"
"$MKWAV" "$WORK/queue/1 first.wav" 5 440 0
"$MKWAV" "$WORK/queue/2 second.wav" 5 440 0
"$MKWAV" "$WORK/queue/3 third.wav" 5 440 0
start --scale 1 "$WORK/queue"
drive key 0 e 0  click 2 100 47 0  key 2 q 0  wait 4500     # playlist, third row, Q; then let the first end
quit
ok "the third track followed the first" setting "track=2"

echo "--- several starts in one moment are a single request"
# What a file manager does when asked to open a selection: one start per
# file. The first of the burst plays; the others are added behind it. A
# start that comes later is a new request and plays.
export XDG_CONFIG_HOME="$WORK/config-burst"
INI="$XDG_CONFIG_HOME/amplitude/amplitude.ini"
M3U="$XDG_CONFIG_HOME/amplitude/amplitude.m3u"
start --scale 1 "$WORK/queue/1 first.wav"
"$PLAYER" "$WORK/queue/2 second.wav"; "$PLAYER" "$WORK/queue/3 third.wav"; "$PLAYER" "$WORK/music/Album A/01 alpha.wav"
sleep 0.5
quit
ok "the first file of the burst plays, not the last" setting "track=1"
ok "all of them were added" tracks 4
start
sleep 0.3
"$PLAYER" "$WORK/music/Album B/01 gamma.wav"
sleep 0.5
quit
ok "a later start plays its file" setting "track=4"

echo "--- with shuffle on, previous returns to the track played before"
# First plays; third is queued, so Next goes there whatever shuffle would
# pick; Previous must then return to the first, not to the second (the one
# above the third in the list).
export XDG_CONFIG_HOME="$WORK/config-back"
INI="$XDG_CONFIG_HOME/amplitude/amplitude.ini"
start --scale 1 "$WORK/queue"
drive key 0 s 0  key 0 e 0  click 2 100 47 0  key 2 q 0  key 0 b 0  wait 300  key 0 z 0  wait 300
quit
ok "previous went back to the first track" setting "track=0"
ok "shuffle was on" setting "shuffle=1"

if [ $failed -eq 0 ]; then echo "gui test passed"; else echo "gui test FAILED"; fi
exit $failed
