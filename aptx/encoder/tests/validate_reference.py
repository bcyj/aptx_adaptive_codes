"""Optional Rust/C differential check for 88.2 kHz and full-scale corner inputs.

Requires rustc only to build the unmodified original for comparison. C has no Rust dependency.
"""
import argparse
from pathlib import Path
import os
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ap = argparse.ArgumentParser()
ap.add_argument('executable', type=Path)
ap.add_argument('--source', type=Path, default=ROOT.parent/'aptx-adaptive-linux')
ap.add_argument('--rustc', default='rustc')
args = ap.parse_args()
build = ROOT/'build/reference'
build.mkdir(parents=True, exist_ok=True)
source = args.source/'crates/aptx-adaptive'
shutil.copyfile(source/'bundled_tables.rs', build/'tables.rs')
env = dict(os.environ, OUT_DIR=str(build))
subprocess.run([args.rustc, '--crate-name', 'aptx_adaptive', '--crate-type', 'rlib', '--edition=2024', '-O',
                str(source/'src/lib.rs'), '-o', str(build/'libaptx_adaptive.rlib')], env=env, check=True)
driver = build/('reference.exe' if os.name=='nt' else 'reference')
subprocess.run([args.rustc, '--edition=2024', '-O', str(ROOT/'aptx/encoder/tests/reference.rs'), '--extern',
                'aptx_adaptive='+str(build/'libaptx_adaptive.rlib'), '-o', str(driver)], check=True)

# Includes exact full-scale endpoints, anti-phase square waves, impulses, silence and noise.
for rate, decoder, profile, bitrate, packet, ms in [
        (88200,'r1','hq',364000,656,14), (88200,'r2','hq',90000,656,50),
        (88200,'r2','hq',1000000,960,9), (96000,'r2','hq',1000000,1032,8),
        (48000,'r1','hq',600000,656,12), (44100,'r1','ll',200000,348,10),
        (48000,'r2','hq',90000,656,50), (48000,'r2','ll',600000,560,10)]:
    s = 0x12345678
    values = []
    for j in range(rate*2):
        s = (s*1664525+1013904223)&0xffffffff
        phase = (j//4096)%4
        v = (-8388608 if j%2 else 8388607) if phase==0 else (
            (8388607 if j%1024==0 else 0) if phase==1 else (
            0 if phase==2 else ((s if s<0x80000000 else s-0x100000000)>>8)))
        values.append(v)
    pcm, want, got = build/'corner.pcm', build/'rust.bin', build/'c.bin'
    pcm.write_bytes(struct.pack('<'+'i'*len(values),*values))
    chunk = rate//1000
    subprocess.run([str(driver), *map(str,[rate,bitrate,packet,ms,chunk,profile,decoder,pcm,want])], check=True)
    p = subprocess.run([str(args.executable.resolve()), '-i',str(pcm),'-o',str(got),'--raw','s32le24',
        '--rate',str(rate),'--decoder',decoder,'--profile',profile,'--bitrate',str(bitrate),
        '--packet',str(packet),'--period',str(ms),'--chunk',str(chunk)], capture_output=True)
    assert p.returncode==0, p.stderr.decode(errors='replace')
    assert got.read_bytes()==want.read_bytes(), (rate,decoder,profile,bitrate)
    print('PASS Rust/C full-scale differential:',rate,decoder,profile,bitrate,len(got.read_bytes()),'bytes')
