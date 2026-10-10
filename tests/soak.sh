#!/bin/sh
# Soak test: runs the player for a long time while a script keeps it busy,
# and watches whether it grows. Looks for leaks of memory, threads and file
# handles (sockets, files, the sound device) that a short test cannot show.
#
#     tests/soak.sh <player> <drive> <mkwav> [minutes] [log file]
#     make soak                       (30 minutes)
#     make soak SOAK_MINUTES=180
#
# What it does, at random and a few times a second: next and previous track,
# seeking, pause, stop and play, the jump and Open location windows, the menu,
# minimising and restoring, and every so often a change of size (which
# rebuilds all windows). The
# playlist mixes files, every kind of stream from tests/stream_server.py, an
# address that fails, and a disc image, so the reader threads for streams and
# CDs are started and stopped thousands of times.
#
# Like the GUI test it needs an X display and uses its own settings folder;
# the player is muted. Its windows are on screen for the whole time: leave
# them alone, and do not run another Amplitude meanwhile.
#
# Every half minute a line goes to the log: seconds, resident memory (kB),
# threads, open file descriptors. At the end the second half of the run is
# compared with the first; steady growth there is a leak.
set -u

PLAYER=$(realpath "$1"); DRIVE=$(realpath "$2"); MKWAV=$(realpath "$3")
MINUTES=${4:-30}
LOG=${5:-build/soak.log}
WORK=$(mktemp -d /tmp/amplitude-soak-XXXXXX)
PORT=$((23000 + $$ % 5000))
URL="http://127.0.0.1:$PORT"

cleanup() {
    [ -n "${PID:-}" ] && kill "$PID" 2>/dev/null
    [ -n "${SERVER:-}" ] && kill "$SERVER" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

export XDG_CONFIG_HOME="$WORK/config"
mkdir -p "$XDG_CONFIG_HOME/amplitude" "$WORK/music" "$WORK/cd" "$(dirname "$LOG")"
printf 'volume=0\ncd_names=0\nrepeat=1\n' > "$XDG_CONFIG_HOME/amplitude/amplitude.ini"
for n in 1 2 3 4; do "$MKWAV" "$WORK/music/$n tone.wav" 4 $((220 * n)) 0; done
cp tests/media/sfx.mp3 tests/media/sfx.flac tests/media/sfx-opus.ogg tests/media/sfx-vorbis.ogg \
   tests/media/sfx-aac.mp4 tests/media/sfx.aac "$WORK/music/"
head -c $((2352 * 75 * 20)) /dev/zero > "$WORK/cd/disc.bin"
printf 'PERFORMER "Band"\nFILE "disc.bin" BINARY\n TRACK 01 AUDIO\n  TITLE "One"\n  INDEX 01 00:00:00\n TRACK 02 AUDIO\n  TITLE "Two"\n  INDEX 01 00:10:00\n' > "$WORK/cd/disc.cue"

STREAM_SERVER_SECONDS=$((MINUTES * 60 + 600)) python3 tests/stream_server.py "$PORT" tests/media/sfx.mp3 tests/media >/dev/null 2>&1 &
SERVER=$!
sleep 0.5

"$PLAYER" --scale 1 "$WORK/music" "$URL/radio" "$URL/radio.aac" "$URL/radio.ogg" "$URL/radio.opus" \
    "$URL/hls/aac.m3u8" "$URL/hls/ts.m3u8" "$URL/hls/mp4.m3u8" "$URL/hls/vod.m3u8" "$URL/nothing-here" \
    "$URL/file.mp3" "$URL/station.pls" "$WORK/cd/disc.cue" 2>"$WORK/stderr" &
PID=$!
export DRIVE_PID=$PID
sleep 2

sample() {      # seconds since the start
    [ -d "/proc/$PID" ] || return 1
    echo "$1 $(awk '/VmRSS/{print $2}' "/proc/$PID/status") $(awk '/Threads/{print $2}' "/proc/$PID/status") $(ls "/proc/$PID/fd" | wc -l)" >> "$LOG"
}
drive() { "$DRIVE" "$@" >/dev/null 2>&1; }
rnd() { echo $(( $(od -An -N2 -tu2 /dev/urandom) % $1 )); }

: > "$LOG"
START=$(date +%s); END=$((START + MINUTES * 60)); NEXT_SAMPLE=$START
big=0; actions=0
echo "soak: $MINUTES minutes, player $PID, log $LOG"
while [ "$(date +%s)" -lt "$END" ]; do
    now=$(date +%s)
    if [ "$now" -ge "$NEXT_SAMPLE" ]; then
        sample $((now - START)) || { echo "soak: THE PLAYER DIED after $((now - START)) s"; cat "$WORK/stderr"; exit 1; }
        NEXT_SAMPLE=$((now + 30))
    fi
    # Positions are real pixels; at the larger size everything is 1.25 times as far.
    if [ $big = 1 ]; then s=125; else s=100; fi
    case $(rnd 20) in
        0|1|2|3|4|5) drive key 0 b 0 ;;                          # next
        6|7)         drive key 0 z 0 ;;                          # previous
        8|9)         drive key 0 Right 0 key 0 Right 0 key 0 Left 0 ;;
        10)          drive key 0 c 0 wait 200 key 0 c 0 ;;       # pause and resume
        11)          drive key 0 v 0 wait 200 key 0 x 0 ;;       # stop and play
        12)          drive key 0 j 0 wait 200 key 3 t 0 key 3 o 0 key 3 Escape 0 ;;
        13)          drive key 0 l 4 wait 200 key 5 h 0 key 5 Escape 0 ;;
        14|15)       drive rclick 0 50 50 500 300 wait 150 menu 2000 5 ;;   # the menu, closed by a click outside
        16)          # minimised for a moment (nothing is painted meanwhile), and back
                     drive click 0 $((258 * s / 100)) $((7 * s / 100)) 0 wait $(( $(rnd 1500) + 200 )) restore 0 ;;
        17)          # the other size: all windows are destroyed and made again
                     if [ $big = 0 ]; then y=52; else y=39; fi
                     drive rclick 0 50 50 500 300 wait 150 menu $((30 * s / 100)) $((152 * s / 100)) wait 200 \
                           menu $((30 * s / 100)) $((y * s / 100)) wait 700
                     big=$((1 - big)) ;;
        *)           ;;                                          # let it play
    esac
    actions=$((actions + 1))
    sleep "0.$(( $(rnd 9) + 1 ))"
