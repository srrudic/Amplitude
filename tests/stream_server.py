#!/usr/bin/env python3
"""A tiny web server for the stream tests.

    tests/stream_server.py <port> <mp3 file> [<media directory>]

Serves, on 127.0.0.1 only:
    /file.mp3      the file, as a web server would
    /radio         the file over and over as an Icecast-style station, with
                   metadata blocks every 1000 bytes
With the directory of test media given as well:
    /radio.aac     the AAC file as a station in the same way, joined part way
                   through a frame as a listener might find it
    /radio.ogg     the Ogg Vorbis file as a station: over and over, each time
    /radio.opus    as a new logical stream, as stations do for every song
                   (and the Ogg Opus file likewise)
    /hls/aac.m3u8  HTTP Live Streaming: a list of qualities leading to a live
    /hls/ts.m3u8   playlist that moves on one segment a second; each segment
                   is the AAC file behind an ID3 tag, or the MP3 file in an
                   MPEG transport stream
    /hls/mp4.m3u8  the same as fragments of an MP4 file: a map describing a
                   video and a sound track, and segments with the AAC frames
    /hls/video.m3u8  a video whose sound is listed apart (the AAC broadcast)
    /hls/vod.m3u8  a finished recording: three segments, each the MP3 file
    /hls/key.m3u8  an encrypted stream (which the player refuses)
    /old           the same behind an old Shoutcast "ICY 200 OK" status line
    /redirect      a redirect to /radio
    /station.pls   a station playlist naming /radio
    /hls.m3u8      an HTTP Live Streaming playlist whose segments do not exist
    /quit          stops the server
anything else is 404. It stops by itself after two minutes, or after the
number of seconds in the environment variable STREAM_SERVER_SECONDS.
"""
import socket
import sys
import threading
import time

PORT = int(sys.argv[1])
DATA = open(sys.argv[2], "rb").read()
MEDIA = sys.argv[3] if len(sys.argv) > 3 else None
AAC = open(MEDIA + "/sfx.aac", "rb").read() if MEDIA else b""
VORBIS = open(MEDIA + "/sfx-vorbis.ogg", "rb").read() if MEDIA else b""
OPUS = open(MEDIA + "/sfx-opus.ogg", "rb").read() if MEDIA else b""
STARTED = time.time()

