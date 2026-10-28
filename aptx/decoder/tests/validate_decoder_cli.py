"""Exercise packet detection, WAV serialization, raw PCM, delay and errors."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

ROOT=Path(__file__).resolve().parents[3]
ap=argparse.ArgumentParser();ap.add_argument('executable',type=Path);args=ap.parse_args()
exe=str(args.executable.resolve());(ROOT/'build').mkdir(exist_ok=True)
def run(*argv,ok=True):
    p=subprocess.run([exe,*map(str,argv)],capture_output=True)
    assert (p.returncode==0)==ok,p.stderr.decode(errors='replace')
    return p
def load(path):
    with wave.open(str(path),'rb') as f:
        assert f.getnchannels()==2
        return f.getframerate(),f.getsampwidth(),f.getnframes(),f.readframes(f.getnframes())

with tempfile.TemporaryDirectory(prefix='decode-cli-',dir=ROOT/'build') as temp:
    temp=Path(temp);output=temp/'out.wav';count=0
    for line in (ROOT/'tests/vectors/parity/runs.txt').read_text().splitlines():
        if not line.strip() or line.startswith('#'):continue
        name,_,_,rate,*_=line.split();source=ROOT/f'tests/vectors/parity/{name}.bin'
        run('-i',source,'-o',output)
        actual_rate,width,frames,pcm=load(output)
        assert actual_rate==int(rate) and width==3 and frames>1000,name
        count+=1
    print('PASS decoder CLI:',count,'reference streams, automatic config/rate detection')
    source=ROOT/'tests/vectors/parity/c3_44k_i1_L3.bin'
    run('-i',source,'-o',output);rate,_,frames,data24=load(output)
    values=[]
    for at in range(0,len(data24),3):
        v=int.from_bytes(data24[at:at+3],'little');values.append(v if v<0x800000 else v-0x1000000)
    raw=temp/'out.pcm';run('-i',source,'-o',raw,'--raw')
    assert struct.unpack('<'+'i'*(len(raw.read_bytes())//4),raw.read_bytes())==tuple(values)
    for bits in (16,32):
        run('-i',source,'-o',output,'--bits',bits)
        r,width,n,b=load(output);assert r==rate and width==bits//8 and n==frames
        got=struct.unpack('<'+('h' if bits==16 else 'i')*(len(b)//width),b)
        assert got==tuple(v>>8 if bits==16 else v*256 for v in values)
    run('-i',source,'-o',output,'--keep-delay')
    _,_,n,b=load(output);assert n==frames+248 and b[248*6:]==data24
    swapped=temp/'swapped.bin';b=bytearray(source.read_bytes());b[0::2],b[1::2]=b[1::2],b[0::2];swapped.write_bytes(b)
    run('-i',swapped,'-o',output);assert load(output)[3]==data24
    run('-i',source,'-o',output,'--rate',48000,ok=False)
    run('-i',source,'-o',output,'--variant',0,ok=False)
    # 88.2 kHz has no golden packet capture: produce a real stream with the
    # separately validated C encoder and exercise header code 0xc end-to-end.
    encoder=args.executable.resolve().with_name('aptx_aa_encode'+args.executable.suffix)
    if encoder.exists():
        wave88=temp/'input88.wav';coded88=temp/'coded88.bin'
        values88=[((j*7919)%65536-32768)*64 for j in range(17640*2)]
        packed=b''.join((v&0xffffff).to_bytes(3,'little') for v in values88)
        with wave.open(str(wave88),'wb') as f:
            f.setnchannels(2);f.setsampwidth(3);f.setframerate(88200);f.writeframes(packed)
        p=subprocess.run([str(encoder),'-i',str(wave88),'-o',str(coded88),'--decoder','r2'],capture_output=True)
        assert p.returncode==0,p.stderr.decode(errors='replace')
        run('-i',coded88,'-o',output)
        assert load(output)[0]==88200 and load(output)[2]>1000
        print('PASS actual 88.2 kHz encode -> auto-detect -> WAV decode')
    bad=temp/'bad.bin'
    for b in [b'',b'abc',b'123456789012',source.read_bytes()[:-1]]:
        bad.write_bytes(b);run('-i',bad,'-o',output,ok=False)
    print('PASS decoder WAV16/24/32, raw int32, delay trimming, swapped input, truncated/config rejection')
