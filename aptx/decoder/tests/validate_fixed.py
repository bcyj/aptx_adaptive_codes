"""Independent arbitrary-precision FIR reference, rail stress and PCM parity.

Run with the fixed library; optionally compare full reference streams against
the double library. No third-party Python packages are required.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import re
import runpy
import subprocess

ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser()
p.add_argument('library',type=Path)
p.add_argument('--float-library',type=Path)
p.add_argument('--report',type=Path)
args=p.parse_args();lib=C.CDLL(str(args.library.resolve()))
constructor=runpy.run_path(str(ROOT/'tests/support/allocator.py'))['constructor']
i32=C.c_int32;ptr=C.c_void_p;size=C.c_size_t
def bind(library,name,result,*types):
    f=getattr(library,name);f.restype=result;f.argtypes=list(types);return f
assert bind(lib,'aptx_aa_decoder_fixed_point',C.c_int)()==1
class Params(C.Structure):
    _fields_=[('variant',C.c_uint),('rate',C.c_uint32),('bands',C.c_uint),('width',C.c_uint)]
class Synth(C.Structure):
    _fields_=[('fir',C.POINTER(i32)),('bands',C.c_uint),('taps',C.c_uint),('pos',C.c_uint),('overlap',(C.c_int64*512)*2)]
init=bind(lib,'aa_synthesis_init',None,C.POINTER(Synth),Params)
run=bind(lib,'aa_synthesis_run',None,C.POINTER(Synth),ptr,ptr)
derived=runpy.run_path(str(ROOT/'aptx/decoder/tests/synthesis_reference.py'))
banks={0:derived['tree'](16),1:derived['tree'](12),3:derived['cosine'](256),4:derived['cosine'](128)}
Q=1<<29;BIAS=Q//2
sat=lambda x:max(-8388608,min(8388607,x))
report={'fixed_library_sha256':hashlib.sha256(args.library.read_bytes()).hexdigest(),'synthesis':[],'stream_parity':[]}
for variant,h in banks.items():
    s=Synth();n=len(h);t=len(h[0]);params=Params(variant,48000,n,2*n)
    init(C.byref(s),params);assert s.taps==t and s.bands==n
    coefficients=[[s.fir[k*t+lag] for lag in range(t)] for k in range(n)]
    assert coefficients==[[math.floor(v*Q+.5) for v in row] for row in h]
    l1=max(sum(abs(row[lag]) for row in coefficients for lag in range(phase,t,n)) for phase in range(n))
    error=max(sum(abs(h[k][lag]-coefficients[k][lag]/Q) for k in range(n) for lag in range(phase,t,n)) for phase in range(n))*8388608
    assert l1*8388608+BIAS<2**63
    # Test exact ties and clipping in isolation, including negative ties.
    for value in [j*Q+d for j in (-8388609,-8388608,-2,-1,0,1,2,8388606,8388607,8388608) for d in (-BIAS,-BIAS+1,0,BIAS-1,BIAS)]:
        init(C.byref(s),params);s.overlap[0][0]=value;s.overlap[1][0]=-value
        x=(i32*(2*n))();y=(i32*(2*n))();run(C.byref(s),x,y)
        assert y[0]==sat((value+BIAS)>>29) and y[1]==sat((-value+BIAS)>>29)
    rng=random.Random(0xAA000+variant)
    blocks=[[0]*(2*n) for _ in range(t//n+2)]
    blocks += [[8388607]*(2*n),[-8388608]*(2*n)]*20
    blocks += [[rng.randrange(-8388608,8388608) for _ in range(2*n)] for _ in range(160)]
    # Align every coefficient sign to one selected output, for all output phases.
    for phase in range(n):
        blocks += [[0]*(2*n) for _ in range(t//n+2)]
        target=t-1+phase
        for b in range((target//n)+1):
            lag=b*n+t-1-target
            signs=[1 if 0<=lag<t and coefficients[k][lag]>=0 else -1 for k in range(n)]
            blocks.append([v for ch in range(2) for v in [sign*(8388607 if ch==0 else -8388608) for sign in signs]])
    blocks += [[0]*(2*n) for _ in range(t//n+2)]
    init(C.byref(s),params);state=[[0]*512 for _ in range(2)];pos=0;max_acc=0
    x=(i32*(2*n))();y=(i32*(2*n))()
    for block in blocks:
        expected=[0]*(2*n)
        for ch in range(2):
            for lag in range(t):
                acc=sum(coefficients[k][lag]*block[ch*n+k] for k in range(n))
                at=(pos+t-1-lag)&511;state[ch][at]+=acc
                max_acc=max(max_acc,abs(state[ch][at]));assert abs(state[ch][at])<=l1*8388608
            for j in range(n):
                at=(pos+j)&511;expected[2*j+ch]=sat((state[ch][at]+BIAS)>>29);state[ch][at]=0
        x[:]=block;run(C.byref(s),x,y)
        assert list(y)==expected,(variant,'independent integer PCM')
        assert all(list(s.overlap[ch])==state[ch] for ch in range(2)),(variant,'integer state')
        pos=(pos+n)&511;assert s.pos==pos
    assert not any(v for ch in state for v in ch),'flush must drain all overlap'
    report['synthesis'].append({'variant':variant,'blocks':len(blocks),'max_phase_L1_Q29':l1,
                               'int64_accumulator_bound':l1*8388608+BIAS,'observed_max_abs_accumulator':max_acc,
                               'coefficient_error_bound_PCM_LSB':error,
                               'rounded_PCM_difference_bound_LSB':math.ceil(error+0.0001)})
    print('PASS integer reference',variant,len(blocks),'blocks; bound',math.ceil(error+0.0001),'PCM LSB',flush=True)

def decode(library,data,packet,variant,rate):
    create=constructor(library,'aptx_aa_decoder_create_variant',C.c_uint,C.c_uint32)
    push=bind(library,'aptx_aa_decoder_push',C.c_int,ptr,ptr,size,ptr,size,C.POINTER(size))
    finish=bind(library,'aptx_aa_decoder_finish',C.c_int,ptr,ptr,size,C.POINTER(size))
    destroy=bind(library,'aptx_aa_decoder_destroy',None,ptr)
    d=create(variant,rate);assert d
    out=(i32*1048576)();result=[]
    try:
        for at in range(0,len(data),packet):
            b=C.create_string_buffer(data[at:at+packet]);used=size()
            assert push(d,b,len(b)-1,out,len(out),C.byref(used))==0
            result.extend(out[:used.value])
        used=size();assert finish(d,out,len(out),C.byref(used))==0
        result.extend(out[:used.value]);return result
    finally:destroy(d)

if args.float_library:
    floating=C.CDLL(str(args.float_library.resolve()))
    assert bind(floating,'aptx_aa_decoder_fixed_point',C.c_int)()==0
    report['float_library_sha256']=hashlib.sha256(args.float_library.read_bytes()).hexdigest()
    for line in (ROOT/'tests/vectors/parity/runs.txt').read_text().splitlines():
        if not line.strip() or line.startswith('#'):continue
        name,level,profile,rate,bitrate,ms,packet,chunk,ll=line.split();rate=int(rate);packet=int(packet)
        variant=1 if profile=='1' else (3 if rate>48000 or (level=='R2' and ll=='0') else (4 if level=='R2' else 0))
        data=(ROOT/f'tests/vectors/parity/{name}.bin').read_bytes()
        a=decode(lib,data,packet,variant,rate);b=decode(floating,data,packet,variant,rate)
        assert len(a)==len(b)
        delta=max(abs(x-y) for x,y in zip(a,b));bound=next(r['rounded_PCM_difference_bound_LSB'] for r in report['synthesis'] if r['variant']==variant)
        assert delta<=bound,(name,delta,bound)
        report['stream_parity'].append({'name':name,'samples':len(a),'max_PCM_difference_LSB':delta})
    print('PASS fixed/float',len(report['stream_parity']),'streams',flush=True)

# On the GCC build, inspect every decoder object for floating arithmetic.
objects=list(args.library.resolve().parent.glob('aptx_decoder_src_*.o'))
if objects:
    forbidden=set('addsd subsd mulsd divsd addss subss mulss divss addpd subpd mulpd divpd addps subps mulps divps cvttsd2si cvttss2si cvtsi2sd cvtsi2ss fsqrt fadd faddp fsub fsubp fmul fmulp fdiv fdivp'.split())
    found=[]
    for obj in objects:
        assembly=subprocess.check_output(['objdump','-d',str(obj)],text=True)
        opcodes=re.findall(r'\t([a-z][a-z0-9]*)\s',assembly)
        found += [(obj.name,op) for op in opcodes if op in forbidden]
    assert not found,found
    report['decoder_objects_without_floating_arithmetic']=len(objects)
target=args.report or args.library.resolve().parent/'fixed-results.json'
target.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('PASS fixed backend:',target,flush=True)
