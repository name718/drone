"""Render only the exported CAD tessellations. No generated-image substitute geometry."""
from pathlib import Path
import json,math,struct
import numpy as np
import vtk
from vtk.util.numpy_support import numpy_to_vtk,numpy_to_vtkIdTypeArray,vtk_to_numpy
R=Path(__file__).resolve().parent
models=json.loads((R/'三款方案与校验.json').read_text())
ren=vtk.vtkRenderer();ren.SetBackground(.85,.88,.83);ren.SetBackground2(.97,.97,.93);ren.GradientBackgroundOn();ren.SetAutomaticLightCreation(False)
win=vtk.vtkRenderWindow();win.SetOffScreenRendering(1);win.SetSize(1500,1500);win.SetMultiSamples(0);win.AddRenderer(ren)
allactors={};datasets={}
for m in models:
 ps=json.loads((R/m['folder']/'渲染数据.json').read_text());datasets[m['id']]=ps
 for p in ps:
  pts=vtk.vtkPoints();pts.SetData(numpy_to_vtk(np.asarray(p['v'],dtype=np.float32),deep=True))
  ff=np.asarray(p['f'],dtype=np.int64);cells=vtk.vtkCellArray();cells.SetCells(len(ff),numpy_to_vtkIdTypeArray(np.column_stack((np.full(len(ff),3,dtype=np.int64),ff)).ravel(),deep=True))
  pd=vtk.vtkPolyData();pd.SetPoints(pts);pd.SetPolys(cells)
  normals=vtk.vtkPolyDataNormals();normals.SetInputData(pd);normals.ComputePointNormalsOn();normals.SplittingOn();normals.SetFeatureAngle(35);normals.ConsistencyOn();normals.Update()
  out=vtk.vtkPolyData();out.DeepCopy(normals.GetOutput());mapper=vtk.vtkPolyDataMapper();mapper.SetInputData(out);mapper.ScalarVisibilityOff()
  actor=vtk.vtkActor();actor.SetMapper(mapper);prop=actor.GetProperty();prop.SetColor(*p['color']);prop.SetInterpolationToPhong();prop.SetAmbient(.2);prop.SetDiffuse(.72);prop.SetSpecular(.22);prop.SetSpecularPower(35)
  if p['mat']=='warm':prop.SetAmbient(.85);prop.SetDiffuse(.3)
  if p['mat']=='metal':prop.SetSpecular(.6)
  if p['kind']=='visual':prop.SetAmbient(.5);prop.SetSpecular(.02)
  ren.AddActor(actor);allactors[(m['id'],p['name'])]=(actor,out)
plane=vtk.vtkPlaneSource();plane.SetOrigin(-2000,-2000,-.12);plane.SetPoint1(2000,-2000,-.12);plane.SetPoint2(-2000,2000,-.12)
mapper=vtk.vtkPolyDataMapper();mapper.SetInputConnection(plane.GetOutputPort());ground=vtk.vtkActor();ground.SetMapper(mapper);ground.GetProperty().SetColor(.87,.89,.84);ground.GetProperty().SetAmbient(.4);ren.AddActor(ground)
for pos,power in [((-230,-320,420),.85),((280,-100,230),.45),((80,300,370),.75),((80,-130,-120),.18)]:
 l=vtk.vtkLight();l.SetLightTypeToSceneLight();l.SetPosition(*pos);l.SetFocalPoint(0,0,60);l.SetPositional(False);l.SetIntensity(power);ren.AddLight(l)
ao=vtk.vtkSSAOPass();ao.SetDelegatePass(vtk.vtkRenderStepsPass());ao.SetRadius(6);ao.SetBias(1);ao.SetKernelSize(64);ao.BlurOn();ren.SetPass(ao)
cam=ren.GetActiveCamera();cam.ParallelProjectionOn();cam.SetViewUp(0,0,1)
def move(actor,v=(0,0,0)):actor.SetPosition(*v)
def select(mid):
 for (model,name),(a,pd) in allactors.items():a.SetVisibility(model==mid);move(a)
 ground.SetPosition(0,0,0)
def render(path,pos,target,scale,size=(1500,1500)):
 win.SetSize(*size);cam.SetPosition(*pos);cam.SetFocalPoint(*target);cam.SetViewUp(0,0,1);cam.SetParallelScale(scale*size[1]/size[0]/2);cam.SetClippingRange(50,1800);win.Render()
 cap=vtk.vtkWindowToImageFilter();cap.SetInput(win);cap.SetInputBufferTypeToRGB();cap.ReadFrontBufferOff();cap.Update()
 w=vtk.vtkPNGWriter();w.SetFileName(str(path));w.SetInputConnection(cap.GetOutputPort());w.Write();print('RENDER '+str(path.name),flush=True)
