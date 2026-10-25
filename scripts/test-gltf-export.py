#!/usr/bin/env python3
"""Exercise the prototype's real headless exporter with calibrated synthetic pairs."""
import base64,io,json,math,os,struct,subprocess,tempfile
from pathlib import Path
import numpy as np
from PIL import Image
binary=Path(os.environ.get('QIMGVR_BINARY',Path(__file__).resolve().parents[1]/'build-fltk/fltk/qimgvr'))
out=Path(os.environ.get('QIMGVR_GLTF_ARTIFACTS',Path(__file__).resolve().parents[1]/'build-fltk/gltf-tests'));out.mkdir(parents=True,exist_ok=True)
def run(source,target,layout='full-sbs',ok=True):
 env={k:v for k,v in os.environ.items() if k not in ['DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET']}
 p=subprocess.run([str(binary),'--export-gltf',str(target),'--input-layout='+layout,str(source)],capture_output=True,text=True,env=env)
 assert (p.returncode==0)==ok,(p.stdout,p.stderr)
 return p

def load(path):
 data=path.read_bytes()
 if path.suffix=='.glb':
  magic,version,length=struct.unpack_from('<III',data);assert magic==0x46546c67 and version==2 and length==len(data)
  n,kind=struct.unpack_from('<II',data,12);assert kind==0x4e4f534a and n%4==0
  doc=json.loads(data[20:20+n]);size,kind=struct.unpack_from('<II',data,20+n);assert kind==0x004e4942
  buf=data[28+n:];assert len(buf)==size and size%4==0
 else:
  doc=json.loads(data);buf=base64.b64decode(doc['buffers'][0]['uri'].split(',',1)[1])
 assert len(buf)==doc['buffers'][0]['byteLength']
 return doc,buf

def accessor(doc,buf,index):
 a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
 count={'VEC3':3,'VEC2':2,'SCALAR':1}[a['type']];fmt='f' if a['componentType']==5126 else 'I'
 stride=v.get('byteStride',count*4);start=v.get('byteOffset',0)+a.get('byteOffset',0)
 return np.array([struct.unpack_from('<'+fmt*count,buf,start+n*stride) for n in range(a['count'])])

rng=np.random.default_rng(7842);left=rng.integers(0,256,(96,256,4),dtype=np.uint8);left[:,:,3]=255
left[5:15,90:110,3]=128;left[20:30,120:130,3]=0
right=np.zeros_like(left)
right[:48,:-8]=left[:48,8:];right[48:,4:]=left[48:,:-4]
source=out/'synthetic-full-sbs.png';Image.fromarray(np.concatenate([left,right],axis=1)).save(source)
for suffix in ['glb','gltf']:
 target=out/('relief.'+suffix);p=run(source,target);doc,buf=load(target)
 position=accessor(doc,buf,0);normals=accessor(doc,buf,1);uv=accessor(doc,buf,2);indices=accessor(doc,buf,3)
 assert np.isfinite(position).all() and np.isfinite(normals).all()
 assert indices.min()>=0 and indices.max()<len(position) and len(indices)%3==0
 assert np.max(np.abs(np.linalg.norm(normals,axis=1)-1))<1e-4
 near=position[(uv[:,0]>.25)&(uv[:,0]<.75)&(uv[:,1]>.12)&(uv[:,1]<.37),2]
 far=position[(uv[:,0]>.25)&(uv[:,0]<.75)&(uv[:,1]>.62)&(uv[:,1]<.87),2]
 assert np.median(near)>.1 and np.median(far)<-.1,(np.median(near),np.median(far))
 extras=doc['nodes'][0]['extras'];assert extras['matchedFraction']>.3 and not extras['flatFallback'],extras
 assert extras['source']==source.name and extras['eyeSize']==[256,96]
 view=doc['bufferViews'][doc['images'][0]['bufferView']];png=buf[view['byteOffset']:view['byteOffset']+view['byteLength']]
 restored=np.array(Image.open(io.BytesIO(png)))
 assert np.array_equal(restored,left),'Embedded left-eye texture must preserve RGBA, orientation and full resolution'
 assert doc['materials'][0]['alphaMode']=='BLEND' and 'KHR_materials_unlit' in doc['extensionsUsed']
 print('PASS',suffix,'valid buffers, signed near/far relief, unit normals, original left-eye RGBA texture',flush=True)
# Half-SBS expansion must never mix pixels across the packed eye boundary.
half=np.empty((24,64,4),dtype=np.uint8);half[:,:32]=[255,0,0,128];half[:,32:]=[0,255,0,255]
p=out/'half.png';Image.fromarray(half).save(p);target=out/'half.glb';run(p,target,'half-sbs');doc,buf=load(target)
v=doc['bufferViews'][2];texture=np.array(Image.open(io.BytesIO(buf[v['byteOffset']:v['byteOffset']+v['byteLength']])))
assert texture.shape==(24,64,4) and np.all(texture==[255,0,0,128])
assert doc['nodes'][0]['extras']['flatFallback']
print('PASS half-SBS eye separation, alpha and explicit flat fallback for unmatched image',flush=True)
# Invalid/fully-transparent input must preserve an existing destination.
original=(out/'relief.glb').read_bytes();bad=out/'odd.png';Image.new('RGBA',(31,16),'red').save(bad)
run(bad,out/'relief.glb',ok=False);assert (out/'relief.glb').read_bytes()==original
transparent=out/'transparent.png';Image.new('RGBA',(128,32),(100,40,90,0)).save(transparent)
run(transparent,out/'transparent.glb',ok=False);assert not (out/'transparent.glb').exists()
run(source,out/'relief.glb','auto',ok=False);assert (out/'relief.glb').read_bytes()==original
sentinel=out/'sentinel.png';sentinel.write_bytes(b'existing');run(source,sentinel,ok=False);assert sentinel.read_bytes()==b'existing'
assert not list(out.glob('.*.qimgvr-*'))
print('PASS failure cleanup, source/layout guards and preservation of existing files',flush=True)
