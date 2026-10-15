"""Independently verify actual GL display pixels against a saved Phoenix pair."""
import argparse,hashlib,json,subprocess,os,shutil
from pathlib import Path
import numpy as np
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument('--serial',required=True)
parser.add_argument('--adb',default=os.environ.get('ADB') or shutil.which('adb'),help='ADB executable; defaults to ADB environment variable or PATH')
parser.add_argument('--output',required=True)
parser.add_argument('--phone-screenshot',action='store_true')
parser.add_argument('--capture-folder',help='Saved inference folder, for example vulkan_1')
args=parser.parse_args()
if not args.adb: parser.error('Install platform-tools and put adb on PATH, or pass --adb')
out=Path(args.output);out.mkdir(parents=True,exist_ok=False)
adb=[args.adb,'-s',args.serial]
if args.phone_screenshot:
    status=json.loads(subprocess.check_output(adb+['shell','cat','/sdcard/Android/data/org.phoenix.museum/files/status.json']))
    folder=args.capture_folder or status.get('backend','cpu')+'_1'
    assert folder.startswith(('cpu_','vulkan_')) and folder.split('_')[-1].isdigit()
    for name in ['frame.json','input.rgb32f','output.rgb32f']:
        data=subprocess.check_output(adb+['exec-out','run-as','org.phoenix.museum','cat','files/captures/'+folder+'/'+name])
        (out/name).write_bytes(data)
    assert status['config']['view']==0 and status['config']['same_resolution'] and status['config']['freeze']
    assert status['displayed_sides'][1]['content']=='phoenix'
    subprocess.run(adb+['shell','screencap','-p','/sdcard/phoenix-display-verify.png'],check=True)
    subprocess.run(adb+['pull','/sdcard/phoenix-display-verify.png',str(out/'screen.png')],check=True,capture_output=True)
    screen=np.asarray(Image.open(out/'screen.png').convert('RGB'));h,w=screen.shape[:2]
    row=json.loads((out/'frame.json').read_text());sw,sh=row['width'],row['height']
    assert row['config']['backend']==status['backend']
    assert (sw,sh)==(status['processing_width'],status['processing_height'])
    for source,target in [('input','original'),('output','processed')]:
        values=np.fromfile(out/(source+'.rgb32f'),np.float32).reshape(sh,sw,3)
        rgba=np.full((sh,sw,4),255,np.uint8);rgba[:,:,:3]=np.uint8(np.clip(values,0,1)*255+.5);(out/(target+'.rgba')).write_bytes(rgba.tobytes())
    rgba=np.full((h,w,4),255,np.uint8);rgba[:,:,:3]=screen
    (out/'display.rgba').write_bytes(rgba[::-1].tobytes())
    (out/'proof.json').write_text(json.dumps({'width':w,'height':h,'input_width':sw,'input_height':sh,'view':0,'same_resolution':True,'result_serial':status['displayed_sides'][1]['serial']}))
    (out/'status.json').write_text(json.dumps(status,indent=2))
else:
    for name in ['proof.json','display.rgba','original.rgba','processed.rgba']:
        data=subprocess.check_output(adb+['exec-out','run-as','org.phoenix.museum','cat','files/captures/display_proof/'+name])
        (out/name).write_bytes(data)
proof=json.loads((out/'proof.json').read_text())
assert proof['same_resolution'] and proof['view']==0
w,h,sw,sh=[proof[k] for k in ['width','height','input_width','input_height']]
actual=np.fromfile(out/'display.rgba',np.uint8).reshape(h,w,4)[::-1,:,:3].copy()
left=np.fromfile(out/'original.rgba',np.uint8).reshape(sh,sw,4)[:,:,:3]
right=np.fromfile(out/'processed.rgba',np.uint8).reshape(sh,sw,4)[:,:,:3]

def linear(image):
    x=(np.arange(w,dtype=np.float64)+.5)*sw/w-.5
    y=(np.arange(h,dtype=np.float64)+.5)*sh/h-.5
    ix=np.floor(x).astype(int);iy=np.floor(y).astype(int)
    fx=(x-ix)[None,:,None];fy=(y-iy)[:,None,None]
    x0=ix.clip(0,sw-1);x1=(ix+1).clip(0,sw-1)
    y0=iy.clip(0,sh-1);y1=(iy+1).clip(0,sh-1)
    a=image[y0[:,None],x0[None,:]].astype(np.float64)
    b=image[y0[:,None],x1[None,:]].astype(np.float64)
    c=image[y1[:,None],x0[None,:]].astype(np.float64)
    d=image[y1[:,None],x1[None,:]].astype(np.float64)
    return np.uint8(np.clip((a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy+.5,0,255))

a,b=linear(left),linear(right)
expected=np.where(((np.arange(w)+.5)/w<.5)[None,:,None],a,b)
divider=np.abs((np.arange(w)+.5)/w-.5)<.001
expected[:,divider]=[184,107,255]
error=np.abs(actual.astype(np.int16)-expected.astype(np.int16))
checked=error[int(h*.24):int(h*.78)] if args.phone_screenshot else error
report={
    'actual_display_sha256':hashlib.sha256(actual.tobytes()).hexdigest(),
    'width':w,'height':h,'processing_width':sw,'processing_height':sh,
    'maximum_channel_error':int(checked.max()),'mean_channel_error':float(checked.mean()),
    'channels_outside_two_levels':int(np.count_nonzero(checked>2)),
    'checked_region':'full-width scene strip from 24% to 78% height; excludes Android HUD' if args.phone_screenshot else 'entire GL display',
    'original_processed_mean_byte_difference':float(np.abs(left.astype(float)-right).mean()),
    'original_processed_changed_channels':int(np.count_nonzero(left!=right)),
    'result_serial':proof['result_serial'],
    'right_side_displays_saved_phoenix_result':bool(np.all(checked<=2)),
}
(out/'verification.json').write_text(json.dumps(report,indent=2))
Image.fromarray(actual).save(out/'display.png')
Image.fromarray(a).save(out/'original.png')
Image.fromarray(b).save(out/'processed.png')
Image.fromarray(np.uint8(np.minimum(np.abs(a.astype(int)-b.astype(int))*6,255))).save(out/'difference-x6.png')
print(json.dumps(report,indent=2))
assert report['right_side_displays_saved_phoenix_result'],'Displayed pixels differ from the actual saved Phoenix output'
