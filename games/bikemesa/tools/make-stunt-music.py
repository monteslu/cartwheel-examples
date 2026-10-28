#!/usr/bin/env python3
"""Prepare the complete Marine band performance of America the Beautiful.
Usage: python3 make-stunt-music.py /path/to/ihas-100010409.mp3
Source: https://www.loc.gov/item/ihas.100010409/
Audio: https://tile.loc.gov/storage-services/public/navcc/ihas-100010409/ihas-100010409.mp3
The Library catalog lists 'No known restrictions on use or reproduction.'
See ../CREDITS. The source recording is not bundled separately.
"""
import hashlib
from pathlib import Path
import subprocess
import sys

source=Path(sys.argv[1])
assert hashlib.sha256(source.read_bytes()).hexdigest()=='8f2db721c8198387c67b63fc923cfe8a589067dc9616b6fd1a0769f347c70a3c', 'unexpected source recording'
out=Path(__file__).resolve().parents[1]/'assets/audio/music_stunt.ogg'
# Keep the full musical cadence; remove the source recording's 3.4s silent tail.
# A short fade removes edge clicks on restart;
# moderate loudness leaves room for tires, landings and UI sounds.
encoders=subprocess.check_output(['ffmpeg','-hide_banner','-encoders'],stderr=subprocess.DEVNULL,text=True)
codec=['-c:a','libvorbis'] if 'libvorbis' in encoders else ['-c:a','vorbis','-strict','experimental']
subprocess.run(['ffmpeg','-y','-loglevel','error','-i',str(source),'-af',
 'atrim=end=150.1,loudnorm=I=-18:TP=-2:LRA=11,afade=t=in:d=0.05,afade=t=out:st=149.8:d=0.3',
 '-ar','44100','-ac','2',*codec,'-q:a','6','-metadata','title=America the Beautiful',
 '-metadata','artist=Second Marine Aircraft Wing Band',str(out)],check=True)
print(out)
