#!/usr/bin/env python3
"""Regression for continuous opaque surfaces and low-texture stereo depth."""
import json,os,struct,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
binary=Path(os.environ.get('QIMGVR_BINARY',Path(__file__).resolve().parents[1]/'build-fltk/fltk/qimgvr'))
out=Path(os.environ.get('QIMGVR_GLTF_ARTIFACTS',Path(__file__).resolve().parents[1]/'build-fltk/parallax-tests'));out.mkdir(parents=True,exist_ok=True)
def accessor(doc,buf,index):
 a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
 n={'VEC3':3,'VEC2':2,'SCALAR':1}[a['type']];fmt='f' if a['componentType']==5126 else 'I'
 start=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',n*4)
 return np.array([struct.unpack_from('<'+fmt*n,buf,start+i*stride) for i in range(a['count'])])
def export(name,left,right):
 source=out/(name+'.png');target=out/(name+'.glb')
 Image.fromarray(np.concatenate([left,right],axis=1)).save(source)
 env={k:v for k,v in os.environ.items() if k not in ['DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET']}
 subprocess.run([str(binary),'--export-gltf',str(target),'--input-layout=full-sbs',str(source)],env=env,check=True,capture_output=True)
 data=target.read_bytes();n=struct.unpack_from('<I',data,12)[0];doc=json.loads(data[20:20+n]);buf=data[28+n:]
 xyz=accessor(doc,buf,0);uv=accessor(doc,buf,2);idx=accessor(doc,buf,3).astype(int).reshape(-1,3)
 p=uv[idx];a=p[:,1]-p[:,0];b=p[:,2]-p[:,0]
 coverage=float(np.sum(np.abs(a[:,0]*b[:,1]-a[:,1]*b[:,0]))/2)
 reconstructed=(.064*1.25/(1.25-xyz[:,2])-.064)*left.shape[1]/1.3
 return doc,xyz,uv,reconstructed,coverage
failures=[]
def require(value,message):
 if not value:failures.append(message)
rng=np.random.default_rng(5081);h,w=160,320
left=rng.integers(30,225,(h,w,4),dtype=np.uint8);left[:,:,3]=255
left[35:125,90:235,:3]=[92,128,166]
right=np.empty_like(left);right[:,:-8]=left[:,8:];right[:,-8:]=left[:,-1:]
doc,xyz,uv,d,coverage=export('low-texture',left,right)
patch=(uv[:,0]>.34)&(uv[:,0]<.68)&(uv[:,1]>.30)&(uv[:,1]<.70)
metrics={'lowTextureMedianDisparity':float(np.median(d[patch])),
         'lowTexture95thErrorPixels':float(np.quantile(np.abs(d[patch]-8),.95)),
         'surfaceCoverage':coverage,'matchedFraction':doc['nodes'][0]['extras']['matchedFraction']}
require(doc['materials'][0]['alphaMode']=='OPAQUE','Opaque stereo images need depth writes/occlusion')
require(metrics['lowTexture95thErrorPixels']<1.5,'Low texture region flattens or has incorrect depth')
require(coverage>.995,'Continuous opaque surface contains holes')
# A sloping surface provides continuous signed disparities; no missing match may
# become a fabricated flat-plane island or a deleted triangle patch.
left=rng.integers(30,225,(h,w,4),dtype=np.uint8);left[:,:,3]=255
x=np.arange(w,dtype=float);truth=-4+12*x/(w-1);mapping=x-truth
right=np.empty_like(left)
for y in range(h):
 for c in range(3):right[y,:,c]=np.interp(x,mapping,left[y,:,c]).round().clip(0,255)
right[:,:,3]=255
doc,xyz,uv,d,coverage=export('sloped-surface',left,right)
inside=(uv[:,0]>.1)&(uv[:,0]<.9)&(uv[:,1]>.1)&(uv[:,1]<.9)
error=np.abs(d-(-4+12*uv[:,0]))
metrics.update(slope95thErrorPixels=float(np.quantile(error[inside],.95)),slopeSurfaceCoverage=coverage)
require(metrics['slope95thErrorPixels']<1.5 and coverage>.995,'Sloped surface has incorrect depth or holes')
# A genuine object edge must remain a discontinuity, rather than becoming a
# sloped curtain; filling low confidence pixels must not flatten either side.
right=np.zeros_like(left);right[:h//2,:-8]=left[:h//2,8:];right[h//2:,4:]=left[h//2:,:-4]
doc,xyz,uv,d,coverage=export('depth-edge',left,right)
near=(uv[:,0]>.2)&(uv[:,0]<.8)&(uv[:,1]>.15)&(uv[:,1]<.4)
far=(uv[:,0]>.2)&(uv[:,0]<.8)&(uv[:,1]>.6)&(uv[:,1]<.85)
metrics.update(nearMedianDisparity=float(np.median(d[near])),farMedianDisparity=float(np.median(d[far])))
require(abs(metrics['nearMedianDisparity']-8)<1 and abs(metrics['farMedianDisparity']+4)<1,'Real foreground/background boundary lost')
(out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
assert not failures, (failures,metrics)
print('PASS low-texture depth continuity, opaque depth occlusion, continuous sloped surface coverage, and sharp signed depth boundary')
print(json.dumps(metrics,indent=2))
