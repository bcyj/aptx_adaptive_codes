"""Build component libraries, the combined library and CLI without CMake."""
import argparse
from pathlib import Path
import os
import shlex
import subprocess

root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser()
ap.add_argument('--cc',default=os.environ.get('CC','gcc'))
ap.add_argument('--ar',default=os.environ.get('AR','ar'))
ap.add_argument('--debug',action='store_true')
ap.add_argument('--sanitize-trap',action='store_true')
ap.add_argument('--no-crt',action='store_true',help='Freestanding codec; host callbacks required')
ap.add_argument('--external-allocator-only',action='store_true',help='Disable the default CRT heap')
ap.add_argument('--driver-resources',action='store_true',help='Bounded nonpaged workspaces, fixed decoder, no processing allocations')
ap.add_argument('--no-encoder',action='store_true')
ap.add_argument('--no-decoder',action='store_true')
ap.add_argument('--no-bundle',action='store_true',help='Skip the combined DLL/SO')
ap.add_argument('--no-tools',action='store_true',help='Skip command-line tools')
ap.add_argument('--audio-common-root',type=Path,default=root.parent/'audio_codec_common')
backend=ap.add_mutually_exclusive_group()
backend.add_argument('--decoder-fixed-point',dest='decoder_fixed_point',action='store_true',help='Use integer Q29/64-bit PCM synthesis (default)')
backend.add_argument('--decoder-float',dest='decoder_fixed_point',action='store_false',help='Use the reference double PCM synthesis')
ap.set_defaults(decoder_fixed_point=True)
ap.add_argument('--output-dir',type=Path,default=root/'build')
args=ap.parse_args();build=args.output_dir.resolve();build.mkdir(parents=True,exist_ok=True)
if args.driver_resources:
    if args.debug or args.sanitize_trap or not args.decoder_fixed_point:
        ap.error('--driver-resources requires optimized fixed-point, without debug/sanitizers')
    args.no_crt=True
    args.external_allocator_only=True
if args.no_crt:
    args.external_allocator_only=True
args.audio_common_root=args.audio_common_root.resolve()
math_root=args.audio_common_root/'math'
core_root=args.audio_common_root/'core'
if not (math_root/'include/audio_math/integer.h').is_file() or not (core_root/'include/audio_common/ring_buffer.h').is_file():
    ap.error('Set --audio-common-root to the audio_codec_common source tree')
flags=['-std=c11','-Wall','-Wextra','-Wpedantic','-g','-O0' if args.debug else '-O2']
flags+=['-DAPTX_AA_DECODER_FIXED_POINT='+str(int(args.decoder_fixed_point))]
flags+=['-DAPTX_AA_NO_CRT='+str(int(args.no_crt))]
flags+=['-DAPTX_AA_DRIVER_RESOURCES='+str(int(args.driver_resources))]
flags+=['-DAPTX_AA_EXTERNAL_ALLOCATOR_ONLY='+str(int(args.external_allocator_only))]
if args.no_crt:flags+=['-DAUDIO_MATH_NO_CRT=1']
if args.driver_resources:flags+=['-DNDEBUG']
if args.no_crt:
    if args.sanitize_trap:ap.error('--no-crt cannot link sanitizer runtime')
flags+=['-I'+str(math_root/'include'),'-I'+str(core_root/'include')]
for folder in ['include']+[f'aptx/{p}/{d}' for p in ('common','encoder','decoder') for d in ('include','src/include')]:
    flags+=['-I'+str(root/folder)]
if os.name!='nt':flags+=['-fPIC']
if args.sanitize_trap:flags+=['-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
core_flags=flags+(['-fno-stack-protector'] if args.no_crt else [])
if args.driver_resources:core_flags+=['-fstack-usage','-Werror=frame-larger-than=512']
def run(cmd):
    print(shlex.join(cmd),flush=True);subprocess.run(cmd,cwd=root,check=True)
def archive(name,objects):
    target=build/name;temp=target.with_suffix('.next.a')
    assert temp.parent==build
    if temp.exists():temp.unlink()
    run([args.ar,'rcs',str(temp),*objects]);temp.replace(target);return str(target)
objects={}
selected={'common'}
if not args.no_encoder:selected.add('encoder')
if not args.no_decoder:selected.add('decoder')
components={
    'common':[
        'aptx/common/src/api/runtime.c',
        'aptx/common/src/api/util.c',
        'aptx/common/src/codec/params.c',
        'aptx/common/src/dsp/engine.c',
        'aptx/common/src/dsp/predict.c',
        'aptx/common/src/tables/codec_tables.c',
    ],
    'encoder':[
        'aptx/encoder/src/api/encoder.c',
        'aptx/encoder/src/codec/analysis.c',
        'aptx/encoder/src/codec/quantize.c',
        'aptx/encoder/src/codec/allocate.c',
        'aptx/encoder/src/codec/pack.c',
        'aptx/encoder/src/stream/stream.c',
        'aptx/encoder/src/stream/mode_tables.c',
    ],
    'decoder':[
        'aptx/decoder/src/api/decoder.c',
        'aptx/decoder/src/codec/bitstream.c',
        'aptx/decoder/src/codec/reconstruct.c',
        'aptx/decoder/src/dsp/synthesis.c',
        'aptx/decoder/src/dsp/synthesis_tables.c',
        'aptx/decoder/src/dsp/decoder_trees.c',
    ],
}
for part,sources in components.items():
    if part not in selected:continue
    objects[part]=[]
    paths=[root/source for source in sources]
    if part=='common':
        paths[0:0]=[core_root/'src/memory.c',core_root/'src/ring_buffer.c']
    for source in paths:
        try:label=source.relative_to(root).as_posix()
        except ValueError:label='audio_common/'+source.name
        obj=build/(label.replace('/','_').removesuffix('.c')+'.o')
        run([args.cc,*core_flags,'-c',str(source),'-o',str(obj)]);objects[part].append(str(obj))
libraries={p:archive('libaptx_aa_'+p+'.a',o) for p,o in objects.items()}
if not args.no_bundle:
    archive('libaptx_aa.a',[obj for group in objects.values() for obj in group])
suffix='.exe' if os.name=='nt' else ''
if not args.no_bundle:
    dll=build/('aptx_aa.dll' if os.name=='nt' else 'libaptx_aa.so')
    link=['-shared']
    if args.sanitize_trap:link+=['-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
    if os.name=='nt':link+=['-Wl,--export-all-symbols','-Wl,--out-implib,'+str(build/'libaptx_aa.dll.a')]
    run([args.cc,*link,*[obj for group in objects.values() for obj in group],'-o',str(dll)])
if not args.no_tools:
    for part,name in [('encoder','encode'),('decoder','decode')]:
        if part in libraries:
            run([args.cc,*flags,str(root/f'tools/cli/aptx_aa_{name}.c'),libraries[part],libraries['common'],
                 '-o',str(build/f'aptx_aa_{name}{suffix}')])
print('Built components and executables in',build)
