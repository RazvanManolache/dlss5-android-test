"""Prepare a private Android test package from the maintained gallery assets.
No source assets or Phoenix model/backend code are modified.
"""
from pathlib import Path
import hashlib, json, shutil, struct
from PIL import Image

ROOT = Path(__file__).resolve().parent
GALLERY = ROOT / 'external/gallery'
PHOENIX = ROOT / 'external/phoenix'
ASSET_ROOT = GALLERY
RUNTIMES = {}
LOCAL = ROOT / 'local.json'
ASSETS = ROOT / 'app/src/main/assets'
CPP = ROOT / 'app/src/main/cpp'

def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)

def generate_scene():
    """Generate only C++ includes; no model or scene downloads needed."""
    CPP.mkdir(parents=True, exist_ok=True)
    catalogue = json.loads((GALLERY/'gallery_people.json').read_text())
    names = ['horse_statue_01','antique_ceramic_vase_01','chinese_armchair','vintage_video_camera']
    names += [p['name'] for p in catalogue['people']+catalogue['animals']]

    # Retain original museum construction and route/pose code directly.
    text=(GALLERY/'heavy_pipeline.inc').read_text()
    construction=text[text.index('void block('):text.index('void buildAcceleration(')]
    start=construction.index(' meshList[0]'); end=construction.index(' XMFLOAT4 stone',start)
    construction=construction[:start]+construction[end:]
    (CPP/'gallery_layout.inc').write_text('// Generated from external/gallery/heavy_pipeline.inc; original room layout.\n'+construction)
    text=(GALLERY/'visitor_scene.inc').read_text()
    (CPP/'gallery_animation.inc').write_text('// Generated from external/gallery/visitor_scene.inc; original routes and bone interpolation.\n'+text[text.index('struct VisitorState'):text.index('void animateGallery')])
    (CPP/'catalogue.inc').write_text('static const char* AssetNames[]={' + ','.join(json.dumps(n) for n in names)+'};\nconstexpr unsigned ActorCount=18,VisitorCount=16;\n')
    return names

def prepare():
    names = generate_scene()
    records = []
    ASSETS.mkdir(parents=True, exist_ok=True)
    for name in names:
        actor = names.index(name)>=4
        folder = ASSET_ROOT/'assets'/('people' if actor else 'variety')
        for kind in ['color','normal','rough']:
            source = folder/f'{name}_{kind}.rgba'
            b = source.read_bytes(); w,h = struct.unpack('<II',b[:8])
            im = Image.frombytes('RGBA',(w,h),b[8:])
            im.thumbnail((1024 if kind=='color' else 512,)*2, Image.Resampling.LANCZOS)
            dst = ASSETS/'scene'/f'{name}_{kind}.rgba'; dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(struct.pack('<II',*im.size)+im.tobytes())
            records.append({'source':str(source.relative_to(ASSET_ROOT)), 'source_sha256':hashlib.sha256(b).hexdigest(),'output':str(dst.relative_to(ASSETS)),'size':list(im.size)})
        if actor:
            folder=ASSET_ROOT/'assets/living_people'
            for suffix in ['.skin','.bones','_rig.json']:
                copy(folder/(name+suffix),ASSETS/'scene'/(name+suffix))
        else:
            for lod in ['high','low']:
                copy(folder/f'{name}_{lod}.mesh', ASSETS/'scene'/f'{name}_{lod}.mesh')
    for name in ['marble_high.mesh','marble_low.mesh','marble_color.rgba','marble_normal.rgba','marble_rough.rgba']:
        source=ASSET_ROOT/'base_assets'/name
        if name.endswith('.rgba'):
            b=source.read_bytes(); w,h=struct.unpack('<II',b[:8]); im=Image.frombytes('RGBA',(w,h),b[8:]); im.thumbnail((1024,1024),Image.Resampling.LANCZOS)
            (ASSETS/'scene'/name).write_bytes(struct.pack('<II',*im.size)+im.tobytes())
        else: copy(source,ASSETS/'scene'/name)
    copy(GALLERY/'CREDITS.md',ASSETS/'CREDITS.md')
    for p in (GALLERY/'licenses').glob('*'):
        if p.is_file(): copy(p, ASSETS/'licenses'/p.name)
    copy(PHOENIX/'LICENSE',ASSETS/'licenses/Phoenix-LICENSE')
    copy(PHOENIX/'NOTICE',ASSETS/'licenses/Phoenix-NOTICE')
    local=json.loads(LOCAL.read_text()); model=Path(local['weight_source']['path']).expanduser()
    if not model.is_absolute(): model=LOCAL.parent/model
    # A private device-test input, excluded from source control and distribution.
    digest=hashlib.sha256(model.read_bytes()).hexdigest()
    if digest != local['weight_source']['sha256']: raise ValueError('External model digest mismatch')
    copy(model,ASSETS/'model/nvngx_dlssnr.dll')
    copy(GALLERY/'gallery_people.json',ASSETS/'catalogue.json')
    (ASSETS/'model.json').write_text(json.dumps({'sha256':digest}))
    for abi,runtime in RUNTIMES.items():
        libs=ROOT/'app/src/main/jniLibs'/abi
        for file in (runtime/'lib').glob('*.so'): copy(file,libs/file.name)
        copy(runtime/'lib/vulkan/libphoenix_vulkan.so',libs/'libphoenix_vulkan.so')
        shaders=list((runtime/'lib/vulkan').glob('*.spv'))
        if not shaders: raise FileNotFoundError('Vulkan shader pack missing')
        for file in shaders: copy(file,ASSETS/'runtime/vulkan'/file.name)
        for name in ['LICENSE','NOTICE','THIRD_PARTY.md']:
            copy(runtime/name,ASSETS/'licenses'/('Phoenix-SDK-'+name))
        for file in (runtime/'licenses').rglob('*'):
            if file.is_file(): copy(file,ASSETS/'licenses/phoenix-sdk'/file.relative_to(runtime/'licenses'))
    (ASSETS/'provenance.json').write_text(json.dumps({'gallery_source':'phoenix-gallery','objects':316,'textures':'resized mobile derivatives','model_sha256':digest,'textures_provenance':records},indent=2))
    print('Prepared original museum assets, 18 animated actors, selected native backends and private model input.')

if __name__=='__main__':
    raise SystemExit('Use python build.py; see docs/BUILD.md')
