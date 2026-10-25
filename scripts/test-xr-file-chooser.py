#!/usr/bin/env python3
"""Native FLTK chooser input through a private DomeD XR seat and physical pointer path."""
import json,os,re,signal,socket,subprocess,tempfile,time,urllib.request,sys,math,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
from pathlib import Path
from PIL import Image
from Xlib import X,XK,display
from Xlib.ext import xtest
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('QIMGVR_CHOOSER_TEST_BUS')!='1':
 os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'QIMGVR_CHOOSER_TEST_BUS':'1'})
repo=Path(__file__).resolve().parents[1]
out=Path(os.environ.get('QIMGVR_CHOOSER_ARTIFACTS',repo/'build-fltk/xr-chooser-tests'));out.mkdir(parents=True,exist_ok=True);children=[];logs=[]
def wait(fn,seconds=12):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except (OSError,ValueError,KeyError,StopIteration):pass
  time.sleep(.08)
 raise AssertionError('Timed out')
def call(dest,path,method,*args):return subprocess.check_output(['gdbus','call','--session','--dest',dest,'--object-path',path,'--method',method,'--',*args],text=True,stderr=subprocess.DEVNULL,timeout=3)
def owner(pid):
 for name in re.findall(r"'(:[0-9.]+)'",call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.ListNames')):
  try:
   if re.search(r'\b'+str(pid)+r'\b',call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID',name)):return name
  except subprocess.SubprocessError:pass
with tempfile.TemporaryDirectory(prefix='qimgvr-chooser-xr-') as directory:
 temp=Path(directory);temp.chmod(0o700)
 rd,wr=os.pipe();x=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(x);os.close(wr)
 with os.fdopen(rd)as f:screen=':'+f.readline().strip()
 with socket.socket()as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 env={**os.environ,'DISPLAY':screen,'XDG_RUNTIME_DIR':directory,'XDG_CONFIG_HOME':directory+'/config','XDG_CACHE_HOME':directory+'/cache','XDG_DATA_HOME':directory+'/data','XDG_STATE_HOME':directory+'/state','XDG_CURRENT_DESKTOP':'Dome','DOME_PREVIEW_PORT':str(port),'DOME_LOGIN_SESSION':'0','DOME_START_XR':'0','DOME_START_XWAYLAND':'0','DOME_SESSION_RESTORE':'0','DOME_APPEARANCE_SERVICE':'0','DOME_RENDERER':'gles','LIBGL_ALWAYS_SOFTWARE':'1','FLTK_BACKEND':'wayland','LD_LIBRARY_PATH':'/opt/fltk-vr/toolkit/lib:/opt/fltk-vr/lib:/opt/fltk-vr/lib/bitmap'}
 for k in ['WAYLAND_DISPLAY','WAYLAND_SOCKET']:env.pop(k,None)
 for service in ['TRAY','KDECONNECT','KWALLET','POLKIT','IME']:env['DOME_START_'+service]='0'
 def start(args,name,environment=env):
  stream=(out/(name+'.log')).open('w');logs.append(stream);p=subprocess.Popen(args,env=environment,stdout=stream,stderr=stream);children.append(p);return p
 try:
  compositor=start([os.environ.get('DOME_COMPOSITOR','/usr/local/bin/dome-compositor'),'--backend','nested'],'probe-compositor')
  base=f'http://127.0.0.1:{port}'
  def request(route,body=None):
   with urllib.request.urlopen(urllib.request.Request(base+'/api/'+route,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'}),timeout=3)as f:return json.load(f)
  def state():return request('state')['scene']
  wait(state)
  record=wait(lambda:next((p for p in temp.glob('*.dome-session.json') if json.loads(p.read_text())['realm']=='xr'),None))
  exportMode=os.environ.get('QIMGVR_CHOOSER_EXPORT')=='1'
  fixture=temp/'stereo.png';Image.new('RGBA',(256,128),(70,110,160,255)).save(fixture)
  appArgs=['--sbs-present',str(fixture),'--dome-present'] if exportMode else ['--no-sbs']
  app=start([os.environ.get('QIMGVR_BINARY',str(repo/'build-fltk/fltk/qimgvr')),*appArgs],'probe-app',{**env,'WAYLAND_DISPLAY':record.name.removesuffix('.dome-session.json'),'WAYLAND_DEBUG':'1'})
  root=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==app.pid),None))
  if exportMode:assert root['app_id']=='org.qimgvr.Sbs' and root['stereo']=='side_by_side',root
  import threading
  stop=threading.Event()
  def heartbeat():
   while not stop.wait(.1):
    try:request('xr-session',{'id':7721,'visible':True,'focused':True,'frame_period_ns':11111111,'head':{'position_m':[0,1.4,0],'orientation_xyzw':[0,0,0,1]},'horizontal_fov_deg':90,'mouse_viewport':[-.7,.7,-.65,.65]})
    except OSError:return
  threading.Thread(target=heartbeat,daemon=True).start();wait(lambda:request('xr-control')['input_realm']=='xr')
  request('action',{'op':'focus','id':root['id']})
  xd=display.Display(screen);host=wait(lambda:next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name()or'').startswith('Dome')),None));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
  service=wait(lambda:owner(app.pid));path='/org/de_xr/FLTK/Menu1';layout=call(service,path,'com.canonical.dbusmenu.GetLayout','0','-1','[]');item=re.search(r"\((\d+), \{'label': <'"+re.escape('Export as glTF...' if exportMode else 'Open...')+"'>",layout)[1]
  def openDialog():
   activation=start(['gdbus','call','--session','--dest',service,'--object-path',path,'--method','com.canonical.dbusmenu.Event','--',item,'clicked','<0>','0'],'probe-event')
   dialog=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==app.pid and w['title']==('Export stereo image as glTF' if exportMode else 'Open image, video or glTF')),None))
   wait(lambda:activation.poll()==0)
   assert dialog['app_id']=='qimgvr' and dialog['stereo']=='mono',dialog
   (out/'dialog-state.json').write_text(json.dumps(dialog,indent=2)+'\n')
   print('PASS mono chooser identity, independent of the SBS presenter',flush=True)
   print('PASS menu activation acknowledged while chooser remains open',flush=True)
   return dialog
  def capture(w,name):
   def frame():
    with urllib.request.urlopen(base+'/api/xr-frame/'+str(w['id']),timeout=3) as f:return Image.frombytes('RGBA',(int(f.headers['X-Dome-Width']),int(f.headers['X-Dome-Height'])),f.read())
   im=wait(frame)
   im.save(out/(name+'.png'));return im.tobytes()
  def key(name):
   code=xd.keysym_to_keycode(XK.string_to_keysym(name))
   for kind in [X.KeyPress,X.KeyRelease]:xtest.fake_input(xd,kind,code);xd.sync();time.sleep(.08)
  dialog=openDialog();time.sleep(.4);before=capture(dialog,'before-input');print('Mapped chooser',dialog['id'],'scene focus',state()['focused'],flush=True)
  key('a');time.sleep(.25);after=capture(dialog,'after-type');print('Initial keyboard changes pixels?',before!=after,flush=True);assert before!=after
  key('BackSpace');time.sleep(.2)
  # Drive the physical mouse through DomeD's ray geometry, not a synthetic UV.
  xtest.fake_input(xd,X.MotionNotify,x=800,y=500);xd.sync();time.sleep(.3)
  c=request('xr-control');angles=c['mouse_angles'] or [0,0]
  quad=next(q for q in request('xr-layout')['quads'] if q['id']==dialog['id'])
  q=quad['pose']['orientation_xyzw'];fieldV=.945 if exportMode else .89;v=[0,quad['size'][1]*(.5-fieldV),0]
  def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
  t=[2*x for x in cross(q[:3],v)];r=cross(q[:3],t)
  point=[quad['pose']['position_m'][i]+v[i]+q[3]*t[i]+r[i] for i in range(3)];point[1]-=1.4
  wanted=[math.atan2(point[0],-point[2]),math.atan2(point[1],math.hypot(point[0],point[2]))]
  print('Filename field angles',wanted,'viewport',request('xr-layout')['viewport'],flush=True)
  dx=(wanted[0]-angles[0])/.0025;dy=-(wanted[1]-angles[1])/.0025
  xtest.fake_input(xd,X.MotionNotify,x=round(800+dx),y=round(500+dy));xd.sync();time.sleep(.4)
  cursor=request('xr-control')['cursor'];print('Physical cursor',cursor,flush=True);assert cursor['id']==dialog['id']
  for kind in [X.ButtonPress,X.ButtonRelease]:xtest.fake_input(xd,kind,1);xd.sync();time.sleep(.1)
  before=capture(dialog,'before-physical-type');key('b');time.sleep(.2);after=capture(dialog,'after-physical-type');print('Keyboard after physical click changes pixels?',before!=after,'focus',state()['focused'],flush=True);assert before!=after and state()['focused']==dialog['id']
  # Holding Escape past the repeat delay must not access a destroyed chooser.
  code=xd.keysym_to_keycode(XK.string_to_keysym('Escape'));xtest.fake_input(xd,X.KeyPress,code);xd.sync();time.sleep(.8);xtest.fake_input(xd,X.KeyRelease,code);xd.sync();time.sleep(.4)
  print('After held Escape, dialog?',any(w['id']==dialog['id'] for w in state()['windows']),'app alive?',app.poll()is None,flush=True);assert app.poll()is None and not any(w['id']==dialog['id'] for w in state()['windows'])
  stop.set();xd.close()
 finally:
  for p in reversed(children):
   if p.poll()is None:
    p.terminate()
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:p.kill();p.wait()
  for f in logs:f.close()
