"""Small authored impact sounds; no external samples. Run from any directory."""
import math,struct,wave
from pathlib import Path
out=Path(__file__).resolve().parents[1]/'assets/audio'
for name,duration in [('ball',.19),('block',.13),('chicken',.34)]:
    data=[];phase=0
    for i in range(int(22050*duration)):
        t=i/22050;u=t/duration
        if name=='chicken':
            f=600+1150*math.sin(math.pi*u)**2+100*math.sin(t*75)
            phase+=2*math.pi*f/22050
            value=(math.sin(phase)+.28*math.sin(phase*3))*math.sin(math.pi*u)**1.4*.26
        else:
            f=115 if name=='ball' else 185
            phase+=2*math.pi*(f+60*math.exp(-t*55))/22050
            value=(math.sin(phase)+(.12 if name=='ball' else .35)*math.sin(phase*2.73))*math.exp(-t*(24 if name=='ball' else 40))*.42*min(1,t*1200)
        data.append(struct.pack('<h',int(max(-1,min(1,value))*32767)))
    with wave.open(str(out/f'toy-{name}.wav'),'wb') as w:
        w.setparams((1,2,22050,0,'NONE','not compressed'));w.writeframes(b''.join(data))