done
sample $(( $(date +%s) - START ))

# Close it properly, so that shutting down is tested too.
if [ $big = 1 ]; then drive click 0 335 9 0; else drive click 0 268 7 0; fi
for i in 1 2 3 4 5 6 7 8 9 10; do [ -d "/proc/$PID" ] || break; sleep 0.5; done
status=0
if [ -d "/proc/$PID" ]; then echo "soak: FAIL  the player did not exit when closed"; status=1; fi
if grep -q . "$WORK/stderr"; then echo "soak: the player wrote to stderr:"; head -40 "$WORK/stderr"; status=1; fi

echo "soak: $actions actions; seconds, memory kB, threads, descriptors:"
awk '{ n++; t[n]=$1; m[n]=$2; th[n]=$3; fd[n]=$4 }
     END {
         step = n > 12 ? int(n / 12) : 1
         for (i = 1; i <= n; i += step) printf "  %6d s  %7d kB  %3d threads  %3d fds\n", t[i], m[i], th[i], fd[i]
         h = int(n / 2); q = int(n / 4)
         for (i = q + 1; i <= h; i++) { a += m[i]; ta += th[i]; fa += fd[i]; na++ }
         for (i = h + q + 1; i <= n; i++) { b += m[i]; tb += th[i]; fb += fd[i]; nb++ }
         if (na && nb) {
             printf "soak: second quarter against last quarter: memory %.0f -> %.0f kB (%+.1f%%), threads %.1f -> %.1f, descriptors %.1f -> %.1f\n",
                    a / na, b / nb, (b / nb - a / na) * 100 / (a / na), ta / na, tb / nb, fa / na, fb / nb
             if ((b / nb) > (a / na) * 1.10 || (fb / nb) > (fa / na) + 3 || (tb / nb) > (ta / na) + 2) { print "soak: FAIL  it grew"; exit 1 }
         }
     }' "$LOG" || status=1
[ $status = 0 ] && echo "soak passed"
exit $status
