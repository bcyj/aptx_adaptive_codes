"""Decode independent encoder captures and compare every reconstructed stage."""
import argparse
import ctypes as C
import json
import math
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
ap=argparse.ArgumentParser();ap.add_argument('library',type=Path);args=ap.parse_args()
lib=C.CDLL(str(args.library.resolve()))
import runpy
constructor=runpy.run_path(str(ROOT/'tests/support/allocator.py'))['constructor']
ptr=C.c_void_p;size=C.c_size_t;i32=C.c_int32
def bind(name,result,*types):
    f=getattr(lib,name);f.restype=result;f.argtypes=list(types);return f
create=constructor(lib,'aptx_aa_decoder_create_variant',C.c_uint,C.c_uint32)
destroy=bind('aptx_aa_decoder_destroy',None,ptr)
push=bind('aptx_aa_decoder_push',C.c_int,ptr,ptr,size,ptr,size,C.POINTER(size))
finish=bind('aptx_aa_decoder_finish',C.c_int,ptr,ptr,size,C.POINTER(size))
reset=bind('aptx_aa_decoder_reset',C.c_int,ptr)
delay=bind('aptx_aa_decoder_delay',C.c_uint,ptr)
fixed=bool(bind('aptx_aa_decoder_fixed_point',C.c_int)())
TRACE=C.CFUNCTYPE(None,ptr,C.POINTER(C.c_uint8),C.POINTER(i32),C.POINTER(i32),C.POINTER(i32),C.POINTER(i32),C.c_uint)
set_trace=bind('aa_decoder_set_trace',None,ptr,TRACE,ptr)
class Params(C.Structure):
    _fields_=[('variant',C.c_uint),('rate',C.c_uint32),('bands',C.c_uint),('width',C.c_uint)]
class Synthesis(C.Structure):
    _fields_=[('fir',ptr),('bands',C.c_uint),('taps',C.c_uint),('pos',C.c_uint),('overlap',((C.c_int64 if fixed else C.c_double)*512)*2)]
synth_init=bind('aa_synthesis_init',None,C.POINTER(Synthesis),Params)
synth_run=bind('aa_synthesis_run',None,C.POINTER(Synthesis),ptr,ptr)
engine_init=bind('aa_engine_init',None,ptr,Params)
analyse=bind('aa_analyse',None,ptr,ptr,ptr)