def glb(mid,path):
 ps=datasets[mid];doc={'asset':{'version':'2.0','generator':'CadQuery 2.8 / VTK'},'scene':0,'scenes':[{'nodes':list(range(len(ps)))}],'nodes':[],'meshes':[],'materials':[],'accessors':[],'bufferViews':[],'buffers':[{'byteLength':0}]};blob=bytearray();materials={}
 def acc(arr,typ,ctype,target,minmax=False):
  while len(blob)%4:blob.append(0)
  off=len(blob);raw=arr.tobytes();blob.extend(raw);ix=len(doc['bufferViews']);doc['bufferViews'].append({'buffer':0,'byteOffset':off,'byteLength':len(raw),'target':target})
  a={'bufferView':ix,'componentType':ctype,'count':len(arr),'type':typ}
  if minmax:a.update(min=arr.min(axis=0).tolist(),max=arr.max(axis=0).tolist())
  idx=len(doc['accessors']);doc['accessors'].append(a);return idx
 for p in ps:
  pd=allactors[(mid,p['name'])][1];v=vtk_to_numpy(pd.GetPoints().GetData()).astype(np.float32);n=vtk_to_numpy(pd.GetPointData().GetNormals()).astype(np.float32);f=vtk_to_numpy(pd.GetPolys().GetData()).reshape(-1,4)[:,1:].astype(np.uint32).ravel()
  v=np.column_stack((v[:,0],v[:,2],-v[:,1])).astype(np.float32)*.001;n=np.column_stack((n[:,0],n[:,2],-n[:,1])).astype(np.float32)
  pa=acc(v,'VEC3',5126,34962,True);na=acc(n,'VEC3',5126,34962);ia=acc(f,'SCALAR',5125,34963)
  if p['mat'] not in materials:
   materials[p['mat']]=len(doc['materials']);mat={'name':p['mat'],'pbrMetallicRoughness':{'baseColorFactor':p['color']+[1],'metallicFactor':.55 if p['mat']=='metal' else 0,'roughnessFactor':.55}}
   if p['mat']=='warm':mat['emissiveFactor']=[.5,.27,.07]
   doc['materials'].append(mat)
  doc['nodes'].append({'name':p['name'],'mesh':len(doc['meshes']),'extras':{'part_cn':p['cn'],'kind':p['kind'],'concept_only':True}})
  doc['meshes'].append({'name':p['name'],'primitives':[{'attributes':{'POSITION':pa,'NORMAL':na},'indices':ia,'material':materials[p['mat']]}]})
 while len(blob)%4:blob.append(0)
 doc['buffers'][0]['byteLength']=len(blob);j=json.dumps(doc,separators=(',',':'),ensure_ascii=False).encode();j+=b' '*((-len(j))%4)
 total=12+8+len(j)+8+len(blob);path.write_bytes(struct.pack('<4sII',b'glTF',2,total)+struct.pack('<I4s',len(j),b'JSON')+j+struct.pack('<I4s',len(blob),b'BIN\0')+blob)
for m in models:
 mid=m['id'];out=R/m['folder'];h=m['dimensions_mm'][2];z=h*.5;scale=max(160,h*1.48)
 select(mid);render(out/'06_外观预览.png',(190,-350,210),(0,0,z),scale)
 render(out/'07_背面与接口.png',(-200,350,220),(0,0,z),scale)
 for p in datasets[mid]:allactors[(mid,p['name'])][0].SetVisibility(not p['exterior'] and p['kind']!='visual')
 render(out/'08_内部器件布局.png',(180,-310,230),(0,0,z),scale)
 select(mid)
 for p in datasets[mid]:move(allactors[(mid,p['name'])][0],p['explode'])
 ground.SetPosition(0,0,-19);render(out/'09_拆解预览.png',(240,-420,260),(0,0,z+7),scale*1.35)
 glb(mid,out/'05_彩色查看.glb')
for n,m in enumerate(models):
 select(m['id']) if False else None
 for p in datasets[m['id']]:a=allactors[(m['id'],p['name'])][0];a.SetVisibility(True);move(a,((n-1)*137,0,0))
ground.SetPosition(0,0,0)
render(R/'00_三款同台预览.png',(230,-670,270),(0,0,65),450,(2400,1350))
win.Finalize();print('RENDER_COMPLETE',flush=True)
