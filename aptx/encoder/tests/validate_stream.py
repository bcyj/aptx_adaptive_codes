"""Independent reference captures for mid-stream level changes, plus API contracts."""
import argparse
import ctypes as C
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ap = argparse.ArgumentParser()
ap.add_argument('library', type=Path)
args = ap.parse_args()
lib = C.CDLL(str(args.library.resolve()))
import runpy
constructor=runpy.run_path(str(ROOT/'tests/support/allocator.py'))['constructor']

class Config(C.Structure):
    _fields_ = [('sample_rate', C.c_uint32), ('channels', C.c_uint),
                ('profile', C.c_int), ('decoder', C.c_int)]
class Row(C.Structure):
    _fields_ = [('bitrate', C.c_uint32), ('period_ms', C.c_uint32),
                ('samples_per_channel', C.c_uint32), ('packet_bytes', C.c_size_t), ('cfg', C.c_uint8)]
class Progress(C.Structure):
    _fields_ = [('consumed', C.c_size_t), ('written', C.c_size_t)]

def bind(name, result, *argtypes):
    f = getattr(lib, 'aptx_aa_'+name)
    f.restype, f.argtypes = result, list(argtypes)
    return f

ptr, size, u, i = C.c_void_p, C.c_size_t, C.c_uint, C.c_int
create = constructor(lib,'aptx_aa_stream_create', C.POINTER(Config), size)
destroy = bind('stream_destroy', None, ptr)
push = bind('stream_push', i, ptr, ptr, size, ptr, size, C.POINTER(size))
stream_workspace = bind('stream_workspace_bytes', size, size)
stream_init = bind('stream_init_in_memory', i, ptr, size, C.POINTER(Config), size, size,
                   C.POINTER(ptr))
request = bind('stream_request_level', i, ptr, u)
apply_now = bind('stream_apply_level_now', i, ptr)
map_one = bind('stream_map_level_one', None, ptr, i)
hold = bind('stream_hold_level', None, ptr, i)
getrow = bind('stream_get_row', i, ptr, C.POINTER(Row))
getlevel = bind('stream_level', u, ptr)
deficit = bind('stream_deficit', C.c_int64, ptr)
table_row = bind('table_row', i, C.POINTER(Config), ptr, size, u, C.POINTER(Row))
packet_budget = bind('packet_budget', size, size)
ec = constructor(lib,'aptx_aa_create', C.POINTER(Config), C.c_uint32, size)
ed = bind('destroy', None, ptr)
encode = bind('encode', i, ptr, ptr, size, ptr, size, ptr, size, C.POINTER(Progress))
reset = bind('reset', i, ptr, C.POINTER(Config), C.c_uint32, size)
swap = bind('swap16', None, ptr, ptr, size)

def checked(status):
    assert status == 0, status

def unswap(data):
    b = bytearray(data)
    b[0::2], b[1::2] = b[1::2], b[0::2]
    return b

