#!/usr/bin/env python3
"""Build a compact circular edit of the public-domain Marine Band performance.
Usage: python3 make-circus-music.py /path/to/gladiators.oga
Source and attribution are in ../CREDITS. Original is not bundled in the cart.
Requires ffmpeg. Keeps the complete arrangement; overlaps only the final tail.
"""
import array, hashlib, math, pathlib, subprocess, sys
source = pathlib.Path(sys.argv[1])
assert hashlib.sha256(source.read_bytes()).hexdigest() == '2dff46321db24757697800f5cb3779748a556c381456d82ea9c13285d3e69f03', 'unexpected source recording'
rate = 32000
raw = subprocess.check_output(['ffmpeg','-v','error','-i',str(source),'-ar',str(rate),'-ac','1','-f','f32le','-'])
a = array.array('f'); a.frombytes(raw)
if sys.byteorder != 'little': a.byteswap()
# Circular overlap: tail transitions into head, then the file boundary advances
# to the immediately following source sample. Avoids a discontinuous restart.
n = int(.75 * rate)
loop = a[n:-n]
for i in range(n):
    w = .5 - .5 * math.cos(math.pi * i / (n-1))
    loop.append((1-w)*a[-n+i] + w*a[i])
peak = max(abs(x) for x in loop)
gain = min(1, .89 / peak)
loop = array.array('f', (x*gain for x in loop))
if sys.byteorder != 'little': loop.byteswap()
out = pathlib.Path(__file__).resolve().parents[1] / 'assets/audio/music_circus.ogg'
subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(rate),'-ac','1','-i','pipe:0',
    '-c:a','libvorbis','-b:a','48k','-map_metadata','-1',
    '-metadata','title=Entrance of the Gladiators (BikeMesa loop)',
    '-metadata','artist=United States Marine Band; Timothy W. Foley',str(out)], input=loop.tobytes(),check=True)
print(f'{out}: {len(loop)/rate:.3f}s, {out.stat().st_size} bytes')
