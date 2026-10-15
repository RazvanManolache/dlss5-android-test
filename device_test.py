"""Bounded smoke test of the installed app; retains actual device receipts."""
import argparse,json,subprocess,time,os,shutil
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--serial',required=True)
parser.add_argument('--adb',default=os.environ.get('ADB') or shutil.which('adb'),help='ADB executable; defaults to ADB environment variable or PATH')
parser.add_argument('--options',required=True,help='JSON file with app settings overrides')
parser.add_argument('--output',required=True)
parser.add_argument('--timeout',type=int,default=180)
args=parser.parse_args()
if not args.adb: parser.error('Install platform-tools and put adb on PATH, or pass --adb')
out=Path(args.output);out.mkdir(parents=True,exist_ok=False)
options=json.loads(Path(args.options).read_text())
options['test_run_id']=str(time.time_ns())
def adb(*commands,check=True):
    return subprocess.run([str(args.adb),'-s',args.serial,*commands],capture_output=True,text=True,check=check).stdout
text=json.dumps(options,separators=(',',':'))
print(adb('shell','am','start','-S','-f','0x10008000','-n','org.phoenix.museum/.MainActivity','--es','test_options',"'"+text+"'"),flush=True)
deadline=time.monotonic()+args.timeout
previous=''
while time.monotonic()<deadline:
    raw=adb('shell','cat','/sdcard/Android/data/org.phoenix.museum/files/status.json',check=False)
    try: status=json.loads(raw)
    except ValueError: time.sleep(1);continue
    # Ignore the prior process's report while the replacement starts.
    cfg=status.get('config',{})
    if any(cfg.get(k)!=v for k,v in options.items()): time.sleep(1);continue
    if status.get('scene_objects')!=316 or status.get('display_fps',0)<=0: time.sleep(1);continue
    state=status.get('state','')
    if state!=previous: print(state,status.get('error',''),flush=True);previous=state
    (out/'latest.json').write_text(json.dumps(status,indent=2))
    if state=='error' or status.get('processed_frames',0)>0 or (not options.get('left_phoenix',0) and not options.get('right_phoenix',0)):
        break
    time.sleep(2)
else: raise SystemExit('Timed out waiting for a completed model call')
adb('shell','screencap','-p','/sdcard/phoenix-museum-test.png')
adb('pull','/sdcard/phoenix-museum-test.png',str(out/'screen.png'))
pid=adb('shell','pidof','org.phoenix.museum').strip().split()[0]
(out/'logcat.txt').write_text(adb('shell','logcat','-d','--pid='+pid,'-s','PhoenixMuseum','AndroidRuntime'))
print('Receipts:',out,flush=True)

if status.get('state')=='error': raise SystemExit('Device reported: '+status.get('error','unknown error'))
