#!/usr/bin/env python3
"""Writes small test models for the editor tests: the same 2 x 1 x 0.5 m box
(Blender axes, meters) as a .glb, as a .gltf with its .bin and pictures
beside it, and as an .obj with an .mtl. usage: make_test_models.py <folder> [length]"""
import json, struct, base64, zlib, os, sys
out = sys.argv[1]
os.makedirs(os.path.join(out, "sub"), exist_ok=True)
# Blender-space box: 2 m along X, 1 m along Y, 0.5 m tall; glTF stands Y up: (x, z, -y)
def b2g(p): return (p[0], p[2], -p[1])
X,Y,Z = float(sys.argv[2]) if len(sys.argv)>2 else 2.0, 1.0, 0.5
corners = [(x,y,z) for x in (0,X) for y in (0,Y) for z in (0,Z)]
faces = [ (0,1,3,2), (4,6,7,5), (0,4,5,1), (2,3,7,6), (0,2,6,4), (1,5,7,3) ]
pos=[];uv=[];idx=[]
for f in faces:
    base=len(pos)
    for k,c in enumerate(f):
        pos.append(b2g(corners[c])); uv.append([(0,0),(1,0),(1,1),(0,1)][k])
    idx += [base,base+1,base+2, base,base+2,base+3]
def png(w,h,rgba):
    raw=b''.join(b'\x00'+bytes(rgba)*w for _ in range(h))
    def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
col=png(4,4,(200,120,60,255)); mr=png(4,4,(255,128,0,255)); nrm=png(4,4,(128,128,255,255))
posb=b''.join(struct.pack('<3f',*p) for p in pos); uvb=b''.join(struct.pack('<2f',*t) for t in uv); idxb=b''.join(struct.pack('<H',i) for i in idx)
def pad(b): return b+b'\x00'*((4-len(b)%4)%4)
mins=[min(p[i] for p in pos) for i in range(3)]; maxs=[max(p[i] for p in pos) for i in range(3)]
def doc(images_inline):
    d={"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"name":"Box"}],
       "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],
       "materials":[{"name":"Crate Wood.001","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1},"metallicFactor":1,"roughnessFactor":1},"normalTexture":{"index":2}}],
       "textures":[{"source":0},{"source":1},{"source":2}],
       "accessors":[{"bufferView":0,"componentType":5126,"count":len(pos),"type":"VEC3","min":mins,"max":maxs},
                    {"bufferView":1,"componentType":5126,"count":len(uv),"type":"VEC2"},
                    {"bufferView":2,"componentType":5123,"count":len(idx),"type":"SCALAR"}]}
    return d
# GLB
bin_=b''; views=[]
for b in (posb,uvb,idxb):
    views.append({"buffer":0,"byteOffset":len(bin_),"byteLength":len(b)}); bin_+=pad(b)
imgs=[]
for name,b in (("col",col),("mr",mr),("nrm",nrm)):
    views.append({"buffer":0,"byteOffset":len(bin_),"byteLength":len(b)}); imgs.append({"bufferView":len(views)-1,"mimeType":"image/png","name":name}); bin_+=pad(b)
d=doc(True); d["bufferViews"]=views; d["images"]=imgs; d["buffers"]=[{"byteLength":len(bin_)}]
js=json.dumps(d).encode(); js+=b' '*((4-len(js)%4)%4)
glb=struct.pack('<III',0x46546C67,2,12+8+len(js)+8+len(bin_))+struct.pack('<II',len(js),0x4E4F534A)+js+struct.pack('<II',len(bin_),0x004E4942)+bin_
open(os.path.join(out,'Wooden Crate.glb'),'wb').write(glb)
# glTF + external bin + pictures (one with a space in its name)
geo=b''; gviews=[]
for b in (posb,uvb,idxb):
    gviews.append({"buffer":0,"byteOffset":len(geo),"byteLength":len(b)}); geo+=pad(b)
open(os.path.join(out,'sub','crate.bin'),'wb').write(geo)
open(os.path.join(out,'sub','my col.png'),'wb').write(col)
open(os.path.join(out,'sub','mr.png'),'wb').write(mr)
d=doc(False); d["bufferViews"]=gviews; d["buffers"]=[{"uri":"crate.bin","byteLength":len(geo)}]
d["images"]=[{"uri":"my%20col.png"},{"uri":"mr.png"},{"uri":"data:image/png;base64,"+base64.b64encode(nrm).decode()}]
open(os.path.join(out,'sub','crate.gltf'),'w').write(json.dumps(d))
# OBJ the way Blender writes it (Y up: x, z, -y) with an .mtl
with open(os.path.join(out,'crate_obj.obj'),'w') as f:
    f.write("mtllib crate_obj.mtl\no Box\n")
    for c in corners:
        g=b2g(c); f.write("v %f %f %f\n"%g)
    f.write("vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl Crate Wood.001\n")
    for fc in faces:
        f.write("f "+" ".join("%d/%d"%(c+1,k+1) for k,c in enumerate(fc))+"\n")
open(os.path.join(out,'crate_obj.mtl'),'w').write("newmtl Crate Wood.001\nNs 250.0\nKd 0.8 0.5 0.3\nmap_Kd my col.png\n")
open(os.path.join(out,'my col.png'),'wb').write(col)
print("ok")
