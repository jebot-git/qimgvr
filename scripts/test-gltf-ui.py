#!/usr/bin/env python3
"""Private FLTK/DBusMenu export test; deterministic stereo worker, no headset/model."""
import json,os,re,signal,subprocess,sys,tempfile,time
from pathlib import Path
from PIL import Image
from Xlib import X,XK,display
from Xlib.ext import xtest
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('QIMGVR_GLTF_UI_BUS')!='1':
 os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'QIMGVR_GLTF_UI_BUS':'1'})
repo=Path(__file__).resolve().parents[1]
binary=Path(os.environ['QIMGVR_BINARY']);fixture=Path(os.environ['QIMGVR_GLTF_FIXTURE'])
out=Path(os.environ['QIMGVR_GLTF_ARTIFACTS']);out.mkdir(parents=True,exist_ok=True)
children=[];streams=[]
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except (OSError,ValueError,KeyError,StopIteration):pass
  time.sleep(.05)
 raise AssertionError('Timed out; inspect '+str(out))
def dbus(dest,path,method,*args):
 return subprocess.check_output(['gdbus','call','--session','--dest',dest,'--object-path',path,'--method',method,'--',*args],text=True,stderr=subprocess.DEVNULL)
def owner(pid):
 for name in re.findall(r"'(:[0-9.]+)'",dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.ListNames')):
  try:
   if re.search(r'\b'+str(pid)+r'\b',dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID',name)):return name
  except subprocess.CalledProcessError:pass
with tempfile.TemporaryDirectory(prefix='qimgvr-gltf-ui-') as directory:
 tmp=Path(directory);(tmp/'images').mkdir();(tmp/'exports').mkdir();(tmp/'config').mkdir();tmp.chmod(0o700)
 (tmp/'config/user-dirs.dirs').write_text('XDG_PICTURES_DIR="'+str(tmp/'exports')+'"\n')
 for name in ['a-stereo.png','b-stereo.png']:(tmp/'images'/name).write_bytes(fixture.read_bytes())
 worker=tmp/'worker';worker.write_text('''#!/usr/bin/python3
import sys,json,shutil,time
for line in sys.stdin:
 try:
  r=json.loads(line);a=r.get('action');v={'action':a,'id':r.get('id'),'status':'ok','connected':False}
  if a=='quit':break
  if a=='convert':
   time.sleep(.6);shutil.copyfile(r['input'],r['output']);v.update(output=r['output'],depth_bypassed=True)
  print(json.dumps(v),flush=True)
 except Exception as e:print(json.dumps({'action':r.get('action'),'id':r.get('id'),'status':'error','error':str(e)}),flush=True)
''');worker.chmod(0o755)
 rd,wr=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(wr)
 with os.fdopen(rd)as f:screen=':'+f.readline().strip()
 env={**os.environ,'DISPLAY':screen,'FLTK_BACKEND':'x11','XDG_CURRENT_DESKTOP':'Dome','XDG_RUNTIME_DIR':directory,'XDG_CONFIG_HOME':str(tmp/'config'),'XDG_CACHE_HOME':str(tmp/'cache'),'LIBGL_ALWAYS_SOFTWARE':'1','QIMGV_SBS_PYTHON':str(worker),'LD_LIBRARY_PATH':'/opt/fltk-vr/toolkit/lib:/opt/fltk-vr/lib:/opt/fltk-vr/lib/bitmap'}
 env.pop('WAYLAND_DISPLAY',None);env.pop('WAYLAND_SOCKET',None)
 xd=display.Display(screen)
 def key(w,name):
  w.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();code=xd.keysym_to_keycode(XK.string_to_keysym(name))
  for kind in [X.KeyPress,X.KeyRelease]:xtest.fake_input(xd,kind,code);xd.sync();time.sleep(.08)
 def windows():return xd.screen().root.query_tree().children
 def find(text):return next((w for w in windows() if text in str(w.get_wm_name()or'') and w.get_attributes().map_state==X.IsViewable),None)
 def screenshot(w,name):
  g=w.get_geometry();data=w.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
  Image.frombytes('RGB',(g.width,g.height),data.data,'raw','BGRX').save(out/(name+'.png'))
 try:
  registrar=subprocess.Popen([sys.executable,str(repo/'scripts/test-fltk-ui.py'),'--registrar'],env=env,stdout=subprocess.PIPE,text=True);children.append(registrar);assert registrar.stdout.readline().strip()=='ready'
  log=(out/'viewer.log').open('w');streams.append(log)
  app=subprocess.Popen([str(binary),'--sbs=always','--input-layout=full-sbs',str(tmp/'images/a-stereo.png')],env=env,stdout=log,stderr=log);children.append(app)
  service=wait(lambda:owner(app.pid));path='/org/de_xr/FLTK/Menu1'
  def item(label,menu=path):
   layout=dbus(service,menu,'com.canonical.dbusmenu.GetLayout','0','-1','[]')
   match=re.search(r"\((\d+), \{'label': <'"+re.escape(label)+"'>",layout);assert match,label
   return match[1]
  def enabled(menu=path):return 'true' in dbus(service,menu,'com.canonical.dbusmenu.GetProperty',item('Export as glTF...',menu),'enabled')
  def action(label,menu=path,asynchronous=False):
   args=['gdbus','call','--session','--dest',service,'--object-path',menu,'--method','com.canonical.dbusmenu.Event','--',item(label,menu),'clicked','<0>','0']
   if asynchronous:
    p=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE);children.append(p);return p
   subprocess.run(args,check=True,stdout=subprocess.DEVNULL)
  wait(enabled);print('PASS main universal menu enables export only after current stereo conversion',flush=True)
  action('Next');wait(lambda:not enabled());wait(enabled)
  print('PASS navigating invalidates the old export until the new generation is ready',flush=True)
  action('Export as glTF...',asynchronous=True)
  dialog=wait(lambda:find('Export stereo image as glTF'));screenshot(dialog,'export-chooser')
  key(dialog,'Return')
  exported=tmp/'exports/b-stereo.glb';wait(lambda:exported.exists(),20)
  (out/'main-menu.glb').write_bytes(exported.read_bytes());assert exported.read_bytes()[:4]==b'glTF';exported.unlink()
  print('PASS main menu native file chooser exports the current stereo image',flush=True)
  # The stereo presenter owns its export action even without an owner-command RPC.
  presenter='/org/de_xr/FLTK/Menu2';wait(lambda:enabled(presenter))
  action('Export as glTF...',presenter,True)
  dialog=wait(lambda:find('Export stereo image as glTF'));key(dialog,'Return')
  exported=tmp/'exports/b-stereo.glb';wait(lambda:exported.exists(),20)
  (out/'stereo-menu.glb').write_bytes(exported.read_bytes())
  print('PASS stereo presenter menu exports its own rendered pixels',flush=True)
  assert app.poll() is None
 finally:
  xd.close()
  for child in reversed(children):
   if child.poll() is None:
    child.terminate()
    try:child.wait(timeout=4)
    except subprocess.TimeoutExpired:child.kill();child.wait()
  for stream in streams:stream.close()