CRC_TABLE = []
for _i in range(256):
    _r = _i << 24
    for _ in range(8):
        _r = ((_r << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if _r & 0x80000000 else (_r << 1) & 0xFFFFFFFF
    CRC_TABLE.append(_r)


def ogg_with_serial(data, serial):
    """The Ogg file with its logical stream renumbered (and checksums redone)."""
    out, at = bytearray(), 0
    while at + 27 <= len(data) and data[at:at + 4] == b"OggS":
        segments = data[at + 26]
        size = 27 + segments + sum(data[at + 27:at + 27 + segments])
        page = bytearray(data[at:at + size])
        page[14:18] = serial.to_bytes(4, "little")
        page[22:26] = b"\0\0\0\0"
        crc = 0
        for byte in page:
            crc = ((crc << 8) & 0xFFFFFFFF) ^ CRC_TABLE[(crc >> 24) ^ byte]
        page[22:26] = crc.to_bytes(4, "little")
        out += page
        at += size
    return bytes(out)


def ogg_body(conn, data):
    serial = 1000
    while True:
        chunk = ogg_with_serial(data, serial)
        serial += 1
        for at in range(0, len(chunk), 700):
            conn.sendall(chunk[at:at + 700])
            time.sleep(0.002)


def ts_packet(pid, starts, payload, counter):
    """One 188-byte transport stream packet, padded in front if need be."""
    pad = 184 - len(payload)
    head = bytes([0x47, (0x40 if starts else 0) | pid >> 8, pid & 0xFF, (0x30 if pad else 0x10) | counter & 15])
    if pad == 1:
        head += b"\0"
    elif pad > 1:
        head += bytes([pad - 1, 0]) + b"\xff" * (pad - 2)
    return head + payload


def ts_segment(audio):
    """The MP3 data as a transport stream: tables, then the audio on PID 0x101."""
    pat = b"\0" + bytes([0x00, 0xB0, 0x0D, 0, 1, 0xC1, 0, 0, 0, 1, 0xF0, 0x00, 0, 0, 0, 0])
    pmt = b"\0" + bytes([0x02, 0xB0, 0x17, 0, 1, 0xC1, 0, 0, 0xE1, 0x01, 0xF0, 0x00,
                         0x1B, 0xE1, 0x00, 0xF0, 0x00,      # a video stream, to be passed over
                         0x03, 0xE1, 0x01, 0xF0, 0x00, 0, 0, 0, 0])
    pat = bytearray(pat)
    pat[11:13] = bytes([0xF0, 0x00])        # programme 1 is described on PID 0x1000
    out = ts_packet(0, True, bytes(pat) + b"\xff" * (184 - len(pat)), 0)
    out += ts_packet(0x1000, True, pmt + b"\xff" * (184 - len(pmt)), 0)
    counter = 0
    for at in range(0, len(audio), 1000):       # a PES packet per thousand bytes
        pes = b"\0\0\1\xc0\0\0\x80\x80\x05" + b"\x21\0\1\0\1" + audio[at:at + 1000]
        for part in range(0, len(pes), 184):
            out += ts_packet(0x101, part == 0, pes[part:part + 184], counter)
            counter += 1
            if part == 184:
                out += ts_packet(0x100, False, b"\xee" * 184, 0)   # some video in between
    return out


def box(name, *parts):
    body = b"".join(parts)
    return (len(body) + 8).to_bytes(4, "big") + name + body


def aac_frames():
    """The AAC file's frames without their headers, and what the headers said."""
    frames, at = [], 0
    while at + 7 <= len(AAC):
        size = (AAC[at + 3] & 3) << 11 | AAC[at + 4] << 3 | AAC[at + 5] >> 5
        frames.append(AAC[at + (7 if AAC[at + 1] & 1 else 9):at + size])
        at += size
    profile, rate = AAC[2] >> 6, AAC[2] >> 2 & 15
    channels = (AAC[2] & 1) << 2 | AAC[3] >> 6
    return frames, bytes([(profile + 1) << 3 | rate >> 1, (rate & 1) << 7 | channels << 3])


def mp4_map():
    """ftyp and moov: track 1 is video, track 2 the AAC."""
    _, config = aac_frames()

    def trak(number, entry):
        tkhd = box(b"tkhd", bytes(12), number.to_bytes(4, "big"), bytes(64))
        stsd = box(b"stsd", bytes(4), (1).to_bytes(4, "big"), entry)
        return box(b"trak", tkhd, box(b"mdia", box(b"mdhd", bytes(24)),
                                      box(b"minf", box(b"smhd", bytes(8)), box(b"stbl", stsd))))

    esds = box(b"esds", bytes(4), b"\x03\x80\x80\x80\x19", bytes(3), b"\x04\x11\x40\x15", bytes(11),
               b"\x05\x02", config, b"\x06\x01\x02")
    sound = box(b"mp4a", bytes(6), b"\0\1", bytes(8), b"\0\2\0\x10", bytes(4), b"\xac\x44\0\0", esds)
    video = box(b"avc1", bytes(78), box(b"avcC", bytes(8)))
    return box(b"ftyp", b"iso5", bytes(4), b"iso5") + box(b"moov", box(b"mvhd", bytes(100)), trak(1, video),
                                                           trak(2, sound))


def mp4_segment(repeats):
    """styp, then two moof and mdat pairs; each moof lists video before sound."""
    frames, _ = aac_frames()
    frames = frames * repeats
    out = box(b"styp", b"msdh", bytes(4), b"msdh")
    for part in (frames[:len(frames) // 2], frames[len(frames) // 2:]):
        junk = bytes(50)

        def moof(offset):
            video = box(b"traf", box(b"tfhd", b"\0\2\0\x10", (1).to_bytes(4, "big"), (50).to_bytes(4, "big")),
                        box(b"trun", b"\0\0\0\1", (1).to_bytes(4, "big"), offset.to_bytes(4, "big")))
            sizes = b"".join(len(f).to_bytes(4, "big") for f in part)
            sound = box(b"traf", box(b"tfhd", b"\0\2\0\0", (2).to_bytes(4, "big")),
                        box(b"tfdt", bytes(8)),
                        box(b"trun", b"\0\0\2\1", len(part).to_bytes(4, "big"),
                            (offset + len(junk)).to_bytes(4, "big"), sizes))
            return box(b"moof", box(b"mfhd", bytes(8)), video, sound)

        out += moof(len(moof(0)) + 8) + box(b"mdat", junk, *part)
    return out


def hls_reply(conn, path):
    def send(body, kind=b"application/vnd.apple.mpegurl"):
        conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: " + kind + b"\r\n\r\n" + body)

    name = path.split("/")[2]
    if name in ("aac.m3u8", "ts.m3u8", "mp4.m3u8"):
        kind = name[:-5]
        send(("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=32000\nnowhere/low.m3u8\n"
              "#EXT-X-STREAM-INF:BANDWIDTH=128000\nnowhere/best.m3u8\n"     # the best does not answer
              "#EXT-X-STREAM-INF:BANDWIDTH=96000,CODECS=\"mp4a.40.2\"\n%s/live.m3u8\n" % kind).encode())
    elif name in ("aac", "ts", "mp4") and path.endswith("/live.m3u8"):
        first = int(time.time() - STARTED)
        text = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:%d\n" % first
        if name == "mp4":
            text += "#EXT-X-MAP:URI=\"init.mp4\"\n"
        for number in range(first, first + 5):
            text += "#EXTINF:1.0,\nseg%d.%s\n" % (number, name)
        send(text.encode())
    elif name == "aac":
        song = b"TIT2\0\0\0\x09\0\0\3HLS Song" + b"TPE1\0\0\0\x0b\0\0\3HLS Artist"
        send(b"ID3\4\0\0\0\0\0\x0a" + b"0123456789" + b"ID3\4\0\0\0\0\0" + bytes([len(song)]) + song + AAC * 8,
             b"audio/aac")
    elif name == "mp4":
        send(mp4_map() if path.endswith("init.mp4") else mp4_segment(8), b"video/mp4")
    elif name == "ts":
        send(ts_segment(DATA * 5), b"video/mp2t")
    elif name == "video.m3u8":
        send(b"#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"English\",URI=\"aac/live.m3u8\"\n"
             b"#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=640x360,AUDIO=\"a\"\nnowhere/video.m3u8\n")
    elif name == "vod.m3u8":
        send(b"#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXTINF:1,\nvod/a.mp3\n#EXTINF:1,\n/hls/vod/b.mp3\n"
             b"#EXTINF:1,\nhttp://127.0.0.1:%d/hls/vod/c.mp3\n#EXT-X-ENDLIST\n" % PORT)
    elif name == "vod":
        send(DATA, b"audio/mpeg")
    elif name == "key.m3u8":
        send(b"#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n#EXTINF:1,\nvod/a.mp3\n")
    else:
        conn.sendall(b"HTTP/1.0 404 Not Found\r\n\r\n")
INTERVAL = 1000
TITLES = ["Artist - First Song", "Ärtist - Sëcond Song"]


def radio_body(conn, DATA=DATA, position=0):
    """Audio for ever, a metadata block after every INTERVAL bytes."""
    block = 0
    while True:
        chunk = bytearray()
        while len(chunk) < INTERVAL:
            take = min(INTERVAL - len(chunk), len(DATA) - position)
            chunk += DATA[position:position + take]
            position = (position + take) % len(DATA)
        # The title changes at the third block; in between, empty blocks.
        text = b""
        if block == 0:
            text = ("StreamTitle='%s';" % TITLES[0]).encode("utf-8")
        elif block == 3:
            text = ("StreamTitle='%s';StreamUrl='';" % TITLES[1]).encode("latin-1")
        text += b"\0" * (-len(text) % 16)
        conn.sendall(bytes(chunk) + bytes([len(text) // 16]) + text)
        block += 1
        time.sleep(0.002)


def serve(conn):
    try:
        request = b""
        while b"\r\n\r\n" not in request:
            more = conn.recv(4096)
            if not more:
                return
            request += more
        path = request.split(b" ")[1].decode()
        wants_meta = b"icy-metadata: 1" in request.lower()
        base = "http://127.0.0.1:%d" % PORT
        if path == "/file.mp3":
            conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: %d\r\n\r\n" % len(DATA))
            conn.sendall(DATA)
        elif path in ("/radio", "/old") and wants_meta:
            status = b"ICY 200 OK" if path == "/old" else b"HTTP/1.0 200 OK"
            conn.sendall(status + b"\r\nContent-Type: audio/mpeg\r\nicy-name: Test Radio\r\nicy-br: 128\r\n"
                         b"icy-metaint: %d\r\n\r\n" % INTERVAL)
            radio_body(conn)
        elif path == "/radio.aac" and AAC:
            conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: audio/aacp\r\nicy-name: Test Radio AAC\r\n"
                         b"icy-br: 64\r\nicy-metaint: %d\r\n\r\n" % INTERVAL)
            radio_body(conn, AAC, 100)
        elif path in ("/radio.ogg", "/radio.opus") and MEDIA:
            conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: application/ogg\r\nicy-name: Test Radio Ogg\r\n\r\n")
            ogg_body(conn, VORBIS if path == "/radio.ogg" else OPUS)
        elif path.startswith("/hls/") and MEDIA:
            hls_reply(conn, path)
        elif path == "/redirect":
            conn.sendall(("HTTP/1.0 302 Found\r\nLocation: /radio\r\n\r\n").encode())
        elif path == "/station.pls":
            body = ("[playlist]\nNumberOfEntries=1\nFile1=%s/radio\nTitle1=Test\n" % base).encode()
            conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: audio/x-scpls\r\n\r\n" + body)
        elif path == "/hls.m3u8":
            conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n\r\n"
                         b"#EXTM3U\n#EXT-X-VERSION:3\n#EXTINF:10,\nhttp://127.0.0.1/seg0.ts\n")
        elif path == "/quit":
            conn.sendall(b"HTTP/1.0 200 OK\r\n\r\n")
            threading.Thread(target=lambda: (time.sleep(0.1), sys.exit(0))).start()
            import os
            os._exit(0)
        else:
            conn.sendall(b"HTTP/1.0 404 Not Found\r\n\r\n")
    except OSError:
        pass
    finally:
        conn.close()


def main():
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", PORT))
    server.listen(16)
    lifetime = float(__import__("os").environ.get("STREAM_SERVER_SECONDS", "120"))
    threading.Timer(lifetime, lambda: __import__("os")._exit(0)).start()
    while True:
        conn, _ = server.accept()
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
