#!/bin/sh
# Generates real media fixtures with ffmpeg (skipped if ffmpeg is missing).
D=tests/fixtures/media
command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg missing: media fixtures skipped"; exit 0; }
mkdir -p "$D"
[ -f "$D/aac_lc.m4b" ] && exit 0
cat > "$D/chapters.txt" <<EOT
;FFMETADATA1
title=Sine Book
artist=Test Author
[CHAPTER]
TIMEBASE=1/1000
START=0
END=10000
title=Chapter One
[CHAPTER]
TIMEBASE=1/1000
START=10000
END=20000
title=Chapter Two
[CHAPTER]
TIMEBASE=1/1000
START=20000
END=30000
title=Chapter Three
EOT
Q="-hide_banner -loglevel error -y"
# 30 s, 440 Hz left / 660 Hz right, moov at the END (no faststart) like many m4b rips
ffmpeg $Q -f lavfi -i "sine=frequency=440:duration=30:sample_rate=44100" -f lavfi -i "sine=frequency=660:duration=30:sample_rate=44100" \
    -i "$D/chapters.txt" -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" -map_metadata 2 \
    -c:a aac -b:a 96k -f mp4 "$D/aac_lc.m4b"
# mono 22.05 kHz, faststart
ffmpeg $Q -f lavfi -i "sine=frequency=440:duration=12:sample_rate=22050" -c:a aac -b:a 48k -movflags +faststart "$D/aac_mono.m4a"
# CBR mp3 with ID3v2 tag
ffmpeg $Q -f lavfi -i "sine=frequency=440:duration=12:sample_rate=44100" -c:a libmp3lame -b:a 64k -metadata title=Sine "$D/cbr.mp3"
# VBR mp3 with Xing header
ffmpeg $Q -f lavfi -i "sine=frequency=440:duration=12:sample_rate=44100" -c:a libmp3lame -q:a 4 "$D/vbr.mp3"
# HE-AAC via afconvert (macOS) if available
if command -v afconvert >/dev/null 2>&1; then
    ffmpeg $Q -f lavfi -i "sine=frequency=440:duration=12:sample_rate=44100" -ac 2 "$D/tmp.wav"
    afconvert -f m4af -d aach -b 48000 "$D/tmp.wav" "$D/he_aac.m4a" 2>/dev/null || true
    rm -f "$D/tmp.wav"
fi
ls "$D"