def golden(name, rate, mapping, schedule):
    config = Config(rate, 2, 0, 0)
    s = create(C.byref(config), 1032)
    assert s
    map_one(s, mapping)
    out = C.create_string_buffer(1032)
    pcm = (ROOT/f'tests/vectors/parity/mix{rate}.s32le').read_bytes()
    chunk = (rate//1000)*8
    packets = bytearray()
    states = []
    try:
        for at in range(0, len(pcm), chunk):
            data = C.create_string_buffer(pcm[at:at+chunk])
            n = size()
            checked(push(s, data, (len(data)-1)//4, out, 1032, C.byref(n)))
            if not n.value:
                continue
            packets.extend(unswap(out.raw[:n.value]))
            row = Row()
            checked(getrow(s, C.byref(row)))
            states.append((getlevel(s), row.period_ms*rate*2//1000, deficit(s)))
            next_packet = len(states)
            if next_packet in schedule:
                checked(request(s, schedule[next_packet]))
                assert apply_now(s) == 1
        expected = (ROOT/f'tests/vectors/levels/{name}.bin').read_bytes()
        if packets != expected:
            at = next((j for j,(a,b) in enumerate(zip(packets, expected)) if a != b), min(len(packets),len(expected)))
            raise AssertionError(f'{name}: mismatch packet {at//656}, byte {at%656}; lengths {len(packets)}/{len(expected)}')
        expected_states = [tuple(map(int, line.split()[3:])) for line in
                           (ROOT/f'tests/vectors/levels/{name}.sizes').read_text().splitlines()]
        assert states == expected_states, f'{name}: level/period/deficit mismatch'
        print('PASS', name, len(states), 'packets and stream state')
    finally:
        destroy(s)

schedule = {10:2, 20:1, 30:2, 40:3, 50:4}
golden('lv_48k_ref', 48000, 1, schedule)
golden('lv_48k_raw1', 48000, 0, schedule)
golden('lv_44k_ref', 44100, 1, schedule)
golden('lv_48k_fast', 48000, 1, {1:4,2:3,3:2,4:3,5:4,6:2,7:4,9:3,11:2,12:4,13:3})

config = Config(48000, 2, 0, 0)
invalid = Config(48000, 1, 0, 0)
out = C.create_string_buffer(1032)
pcm = (C.c_int32*4000)(*[(j*7919)%16777216-8388608 for j in range(4000)])

# A deliberately non-power-of-two ring and thirteen-sample pushes force blocks
# to cross its physical tail. Output must match the compatibility-sized queue.
def ring_capture(queue_samples):
    workspace_size = stream_workspace(queue_samples)
    storage = C.create_string_buffer(workspace_size + 15)
    address = (C.addressof(storage) + 15) & ~15
    handle = ptr()
    checked(stream_init(address, workspace_size, C.byref(config), 1032, queue_samples,
                        C.byref(handle)))
    raw = (ROOT/'tests/vectors/parity/mix48000.s32le').read_bytes()[:48000]
    samples = (C.c_int32*(len(raw)//4)).from_buffer_copy(raw)
    packets = bytearray()
    try:
        for at in range(0, len(samples), 13):
            count = min(13, len(samples)-at)
            written = size()
            source = C.cast(C.byref(samples, at*C.sizeof(C.c_int32)), ptr)
            checked(push(handle, source, count, out, 1032, C.byref(written)))
            if written.value:
                packets.extend(unswap(out.raw[:written.value]))
    finally:
        destroy(handle)
    return packets

assert stream_workspace(34) < stream_workspace(64)
small_ring = ring_capture(34)
large_ring = ring_capture(30720)
assert small_ring and small_ring == large_ring
print('PASS PCM ring wraparound with 13-sample pushes and 34-sample logical capacity')

assert not ec(C.byref(invalid), 364000, 656)
assert not ec(C.byref(config), 364000, 655)
assert packet_budget(0) == 128 and packet_budget(1040) == 1032
row = Row()
assert table_row(C.byref(config), None, 128, 3, C.byref(row)) == -6
assert table_row(C.byref(config), None, 1032, 0, C.byref(row)) == -1

# Output/argument rejection must not advance state; separate instances and reset agree.
a, b = ec(C.byref(config), 364000, 656), ec(C.byref(config), 364000, 656)
assert a and b
try:
    p, q = Progress(), Progress()
    assert encode(a, pcm, 4000, None, 0, out, 655, C.byref(p)) == -3
    assert p.consumed == p.written == 0
    bad = (C.c_int32*2)(8388608, 0)
    assert encode(a, bad, 2, None, 0, out, 1032, C.byref(p)) == -1
    checked(encode(a, pcm, 7, C.byref(pcm, 7*C.sizeof(C.c_int32)), 3993,
                   out, 1032, C.byref(p)))
    want = out.raw[:p.written]
    checked(encode(b, pcm, 4000, None, 0, out, 1032, C.byref(q)))
    assert (p.consumed, p.written, want) == (q.consumed, q.written, out.raw[:q.written])
    assert reset(a, C.byref(invalid), 364000, 656) == -1
    checked(reset(a, C.byref(config), 364000, 656))
    checked(encode(a, pcm, 4000, None, 0, out, 1032, C.byref(p)))
    assert want == out.raw[:p.written]
finally:
    ed(a); ed(b)

s = create(C.byref(config), 1032)
try:
    n = size()
    assert push(s, pcm, 4000, out, 655, C.byref(n)) == -3
    assert push(s, pcm, 30721, out, 1032, C.byref(n)) == -4
    assert request(s, 0) == -1
    checked(request(s, 1))
    hold(s, 1)
    assert apply_now(s) == 0
    hold(s, 0)
    assert apply_now(s) == 1 and getlevel(s) == 2
finally:
    destroy(s)
x = C.create_string_buffer(bytes(range(5)), 5)
swap(x, x, 5)
assert x.raw == bytes([1,0,3,2,4])
print('PASS API validation, independent state, reset, output capacity, queue bounds and swap16')
