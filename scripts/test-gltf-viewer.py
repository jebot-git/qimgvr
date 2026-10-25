#!/usr/bin/env python3
"""Native glTF viewer/navigation/controls on a private bus and Xvfb."""
import base64,json,os,re,signal,struct,subprocess,sys,tempfile,time
from pathlib import Path
import numpy as np
from PIL import Image
from Xlib import X,XK,display
from Xlib.ext import xtest
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('QIMGVR_MODEL_TEST_BUS')!='1':
 os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'QIMGVR_MODEL_TEST_BUS':'1'})
repo=Path(__file__).resolve().parents[1]
binary=Path(os.environ['QIMGVR_BINARY']).resolve();out=Path(os.environ['QIMGVR_MODEL_ARTIFACTS']).resolve();out.mkdir(parents=True,exist_ok=True)
children=[];logs=[]
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except (OSError,ValueError,KeyError):pass
  time.sleep(.05)
 raise AssertionError('Timed out; inspect '+str(out))
with tempfile.TemporaryDirectory(prefix='qimgvr-model-') as directory:
 root=Path(directory);root.chmod(0o700);media=root/'media';assets=media/'assets';assets.mkdir(parents=True)
 # External buffer and image, plus two independent animation clips.
 values=[-.5,-.4,0,.5,-.4,0,0,.5,.15,0,0,1,0,0,1,0,0,1,0,1,1,1,.5,0]
 buf=bytearray(struct.pack('<24f',*values));idxOffset=len(buf);buf+=struct.pack('<3I',0,1,2)
 times=len(buf);buf+=struct.pack('<3f',0,1,2);translation=len(buf);buf+=struct.pack('<9f',0,0,0,.2,0,0,0,0,0)
 (assets/'mesh.bin').write_bytes(buf)
 texture=Image.new('RGBA',(32,32),(220,80,60,255))
 for y in range(32):
  for x in range(32):texture.putpixel((x,y),(220,50+x*5,40+y*5,255))
 texture.save(assets/'texture.png');texture.save(media/'c-image.png')
 doc={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0]}],'nodes':[{'mesh':0}],
 'buffers':[{'uri':'assets/mesh.bin','byteLength':len(buf)}],
 'bufferViews':[{'buffer':0,'byteOffset':0,'byteLength':36},{'buffer':0,'byteOffset':36,'byteLength':36},{'buffer':0,'byteOffset':72,'byteLength':24},{'buffer':0,'byteOffset':idxOffset,'byteLength':12},{'buffer':0,'byteOffset':times,'byteLength':12},{'buffer':0,'byteOffset':translation,'byteLength':36}],
 'accessors':[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3','min':[-.5,-.4,0],'max':[.5,.5,.15]},{'bufferView':1,'componentType':5126,'count':3,'type':'VEC3'},{'bufferView':2,'componentType':5126,'count':3,'type':'VEC2'},{'bufferView':3,'componentType':5125,'count':3,'type':'SCALAR'},{'bufferView':4,'componentType':5126,'count':3,'type':'SCALAR','min':[0],'max':[2]},{'bufferView':5,'componentType':5126,'count':3,'type':'VEC3'}],
 'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3,'material':0}]}],
 'images':[{'uri':'assets/texture.png'}],'textures':[{'source':0}],
 'extensionsUsed':['KHR_materials_unlit'],'materials':[{'pbrMetallicRoughness':{'baseColorTexture':{'index':0}},'extensions':{'KHR_materials_unlit':{}},'doubleSided':True}],
 'animations':[{'name':name,'samplers':[{'input':4,'output':5,'interpolation':'LINEAR'}],'channels':[{'sampler':0,'target':{'node':0,'path':'translation'}}]} for name in ['Move one','Move two']]}
 (media/'a-scene.gltf').write_text(json.dumps(doc))
 # A real export exercises unlit RGBA texture decoding and large indexed meshes.
 pair=np.zeros((64,256,4),dtype=np.uint8);rng=np.random.default_rng(171);left=rng.integers(0,256,(64,128,4),dtype=np.uint8);left[:,:,3]=255;pair[:,:128]=left;pair[:,128:248]=left[:,8:];pair[:,248:]=left[:,-1:]
 source=root/'pair.png';Image.fromarray(pair).save(source)
 subprocess.run([str(binary),'--export-gltf',str(media/'b-relief.glb'),'--input-layout=full-sbs',str(source)],check=True,capture_output=True)
 subprocess.run([str(binary),'--export-gltf',str(media/'h-self-contained.gltf'),'--input-layout=full-sbs',str(source)],check=True,capture_output=True)
 (media/'d-broken.glb').write_bytes(b'not a model')
 outside=dict(doc);outside['buffers']=[{'uri':'../outside.bin','byteLength':len(buf)}];(root/'outside.bin').write_bytes(buf);(media/'e-parent.gltf').write_text(json.dumps(outside))
 outside=dict(doc);outside['buffers']=[{'uri':'https://example.invalid/mesh.bin','byteLength':len(buf)}];(media/'f-network.gltf').write_text(json.dumps(outside))
 badExtra=json.loads(json.dumps(doc));badExtra['nodes'][0]['extras']={'representation':'approximate-stereo-relief','screenWidthMetres':'bad'};(media/'g-extras.gltf').write_text(json.dumps(badExtra))
 rd,wr=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1400x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(wr)
 with os.fdopen(rd)as stream:screen=':'+stream.readline().strip()
 env={**os.environ,'DISPLAY':screen,'FLTK_BACKEND':'x11','LIBGL_ALWAYS_SOFTWARE':'1','XDG_RUNTIME_DIR':directory,'XDG_CONFIG_HOME':str(root/'config'),'XDG_CURRENT_DESKTOP':'Dome','QIMGVR_SBS_TRACE':'1','QIMGV_SBS_PYTHON':'/nonexistent/no-arc-worker','LD_LIBRARY_PATH':'/opt/fltk-vr/toolkit/lib:/opt/fltk-vr/lib:/opt/fltk-vr/lib/bitmap'}
 for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET']:env.pop(key,None)
 def start(args,name):
  log=(out/(name+'.log')).open('w');logs.append(log);p=subprocess.Popen(list(map(str,args)),env=env,stdout=log,stderr=log);children.append(p);return p
 def dbus(dest,path,method,*args):return subprocess.check_output(['gdbus','call','--session','--dest',dest,'--object-path',path,'--method',method,'--',*args],env=env,text=True,stderr=subprocess.DEVNULL)
 def owner(pid):
  for name in re.findall(r"'(:[0-9.]+)'",dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.ListNames')):
   try:
    if re.search(r'\b'+str(pid)+r'\b',dbus('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID',name)):return name
   except subprocess.CalledProcessError:pass
 try:
  registrar=start([sys.executable,repo/'scripts/test-fltk-ui.py','--registrar'],'registrar');time.sleep(.3)
  app=start([binary,'--no-sbs',media/'a-scene.gltf'],'viewer');xd=display.Display(screen)
  service=wait(lambda:owner(app.pid));menu='/org/de_xr/FLTK/Menu1'
  def item(label):
   layout=dbus(service,menu,'com.canonical.dbusmenu.GetLayout','0','-1','[]');match=re.search(r"\((\d+), \{'label': <'"+re.escape(label)+"'>",layout);assert match,label;return match[1]
  def enabled(label):return 'true' in dbus(service,menu,'com.canonical.dbusmenu.GetProperty',item(label),'enabled')
  def action(label):dbus(service,menu,'com.canonical.dbusmenu.Event',item(label),'clicked','<0>','0');time.sleep(.15)
  def window(name='qimgvr'):return next((w for w in xd.screen().root.query_tree().children if name in str(w.get_wm_name()or'') and w.get_attributes().map_state==X.IsViewable),None)
  def image(w,name):
   g=w.get_geometry();raw=w.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff);im=Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX');im.save(out/(name+'.png'));return np.array(im)
  wait(lambda:enabled('Model'));w=wait(lambda:window('a-scene.gltf'));time.sleep(.4)
  assert not enabled('Export as glTF...') and not enabled('Edit')
  action('Play or pause animation');time.sleep(.15);initial=image(w,'external-gltf')
  assert len(np.unique(initial[50:-60].reshape(-1,3),axis=0))>100,'Model was not textured/rendered'
  origin=xd.screen().root.translate_coords(w,0,0)
  def key(name):
   w.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();code=xd.keysym_to_keycode(XK.string_to_keysym(name));xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code);xd.sync();time.sleep(.25)
  def drag(dx,dy,shift=False):
   if shift:xtest.fake_input(xd,X.KeyPress,xd.keysym_to_keycode(XK.string_to_keysym('Shift_L')))
   xtest.fake_input(xd,X.MotionNotify,x=origin.x+500,y=origin.y+280);xtest.fake_input(xd,X.ButtonPress,1);xd.sync();time.sleep(.08)
   xtest.fake_input(xd,X.MotionNotify,x=origin.x+500+dx,y=origin.y+280+dy);xd.sync();time.sleep(.1);xtest.fake_input(xd,X.ButtonRelease,1)
   if shift:xtest.fake_input(xd,X.KeyRelease,xd.keysym_to_keycode(XK.string_to_keysym('Shift_L')))
   xd.sync();time.sleep(.3)
  key('space');time.sleep(.7);animated=image(w,'animation-playing');assert np.mean(np.abs(animated.astype(float)-initial))>.1
  key('space');initial=image(w,'animation-paused')
  drag(70,30);rotated=image(w,'orbit');assert np.mean(np.abs(rotated.astype(float)-initial))>.5
  action('Reset model camera');reset=image(w,'reset');assert np.mean(np.abs(reset.astype(float)-initial))<1
  drag(70,10,True);panned=image(w,'pan');assert np.mean(np.abs(panned.astype(float)-initial))>.5
  key('r');xtest.fake_input(xd,X.ButtonPress,4);xtest.fake_input(xd,X.ButtonRelease,4);xd.sync();time.sleep(.3);zoomed=image(w,'zoom');assert np.mean(np.abs(zoomed.astype(float)-initial))>.5
  action('Next animation');assert 'Move two' in (out/'viewer.log').read_text()
  beforeReload=image(w,'before-live-reload');Image.new('RGBA',(32,32),(50,220,100,255)).save(assets/'texture.png')
  wait(lambda:np.mean(np.abs(image(w,'after-live-reload').astype(float)-beforeReload))>1)

  action('Next');wait(lambda:window('b-relief.glb'));wait(lambda:enabled('Model'));reference=image(wait(lambda:window('b-relief.glb')),'exported-glb')
  assert enabled('Reference image view')
  action('Fit model bounds');bounds=image(wait(lambda:window('b-relief.glb')),'bounds-fit');assert np.mean(np.abs(bounds.astype(float)-reference))>.5
  action('Reference image view');restored=image(wait(lambda:window('b-relief.glb')),'reference-restored');assert np.mean(np.abs(restored.astype(float)-reference))<1
  action('Next');wait(lambda:window('c-image.png'));wait(lambda:enabled('Edit'));assert not enabled('Model');image(wait(lambda:window('c-image.png')),'image-after-model')
  action('Previous');wait(lambda:window('b-relief.glb'));wait(lambda:enabled('Model'))
  action('Next');action('Next');wait(lambda:window('d-broken.glb'));time.sleep(.5);assert not enabled('Model') and app.poll()is None
  app.terminate();app.wait(timeout=4)
  # The real desktop SBS path must render geometry without launching Arc SBS.
  smoke=out/'stereo.ppm';p=start([binary,'--sbs=always','--smoke-test',smoke,media/'b-relief.glb'],'stereo');assert p.wait(timeout=20)==0
  state=json.loads(Path(str(smoke)+'.json').read_text());assert state['loaded'] and state['referenceView'] and state['stereoFrames']>1 and not state['error'],state
  for source in ['a-scene.gltf','d-broken.glb','e-parent.gltf','f-network.gltf','g-extras.gltf','h-self-contained.gltf']:
   target=out/(source+'.ppm');p=start([binary,'--no-sbs','--smoke-test',target,media/source],source)
   status=p.wait(timeout=20);report=json.loads(Path(str(target)+'.json').read_text());assert (status==0)==(source in ['a-scene.gltf','e-parent.gltf','g-extras.gltf','h-self-contained.gltf']),(source,status,report)
   assert report['loaded']==(source in ['a-scene.gltf','e-parent.gltf','g-extras.gltf','h-self-contained.gltf']),report
  print('PASS external glTF buffers/textures/animations, GLB display, orbit/pan/zoom/reset, image/model navigation, stereo geometry, malformed input and relative-parent resources and network URI rejection')
 finally:
  for p in reversed(children):
   if p.poll()is None:p.terminate()
  for p in reversed(children):
   try:p.wait(timeout=4)
   except subprocess.TimeoutExpired:p.kill();p.wait()
  for log in logs:log.close()
