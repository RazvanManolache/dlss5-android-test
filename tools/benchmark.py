"""Measure a local/private fixture with Phoenix's portable host-plane C API."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for flag in ['frame-exe','runtime','input','motion','model']:
        parser.add_argument('--'+flag,type=Path,required=True)
    parser.add_argument('--model-sha256',required=True)
    parser.add_argument('--device',required=True,help='Explicit Phoenix Vulkan device ID')
    parser.add_argument('--width',type=int,default=320)
    parser.add_argument('--height',type=int,default=144)
    parser.add_argument('--output',type=Path,required=True,help='New ignored build/ or evidence/ folder')
    parser.add_argument('--expected-output-sha256')
    args=parser.parse_args()
    if not 1<=args.width<=8192 or not 1<=args.height<=8192:
        parser.error('Width and height must be 1..8192')
    if args.input.stat().st_size != args.width*args.height*12 or args.motion.stat().st_size != args.width*args.height*8:
        parser.error('RGB32F / RG32F planes do not match the specified extent')
    if sha(args.model)!=args.model_sha256:
        parser.error('Model hash mismatch')
    args.output.mkdir(parents=True,exist_ok=False)
    config={'allow_unvalidated':True,'backend':'vulkan','device':args.device,'runtime_root':str(args.runtime.resolve()),
        'vulkan_workgroup_compatibility':True,'profiling':'frame','cpu_threads':4,
        'weight_source':{'kind':'nvidia_dll','path':str(args.model.resolve()),'sha256':args.model_sha256}}
    controls={'auto_mask':True,'intensity':1.0,'local_structure':1.0,'local_tone':1.0,'seed':0,'skin_structure':-1.0,'style':0}
    job={'width':args.width,'height':args.height,'config':config,'benchmark_iterations':7,
         'color':str(args.input.resolve()),'motion':str(args.motion.resolve()),
         'metadata':{'controls':controls,'has_motion':True,'passes':1,'repeats':[1]*8,'reset':True}}
    path=args.output/'private-job.json';path.write_text(json.dumps(job))
    output=args.output/'output.rgb32f'
    with (args.output/'private-frames.jsonl').open('w') as log:
        subprocess.run([str(args.frame_exe.resolve()),str(path.resolve()),str(output.resolve())],stdout=log,check=True)
    rows=[json.loads(s) for s in (args.output/'private-frames.jsonl').read_text().splitlines()]
    if len(rows)!=7 or any(r.get('state')!='completed' for r in rows):
        raise RuntimeError('Expected seven completed calls')
    digest=sha(output)
    if args.expected_output_sha256 and digest!=args.expected_output_sha256:
        raise RuntimeError('Output differs from the supplied reference hash')
    result={'width':args.width,'height':args.height,'cold_call_ms':rows[0]['call_ms'],
        'warm_call_ms':[r['call_ms'] for r in rows[1:]],'median_call_ms':statistics.median(r['call_ms'] for r in rows[1:]),
        'output_sha256':digest,'input_sha256':sha(args.input),'motion_sha256':sha(args.motion),'model_sha256':args.model_sha256,
        'scope':'Seven synchronous host-plane calls, first excluded; one pass, eight blocks once, history reset; not display FPS'}
    (args.output/'sanitized-results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    main()
