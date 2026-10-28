"""Test the file encoder against independent reference packets and PCM/WAV forms."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[3]
ap = argparse.ArgumentParser()
ap.add_argument('executable', type=Path)
args = ap.parse_args()
exe = str(args.executable.resolve())
(ROOT/'build').mkdir(exist_ok=True)

def run(*argv, ok=True):
    p = subprocess.run([exe, *map(str, argv)], capture_output=True)
    if ok and p.returncode:
        raise AssertionError(p.stderr.decode(errors='replace'))
    if not ok:
        assert p.returncode != 0
    return p

with tempfile.TemporaryDirectory(prefix='cli-', dir=ROOT/'build') as d:
    d = Path(d)
    output, sizes = d/'out.bin', d/'out.sizes'
    count = 0
    for line in (ROOT/'tests/vectors/parity/runs.txt').read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        name, decoder, profile, rate, bitrate, ms, packet, chunk, ll = line.split()
        pcm = ROOT/f'tests/vectors/parity/mix{rate}.s32le'
        variant = '1' if profile == '1' else ('3' if int(rate)>48000 or (decoder=='R2' and ll=='0') else ('4' if decoder=='R2' else '0'))
        run('-i', pcm, '-o', output, '--raw', 's32le24', '--rate', rate,
            '--decoder', decoder.lower(), '--profile', 'll' if ll=='1' else 'hq',
            '--bitrate', bitrate, '--period', ms, '--packet', packet,
            '--chunk', chunk, '--variant', variant, '--sizes', sizes)
        assert output.read_bytes() == (ROOT/f'tests/vectors/parity/{name}.bin').read_bytes(), name
        assert sizes.read_text().splitlines() == (ROOT/f'tests/vectors/parity/{name}.sizes').read_text().splitlines(), name+' sizes'
        count += 1
    print('PASS CLI reference captures:', count, '/', count)

    # The same representable PCM must produce identical bytes through every input adapter.
    samples = [(j*127)%65536-32768 for j in range(48000*2)]
    reference = None
    for bits in (16,24,32):
        raw = d/f'input{bits}.pcm'
        if bits==16:
            data = struct.pack('<'+'h'*len(samples), *samples)
        elif bits==24:
            data = b''.join(((v*256)&0xffffff).to_bytes(3,'little') for v in samples)
        else:
            data = struct.pack('<'+'i'*len(samples), *[v*65536 for v in samples])
        raw.write_bytes(data)
        run('-i', raw, '-o', output, '--raw', f's{bits}le')
        if reference is None:
            reference = output.read_bytes()
        assert output.read_bytes() == reference, f'raw{bits}'
        wav = d/f'input{bits}.wav'
        with wave.open(str(wav),'wb') as f:
            f.setnchannels(2); f.setsampwidth(bits//8); f.setframerate(48000); f.writeframes(data)
        run('-i', wav, '-o', output)
        assert output.read_bytes() == reference, f'wav{bits}'
    wav = d/'input24.wav'
    run('-i', wav, '-o', output, '--wire')
    swapped = bytearray(reference)
    swapped[0::2],swapped[1::2] = swapped[1::2],swapped[0::2]
    assert output.read_bytes() == swapped
    run('-i', wav, '-o', output, '--flush')
    assert output.read_bytes().startswith(reference)
    run('-i', wav, '-o', output, '--rate', 44100, ok=False)
    run('-i', wav, '-o', output, '--packet', 655, ok=False)
    broken = d/'truncated.pcm'; broken.write_bytes(b'abc')
    run('-i', broken, '-o', output, '--raw', 's16le', ok=False)
    broken.write_bytes(b'RIFFbroken')
    run('-i', broken, '-o', output, ok=False)
    print('PASS raw/WAV PCM16/24/32 conversion, byte swap, EOF flush and input rejection')
