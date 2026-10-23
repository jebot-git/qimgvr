#!/usr/bin/env python3
"""Observe first-image presentation and menu transitions outside the application.
Run: uv run --no-project --with dbus-next --with python-xlib --with pillow python scripts/test-fltk-ui.py
"""
import asyncio,json,os,re,signal,socket,subprocess,sys,tempfile,time,urllib.request
from pathlib import Path
if '--registrar' in sys.argv:
 from dbus_next.aio import MessageBus
 async def hold():
  bus=await MessageBus().connect();await bus.request_name('com.canonical.AppMenu.Registrar');print('ready',flush=True);await asyncio.Future()
 asyncio.run(hold());sys.exit()
from Xlib import X,XK,display
from Xlib.ext import xtest
from PIL import Image
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('QIMGVR_TEST_BUS')!='1':
 os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'QIMGVR_TEST_BUS':'1'})
repo=Path(__file__).resolve().parents[1];build=Path(os.environ.get('QIMGVR_BUILD_DIR',repo/'build-fltk/fltk'));out=repo/'build-fltk/test-artifacts';out.mkdir(parents=True,exist_ok=True)
children=[];logs=[];checks=[]
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except (OSError,ValueError,KeyError,StopIteration):pass
  time.sleep(.05)
 raise AssertionError('Timed out; see '+str(out))
def check(value,name):
 assert value,name
 checks.append(name);print('PASS',name,flush=True)
def start(args,name,env):
 log=(out/(name+'.log')).open('w');logs.append(log);p=subprocess.Popen(args,env=env,stdout=log,stderr=log);children.append(p);return p
def stop(p):
 if p.poll() is None:
  p.terminate()
  try:p.wait(timeout=4)
  except subprocess.TimeoutExpired:p.kill();p.wait()
def dbus(dest,path,method,*args):
 return subprocess.check_output(['gdbus','call','--session','--dest',dest,'--object-path',path,'--method',method,'--',*args],text=True)
