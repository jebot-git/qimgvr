#!/usr/bin/env python3
"""Real Wayland model stereo and mono dialogs on a private DomeD instance."""
import json,os,re,signal,socket,subprocess,sys,tempfile,threading,time,urllib.request
from pathlib import Path
import numpy as np
from PIL import Image
from Xlib import X,XK,display
from Xlib.ext import xtest
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('QIMGVR_MODEL_XR_BUS')!='1':os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'QIMGVR_MODEL_XR_BUS':'1'})
binary=Path(os.environ['QIMGVR_BINARY']).resolve();fixture=Path(os.environ['QIMGVR_MODEL_FIXTURE']).resolve();out=Path(os.environ['QIMGVR_MODEL_ARTIFACTS']).resolve();out.mkdir(parents=True,exist_ok=True)
children=[];logs=[];stop=threading.Event()
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   value=fn()
   if isinstance(value,np.ndarray) or value:return value
  except (OSError,ValueError,KeyError,StopIteration,subprocess.CalledProcessError):pass
  time.sleep(.08)
 raise AssertionError('Timed out; see '+str(out))
with tempfile.TemporaryDirectory(prefix='qimgvr-model-xr-') as directory:
 root=Path(directory);root.chmod(0o700)
 rd,wr=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(wr)
 with os.fdopen(rd)as stream:screen=':'+stream.readline().strip()
 with socket.socket()as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 env={**os.environ,'DISPLAY':screen,'XDG_RUNTIME_DIR':directory,'XDG_CONFIG_HOME':directory+'/config','XDG_CACHE_HOME':directory+'/cache','XDG_DATA_HOME':directory+'/data','XDG_STATE_HOME':directory+'/state','XDG_CURRENT_DESKTOP':'Dome','DOME_PREVIEW_PORT':str(port),'DOME_LOGIN_SESSION':'0','DOME_START_XR':'0','DOME_START_XWAYLAND':'0','DOME_SESSION_RESTORE':'0','DOME_APPEARANCE_SERVICE':'0','DOME_RENDERER':'gles','LIBGL_ALWAYS_SOFTWARE':'1','FLTK_BACKEND':'wayland','QIMGV_SBS_PYTHON':'/nonexistent/no-arc-worker','QIMGVR_SBS_TRACE':'1','LD_LIBRARY_PATH':'/opt/fltk-vr/toolkit/lib:/opt/fltk-vr/lib:/opt/fltk-vr/lib/bitmap','DBUS_SYSTEM_BUS_ADDRESS':'unix:path=/nonexistent/qimgvr-model-test-system-bus'}
 if os.environ.get('QIMGVR_XR_TEST_GPU')=='1':
  env['DOME_RENDERER']='vulkan'
  env.pop('LIBGL_ALWAYS_SOFTWARE',None)
 for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET']:env.pop(key,None)
 for service in ['TRAY','KDECONNECT','KWALLET','POLKIT','IME']:env['DOME_START_'+service]='0'
 def start(args,name,environment=env):
  log=(out/(name+'.log')).open('w');logs.append(log);p=subprocess.Popen(list(map(str,args)),env=environment,stdout=log,stderr=log);children.append(p);return p
 def dbus(dest,path,method,*args):return subprocess.check_output(['gdbus','call','--session','--dest',dest,'--object-path',path,'--method',method,'--',*args],env=env,text=True,stderr=subprocess.DEVNULL,timeout=3)
 def owner(pid):
  for name in re.findall(r"'(:[0-9.]+)'",dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.ListNames')):
   try:
    if re.search(r'\b'+str(pid)+r'\b',dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID',name)):return name
   except subprocess.SubprocessError:pass
 try:
  start([os.environ.get('DOME_COMPOSITOR','/usr/local/bin/dome-compositor'),'--backend','nested'],'compositor')
  base=f'http://127.0.0.1:{port}'
  def request(route,body=None):
   with urllib.request.urlopen(urllib.request.Request(base+'/api/'+route,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'}),timeout=3)as f:return json.load(f)
  def state():return request('state')['scene']
  wait(state)
  record=wait(lambda:next((p for p in root.glob('*.dome-session.json') if json.loads(p.read_text())['realm']=='xr'),None))
  app=start([binary,fixture],'viewer',{**env,'WAYLAND_DISPLAY':record.name.removesuffix('.dome-session.json')})
  main=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==app.pid and w['app_id']=='qimgvr'),None))
  tracking={'x':0}
  def heartbeat():
   while not stop.wait(.1):
    try:
     x=tracking['x'];request('xr-session',{'id':7731,'visible':True,'focused':True,'frame_period_ns':11111111,'head':{'position_m':[x,1.4,0],'orientation_xyzw':[0,0,0,1]},'eyes':[{'position_m':[x+offset,1.4,0],'orientation_xyzw':[0,0,0,1]} for offset in [-.032,.032]],'horizontal_fov_deg':90,'mouse_viewport':[-.7,.7,-.65,.65]})
    except OSError:return
  threading.Thread(target=heartbeat,daemon=True).start();wait(lambda:request('xr-control')['input_realm']=='xr')
  window=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==app.pid and w['app_id']=='org.qimgvr.Sbs'),None))
  assert window['stereo']=='side_by_side',window
  wait(lambda:any(q['id']==window['id'] and q.get('dome') for q in request('integrations/comic-chat')['quads']))
  time.sleep(.7)
  def capture(name):
   with urllib.request.urlopen(base+'/api/xr-frame/'+str(window['id']),timeout=3)as f:im=Image.frombytes('RGBA',(int(f.headers['X-Dome-Width']),int(f.headers['X-Dome-Height'])),f.read())
   im.save(out/(name+'.png'));return np.array(im)
  first=wait(lambda:capture('stereo-tracked'))
  w=first.shape[1]//2;difference=float(np.mean(np.abs(first[:,:w,:3].astype(float)-first[:,-w:,:3])))
  assert difference>.3,('Stereo eyes identical or blank',difference)
  tracking['x']=.08;time.sleep(.6);moved=wait(lambda:capture('stereo-head-shift'))
  motion=float(np.mean(np.abs(first.astype(float)-moved.astype(float))));assert motion>.3,('No head parallax',motion)
  (out/'metrics.json').write_text(json.dumps({'eyeDifference':difference,'headMotionDifference':motion,'window':window},indent=2)+'\n')
  # The model presenter uses the same mono-dialog policy as image presenters.
  service=wait(lambda:owner(app.pid));menu='/org/de_xr/FLTK/Menu2'
  layout=wait(lambda:dbus(service,menu,'com.canonical.dbusmenu.GetLayout','0','-1','[]'))
  item=re.search(r"\((\d+), \{'label': <'Open\.\.\.'>",layout)[1]
  activation=start(['gdbus','call','--session','--dest',service,'--object-path',menu,'--method','com.canonical.dbusmenu.Event','--',item,'clicked','<0>','0'],'open-event')
  dialog=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==app.pid and w['title']=='Open image, video or glTF'),None))
  assert dialog['app_id']=='qimgvr' and dialog['stereo']=='mono',dialog
  wait(lambda:activation.poll()==0)
  xd=display.Display(screen);host=wait(lambda:next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name()or'').startswith('Dome')),None));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
  code=xd.keysym_to_keycode(XK.string_to_keysym('Escape'));xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code);xd.sync()
  wait(lambda:not any(w['id']==dialog['id'] for w in state()['windows']));assert app.poll()is None
  # Keep an unrelated native window alive while closing each model window.
  witness=start([binary,'--no-sbs',fixture],'witness',{**env,'WAYLAND_DISPLAY':record.name.removesuffix('.dome-session.json'),'XDG_CONFIG_HOME':directory+'/witness-config'})
  witnessWindow=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==witness.pid and w['app_id']=='qimgvr'),None))
  def survives():
   assert witness.poll() is None
   assert any(w['id']==witnessWindow['id'] and w['xr_visible'] and not w['minimized'] for w in state()['windows'])
   with urllib.request.urlopen(base+'/api/xr-frame/'+str(witnessWindow['id']),timeout=3)as f:
    pixels=f.read();assert len(pixels)>0
   return True
  def closeWithSuperW(id):
   request('action',{'op':'focus','id':id})
   wait(lambda:state()['focused']==id)
   time.sleep(.2) # allow the compositor to apply native keyboard focus
   superKey=xd.keysym_to_keycode(XK.string_to_keysym('Super_L'))
   wKey=xd.keysym_to_keycode(XK.string_to_keysym('w'))
   xtest.fake_input(xd,X.KeyPress,superKey);xtest.fake_input(xd,X.KeyPress,wKey);xd.sync()
   # Keep the physical chord held across the focus change. Repeated presses
   # must not close the next application while the original key is still down.
   for _ in range(6):
    time.sleep(.1);xtest.fake_input(xd,X.KeyPress,wKey);xd.sync()
   xtest.fake_input(xd,X.KeyRelease,wKey);xtest.fake_input(xd,X.KeyRelease,superKey);xd.sync()
  request('action',{'op':'focus','id':window['id']})
  wait(lambda:state()['focused']==window['id'])
  time.sleep(.2)
  code=xd.keysym_to_keycode(XK.string_to_keysym('Escape'))
  xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code);xd.sync()
  wait(lambda:not any(w.get('client_pid')==app.pid and w['app_id']=='org.qimgvr.Sbs' for w in state()['windows']))
  time.sleep(.3);assert app.poll() is None;wait(survives)
  # Re-enable stereo, then close its XR document window with a real compositor
  # close request while model rendering and its polling timer are active.
  layout=dbus(service,'/org/de_xr/FLTK/Menu1','com.canonical.dbusmenu.GetLayout','0','-1','[]')
  toggle=re.search(r"\((\d+), \{'label': <'Always \(XR or preview\)'>",layout)[1]
  dbus(service,'/org/de_xr/FLTK/Menu1','com.canonical.dbusmenu.Event',toggle,'clicked','<0>','0')
  reopened=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==app.pid and w['app_id']=='org.qimgvr.Sbs'),None))
  closeWithSuperW(reopened['id'])
  wait(lambda:app.poll() is not None)
  assert app.returncode==0,('Abnormal model-close exit',app.returncode)
  wait(lambda:not any(w.get('client_pid')==app.pid for w in state()['windows']))
  wait(survives)
  # A fresh viewer's main-window close must also remove its stereo surface.
  app=start([binary,fixture],'main-close',{**env,'WAYLAND_DISPLAY':record.name.removesuffix('.dome-session.json'),'XDG_CONFIG_HOME':directory+'/main-close-config'})
  main=wait(lambda:next((w for w in state()['windows'] if w.get('client_pid')==app.pid and w['app_id']=='qimgvr'),None))
  wait(lambda:any(w.get('client_pid')==app.pid and w['app_id']=='org.qimgvr.Sbs' for w in state()['windows']))
  closeWithSuperW(main['id'])
  wait(lambda:app.poll() is not None)
  assert app.returncode==0,('Abnormal main-close exit',app.returncode)
  wait(lambda:not any(w.get('client_pid')==app.pid for w in state()['windows']))
  wait(survives)
  print('PASS native Wayland geometry stereo, tracked-eye cylinder projection, head parallax, mono model-file chooser and Super+W closes preserve unrelated XR windows')
 finally:
  stop.set()
  for p in reversed(children):
   if p.poll()is None:p.terminate()
  for p in reversed(children):
   try:p.wait(timeout=4)
   except subprocess.TimeoutExpired:p.kill();p.wait()
  for log in logs:log.close()