def ints(path):
    b=path.read_bytes();return struct.unpack('<'+'i'*(len(b)//4),b)
def swapped(data):
    b=bytearray(data);b[0::2],b[1::2]=b[1::2],b[0::2];return bytes(b)
def metric(source,output,lag,skip=512):
    n=min(len(source),len(output)//2-lag)
    if n<=skip:return None
    a=source[skip:n];b=output[2*(skip+lag):2*(n+lag):2]
    power=sum(v*v for v in a);error=sum((v-w)**2 for v,w in zip(a,b))
    gain=sum(v*w for v,w in zip(a,b))/power if power else 0
    return (10*math.log10(power/max(error,1)),gain)

def decode(data,packet,variant,rate,trace=None,wire=False):
    d=create(variant,rate);assert d,'decoder create failed'
    out=(i32*1048576)();output=[]
    cb=None
    if trace:
        cb=TRACE(trace);set_trace(d,cb,None)
    try:
        lag=delay(d)
        for at in range(0,len(data),packet):
            b=data[at:at+packet];b=swapped(b) if wire else b
            p=C.create_string_buffer(b);n=size()
            status=push(d,p,len(b),out,len(out),C.byref(n))
            assert status==0,f'packet {at//packet}: status {status}'
            output.extend(out[:n.value])
        n=size();status=finish(d,out,len(out),C.byref(n))
        assert status==0,f'finish status {status}'
        output.extend(out[:n.value])
        assert finish(d,out,len(out),C.byref(n))==0 and n.value==0
        return output,lag
    finally:destroy(d)

report={'fixed_point':fixed,'stages':[],'parity':[],'synthesis':[]}
for folder in sorted((ROOT/'tests/vectors/stages').iterdir()):
    if not folder.is_dir():continue
    name=folder.name;variant=int(name[3]);rate=96000 if '96k' in name else 48000
    meta=json.loads((folder/'meta.json').read_text());w=meta['subbands_per_channel']*2
    got={'codes':[],'recon':[],'pred':[],'bits':[],'sub':[]}
    def trace(_,bits,q,recon,pred,sub,width):
        got['bits'].extend(bits[:width]);got['codes'].extend(q[:width]);got['recon'].extend(recon[:width])
        got['pred'].extend(pred[:width]);got['sub'].extend(sub[:width])
    output,lag=decode((folder/'packets.bin').read_bytes(),meta['packet_len'],variant,rate,trace)
    for key,file in [('codes','quant_codes.s32'),('recon','quant_recon.s32'),('pred','pred_out.s32')]:
        expected=ints(folder/file);actual=got[key]
        assert len(actual)>w*100 and len(actual)<=len(expected)
        if actual!=list(expected[:len(actual)]):
            at=next(j for j,(a,b) in enumerate(zip(actual,expected)) if a!=b)
            raise AssertionError(f'{name} {key}: block {at//w} band {at%w}: {actual[at]} != {expected[at]}')
    expected_bits=[]
    for frame in json.loads((folder/'frames.json').read_text()):
        expected_bits.extend(frame['bits']*frame['blocks'])
    assert got['bits']==expected_bits[:len(got['bits'])],name+' allocations'
    predin=ints(folder/'pred_in.s32');recon=ints(folder/'quant_recon.s32')
    expected_sub=[max(-8388608,min(8388607,a+b)) for a,b in zip(predin,recon)]
    assert got['sub']==expected_sub[:len(got['sub'])],name+' reconstructed subbands'
    quality=metric(ints(folder/'pcm_in.s32')[0::2],output,lag)
    print('PASS stages',name,len(got['codes'])//w,'blocks; PCM SNR/gain',quality)
    # Lossy allocation can omit bands (cfg1 always omits the top band; 96 kHz
    # at this bitrate also removes upper bands). Report quality rather than
    # assuming unit broadband gain. The synthesis-only test below verifies
    # unity gain separately; inverse-quantizer/predictor checks remain exact.
    assert quality[0]>0 and quality[1]>0
    report['stages'].append({'name':name,'decoded_blocks':len(got['codes'])//w,'snr_db':quality[0],'gain':quality[1]})

    # Isolate the synthesis stage from lossy quantization/prediction.
    s=Synthesis();synth_init(C.byref(s),Params(variant,rate,w//2,w));raw=ints(folder/'analysis_out.s32')
    b=(i32*w)();o=(i32*w)();y=[]
    for at in range(0,len(raw),w):
        for j in range(w): b[j]=raw[at+j]>>8
        synth_run(C.byref(s),b,o);y.extend(o)
    quality=metric(ints(folder/'pcm_in.s32')[0::2],y,lag)
    assert quality[0]>75 and abs(quality[1]-1)<0.001,(name,'synthesis',quality)
    right=metric(ints(folder/'pcm_in.s32')[1::2],y[1:]+[0],lag)
    assert right[0]>75 and abs(right[1]-1)<0.001,(name,'right synthesis',right)
    print('PASS synthesis',name,'delay',lag,'SNR/gain',quality)
    report['synthesis'].append({'name':name,'delay':lag,'snr_db':quality[0],'gain':quality[1]})

# The stage captures have no cfg4 analysis trace. Test its short bank directly,
# including every decimation phase, and cover the 88.2 kHz HQ bank as well.
for variant,rate in [(0,44100),(1,44100),(3,88200),(4,44100),(4,48000)]:
    bands=4 if variant<2 else 8;w=bands*2;p=Params(variant,rate,bands,w)
    engine=C.create_string_buffer(65536) # >= sizeof(AAEngine), naturally heap aligned
    engine_init(engine,p);s=Synthesis();synth_init(C.byref(s),p)
    x=(i32*w)();sub=(i32*w)();o=(i32*w)();source=[];result=[];state=0x12345678
    for block in range(1024):
        for j in range(w):
            state=(state*1664525+1013904223)&0xffffffff
            x[j]=((state if state<0x80000000 else state-0x100000000)>>12)
        source.extend(x[0::2]);analyse(engine,x,sub)
        for j in range(w):sub[j]>>=8
        synth_run(C.byref(s),sub,o);result.extend(o)
    quality=metric(source,result,s.taps-bands)
    assert quality[0]>75 and abs(quality[1]-1)<0.001,(variant,rate,quality)
    print('PASS derived filterbank config/rate',variant,rate,'SNR',round(quality[0],2))
    report['synthesis'].append({'name':f'cfg{variant}_{rate}_generated','delay':s.taps-bands,'snr_db':quality[0],'gain':quality[1]})

for line in (ROOT/'tests/vectors/parity/runs.txt').read_text().splitlines():
    if not line.strip() or line.startswith('#'):continue
    name,level,profile,rate,bitrate,ms,packet,chunk,ll=line.split();rate=int(rate);packet=int(packet)
    variant=1 if profile=='1' else (3 if rate>48000 or (level=='R2' and ll=='0') else (4 if level=='R2' else 0))
    data=(ROOT/f'tests/vectors/parity/{name}.bin').read_bytes()
    pcm,lag=decode(data,packet,variant,rate)
    wire_pcm,_=decode(data,packet,variant,rate,wire=True)
    assert pcm==wire_pcm,name+' swapped presentation'
    quality=metric(ints(ROOT/f'tests/vectors/parity/mix{rate}.s32le')[0::2],pcm,lag)
    assert quality[0]>0 and 0<quality[1]<1.4,(name,quality)
    print('PASS packets',name,len(pcm)//2,'frames; SNR',round(quality[0],2))
    report['parity'].append({'name':name,'frames':len(pcm)//2,'snr_db':quality[0]})

for name,rate in [('lv_48k_ref',48000),('lv_48k_raw1',48000),('lv_44k_ref',44100),('lv_48k_fast',48000)]:
    data=(ROOT/f'tests/vectors/levels/{name}.bin').read_bytes()
    pcm,lag=decode(data,656,0,rate)
    quality=metric(ints(ROOT/f'tests/vectors/parity/mix{rate}.s32le')[0::2],pcm,lag)
    assert quality[0]>0
    print('PASS decoding quality-level changes:',name)

# Capacity retry and malformed-header rejection preserve decoder state.
data=(ROOT/'tests/vectors/parity/c0_48k_L3.bin').read_bytes();d=create(0,48000)
try:
    n=size();out=(i32*10000)();first=C.create_string_buffer(data[:656]);second=C.create_string_buffer(data[656:1312])
    assert push(d,first,656,None,0,C.byref(n))==0 and n.value==0
    assert push(d,second,656,None,0,C.byref(n))==-3 and n.value>0
    required=n.value
    guard=(i32*(required+1))();sentinel=-0x1234567
    guard[0]=guard[required]=sentinel
    assert push(d,second,656,C.byref(guard,C.sizeof(i32)),required-1,C.byref(n))==-3
    assert n.value==required and guard[0]==guard[required]==sentinel
    bad=C.create_string_buffer(bytes([1])+data[657:1312])
    assert push(d,bad,656,out,len(out),C.byref(n))==-7 and n.value==0
    assert push(d,second,656,out,len(out),C.byref(n))==0 and n.value==required
    baseline=tuple(out[:n.value]);assert reset(d)==0
    assert push(d,first,656,out,len(out),C.byref(n))==0
    assert push(d,second,656,out,len(out),C.byref(n))==0 and tuple(out[:n.value])==baseline
finally:destroy(d)
print('PASS decoder output capacity retry, malformed header and reset')
(ROOT/'build').mkdir(exist_ok=True)
(args.library.resolve().parent/'decoder-results.json').write_text(json.dumps(report,indent=2))