def service(pid):
 names=re.findall(r"'(:[0-9.]+)'",dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.ListNames'))
 for name in names:
  try:
   if str(pid) in dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID',name):return name
  except subprocess.CalledProcessError:pass
 return None
def menu_action(owner,label):
 path='/org/de_xr/FLTK/Menu1';layout=dbus(owner,path,'com.canonical.dbusmenu.GetLayout','0','-1','[]')
 item=re.search(r"\((\d+), \{'label': <'"+re.escape(label)+"'>",layout);assert item,label
 return dbus(owner,path,'com.canonical.dbusmenu.Event',item[1],'clicked','<0>','0')
with tempfile.TemporaryDirectory(prefix='qimgvr-ui-') as temporary:
 tmp=Path(temporary);tmp.chmod(0o700);image=tmp/'first.png';Image.new('RGB',(320,240),(235,25,45)).save(image);Image.new('RGB',(320,240),(25,220,70)).save(tmp/'second.png')
 rd,wr=os.pipe();x=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(x);os.close(wr)
 with os.fdopen(rd)as f:ds=':'+f.readline().strip()
 env={**os.environ,'DISPLAY':ds,'FLTK_BACKEND':'x11','XDG_CURRENT_DESKTOP':'Dome','XDG_RUNTIME_DIR':temporary,'XDG_CONFIG_HOME':temporary+'/config','XDG_CACHE_HOME':temporary+'/cache','LIBGL_ALWAYS_SOFTWARE':'1','LD_LIBRARY_PATH':'/opt/fltk-vr/toolkit/lib:/opt/fltk-vr/lib:/opt/fltk-vr/lib/bitmap'}
 env.pop('WAYLAND_DISPLAY',None);env.pop('WAYLAND_SOCKET',None);xd=display.Display(ds)
 def win():return next((w for w in xd.screen().root.query_tree().children if 'qimgvr' in str(w.get_wm_name()or'')),None)
 def shot(w,name):
  g=w.get_geometry();raw=xd.screen().root.get_image(g.x,g.y,g.width,g.height,X.ZPixmap,0xffffffff);im=Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX');im.save(out/(name+'.png'));return im
 def red(pixel):return pixel[0]>210 and pixel[1]<70 and pixel[2]<90
 def green(pixel):return pixel[0]<70 and pixel[1]>190 and pixel[2]<100
 def key(w,name):
  w.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();code=xd.keysym_to_keycode(XK.string_to_keysym(name))
  for kind in [X.KeyPress,X.KeyRelease]:xtest.fake_input(xd,kind,code)
  xd.sync()
 try:
  app=start([str(build/'qimgvr'),'--no-sbs',str(image)],'x11',env);w=wait(win);owner=wait(lambda:service(app.pid));time.sleep(.5)
  initial=shot(w,'first-x11');check(red(initial.getpixel((initial.width//2,initial.height//2))),'first image renders without a second image or forced application snapshot')
  check(not red(initial.getpixel((initial.width//2,5))),'Dome without a menu host keeps the local menu')
  host=subprocess.Popen([sys.executable,__file__,'--registrar'],stdout=subprocess.PIPE,text=True);children.append(host);assert host.stdout.readline().strip()=='ready'
  wait(lambda:red(shot(w,'universal').getpixel((w.get_geometry().width//2,5))))
  check(True,'working universal menu collapses the local bar and reclaims its space')
  check('Open...' in dbus(owner,'/org/de_xr/FLTK/Menu1','com.canonical.dbusmenu.GetLayout','0','-1','[]'),'universal menu exports real commands')
  key(w,'Right');wait(lambda:green(shot(w,'second').getpixel((w.get_geometry().width//2,200))))
  check(True,'Right shortcut switches images while the local bar is collapsed')
  menu_action(owner,'Previous');wait(lambda:red(shot(w,'previous').getpixel((w.get_geometry().width//2,200))))
  check(True,'exported universal-menu action switches back to the first image')
  stop(host);wait(lambda:not red(shot(w,'restored').getpixel((w.get_geometry().width//2,5))))
  check(True,'local menu and layout return after menu host disappears');stop(app)
  other=start([str(build/'qimgvr'),'--no-sbs',str(image)],'other-desktop',{**env,'XDG_CURRENT_DESKTOP':'XFCE'});w=wait(win);time.sleep(.5)
  check(not red(shot(w,'other-desktop').getpixel((w.get_geometry().width//2,5))),'other desktop retains its local menu');stop(other)
  # A real private DomeD compositor supplies Wayland subsurfaces and the menu host.
  with socket.socket()as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  nested={**env,'FLTK_BACKEND':'wayland','DOME_START_XR':'0','DOME_LOGIN_SESSION':'0','DOME_APPEARANCE_SERVICE':'0','DOME_RENDERER':'gles','DOME_PREVIEW_PORT':str(port)}
  compositor=start([os.environ.get('DOME_COMPOSITOR','/usr/local/bin/dome-compositor'),'--backend','nested'],'compositor',nested)
  text=wait(lambda:(out/'compositor.log').read_text() if 'Dome Wayland socket:' in (out/'compositor.log').read_text() else None);nested['WAYLAND_DISPLAY']=re.search(r'Dome Wayland socket: (\S+)',text)[1]
  def state():return json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/api/state'))['scene']
  host_window=wait(lambda:next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name()or'').startswith('Dome')),None))
  def client():return next((w for w in state()['windows']if 'qimgvr' in w['title']),None)
  def client_pixel(name,top=False):
   r=client()['desktop'];g=host_window.get_geometry();raw=host_window.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff);im=Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX');im.save(out/(name+'.png'))
   return im.getpixel((int(r['x']+r['w']/2),int(r['y']+(5 if top else r['h']/2))))
  app=start([str(build/'qimgvr'),'--no-sbs',str(image)],'wayland',nested);wait(client);wait(lambda:red(client_pixel('wayland-first')))
  check(True,'first image reaches the compositor through native Wayland GL subsurfaces')
  wait(lambda:red(client_pixel('wayland-menu',True)));check(True,'real DomeD host removes the local menu and empty menu strip');stop(app);wait(lambda:not client())
  fixture=start([str(build/'qimgvr-canvas-test')],'late-canvas',nested);wait(client);wait(lambda:red(client_pixel('late-canvas')))
  check(True,'a GL canvas created after the parent first frame becomes visible without image switching');stop(fixture)
 finally:
  xd.close()
  for child in reversed(children):stop(child)
  for log in logs:log.close()
(out/'ui-tests.json').write_text(json.dumps(checks,indent=2)+'\n')
