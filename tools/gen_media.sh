#!/bin/sh
# Generates the books served by tools/mock_abs.py (needs ffmpeg). Idempotent.
#   long.m4b       6 h mono AAC-LC, moov at the END (large stsz: ~3.7 MB moov)
#   faststart.m4a  2 min stereo AAC-LC, moov at the start
#   part1.mp3      60 s, part2.mp3 60 s (a two-file MP3 book)
#   cover0..7.jpg  plain covers
set -e
D="${1:-tests/media}"
mkdir -p "$D"
cd "$D"
q="-hide_banner -loglevel error -y"
# a tone that changes pitch every second, so decoded positions are checkable
tone="aevalsrc=0.3*sin(2*PI*(220+20*mod(floor(t)\,10))*t):s=44100"
[ -f long.m4b ] || ffmpeg $q -f lavfi -i "$tone:d=21600" -ac 1 -c:a aac -b:a 32k long.m4b
[ -f faststart.m4a ] || ffmpeg $q -f lavfi -i "$tone:d=120" -ac 2 -c:a aac -b:a 64k -movflags +faststart faststart.m4a
[ -f part1.mp3 ] || ffmpeg $q -f lavfi -i "$tone:d=60" -ac 2 -c:a libmp3lame -b:a 64k part1.mp3
[ -f part2.mp3 ] || ffmpeg $q -f lavfi -i "$tone:d=60" -ac 2 -c:a libmp3lame -b:a 64k part2.mp3
i=0
for c in 0x7a3b2e 0x2e5a7a 0x3b7a4f 0x7a2e5c 0x6b6b2e 0x2e6b6b 0x4a2e7a 0x7a5a2e; do
    [ -f cover$i.jpg ] || ffmpeg $q -f lavfi -i "color=c=$c:s=400x400,format=rgb24,geq=r='r(X,Y)*(0.6+0.4*Y/H)':g='g(X,Y)*(0.6+0.4*Y/H)':b='b(X,Y)*(0.6+0.4*Y/H)'" -frames:v 1 cover$i.jpg
    i=$((i + 1))
done
echo "media ready in $D"
