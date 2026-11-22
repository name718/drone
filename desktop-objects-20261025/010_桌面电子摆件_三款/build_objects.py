"""Three original desktop electronics concepts, mm. Real B-rep geometry and editable STEP.
Displays use cited active-area references; all PCB/LED/control envelopes remain provisional.
"""
from pathlib import Path
import cadquery as cq
import math,json,itertools
from OCP.Bnd import Bnd_Box
from OCP.BRepBndLib import BRepBndLib
R=Path(__file__).resolve().parent
COL={'cream':(.92,.90,.83),'sage':(.40,.60,.52),'clay':(.74,.43,.32),'lavender':(.67,.68,.80),'paper':(.94,.94,.86),'dark':(.08,.12,.13),'black':(.025,.035,.035),'gold':(.85,.63,.32),'warm':(1.0,.81,.47),'metal':(.68,.71,.70),'pcb':(.04,.36,.25),'chip':(.16,.19,.18),'rubber':(.07,.09,.09),'blue':(.30,.55,.66),'glass':(.035,.07,.08),'white':(.92,.95,.91)}
FONT='/System/Library/Fonts/Supplemental/Arial.ttf'
CJK='/System/Library/Fonts/STHeiti Light.ttc'
parts=[];models=[]
def log(v):print(v,flush=True)
def box(w,d,h,x=0,y=0,z=0):return cq.Workplane('XY').box(w,d,h).translate((x,y,z)).val()
def rr(w,d,h,x=0,y=0,z=0,r=2):return cq.Workplane('XY').box(w,d,h).edges('|Z').fillet(r).translate((x,y,z)).val()
def soft(w,d,h,x=0,y=0,z=0,r=2):return cq.Workplane('XY').box(w,d,h).edges().fillet(r).translate((x,y,z)).val()
def face(w,h,d,x=0,y=0,z=0,r=1):return cq.Workplane('XZ').rect(w,h).extrude(d).edges('|Y').fillet(r).translate((x,y+d/2,z)).val()
def cyl(radius,length,x=0,y=0,z=0,axis=(0,0,1)):return cq.Solid.makeCylinder(radius,length,cq.Vector(x,y,z),cq.Vector(*axis))
def ring(ro,ri,d,y=0,z=0):return cq.Workplane('XZ').circle(ro).circle(ri).extrude(d).translate((0,y+d/2,z)).val()
def text(t,size,x,y,z,mat='paper',T=None):
 sh=cq.Workplane('XZ').text(t,size,.035,combine=False,fontPath=CJK if any(ord(c)>127 for c in t) else FONT,halign='center',valign='center').val().translate((x,y,z))
 add('DisplayText'+str(len(parts)),t+'（显示示意）',sh,mat,'visual','display',False,(0,-25,0),'屏幕显示内容示意，不是打印零件，也不表示固件已完成',T)
def add(name,cn,s,mat='cream',kind='structure',group='case',exterior=False,explode=(0,0,0),note='原创概念结构；固定、公差、线束与实物配合待验证',T=None):
 if T:s=T(s)
 s=s.clean();assert s.isValid() and len(s.Solids()),(name,'invalid')
 if kind=='structure':assert len(s.Solids())==1,(name,'disconnected structure',len(s.Solids()))
 parts.append(dict(name=name,cn=cn,shape=s,mat=mat,color=COL[mat],kind=kind,group=group,exterior=exterior,explode=explode,note=note));log('ADD '+name);return s
def bounds(s):
 b=Bnd_Box();BRepBndLib.AddOptimal_s(s.wrapped,b,False,False);return cq.BoundBox(b)
def overlap(a,b):return all(min(getattr(a,k+'max'),getattr(b,k+'max'))-max(getattr(a,k+'min'),getattr(b,k+'min'))>.002 for k in 'xyz')
def usb(y,z,T=None):
 sh=face(8.8,3.4,5.8,y=y,z=z,r=.7).cut(face(6.8,1.8,5.0,y=y+.9,z=z,r=.5))
 add('USB_C','USB-C 插座空间',sh,'metal','envelope','electronics',False,(0,18,0),'接口仅为安装空间预算，型号与板边位置尚未定型',T)
def feet(xs,ys):
 for k,(x,y) in enumerate(itertools.product(xs,ys)):add('Pad'+str(k),'底部防滑垫',cyl(4,1,x,y,0),'rubber','envelope','base',True,(x*.2,y*.2,-5),'防滑垫为 8×1 mm 候选预算，需按实际胶垫确认')
def finish(folder,meta):
 out=R/folder;out.mkdir(parents=True,exist_ok=True);(out/'结构分件_STEP').mkdir(exist_ok=True)
 def assembly(ps,path,explode=False):
  a=cq.Assembly(name=meta['id'])
  for p in ps:a.add(p['shape'].translate(tuple(p['explode'])) if explode else p['shape'],name=p['name'],color=cq.Color(*p['color']))
  a.export(str(out/path))
 assembly(parts,'01_完整装配_导入Shapr3D.step');assembly([p for p in parts if p['kind']=='structure'],'02_结构外壳.step')
 assembly([p for p in parts if not p['exterior'] and p['kind']!='visual'],'03_内部器件布局.step');assembly(parts,'04_爆炸装配.step',True)
 check={'parts':len(parts),'expected_solids':sum(len(p['shape'].Solids()) for p in parts),'valid_parts':all(p['shape'].isValid() for p in parts),'structure_roundtrips':[],'collisions':[],'tested_pairs':0}
 for p in parts:
  if p['kind']=='structure':
   f=out/'结构分件_STEP'/f"{p['name']}_{p['cn']}.step";cq.exporters.export(p['shape'],str(f));back=cq.importers.importStep(str(f)).val()
   # Tight integration avoids quadrature differences on trimmed curved faces.
   before_volume=p['shape'].Volume(1e-9);after_volume=back.Volume(1e-9)
   rel=abs(after_volume-before_volume)/max(before_volume,1)
   if rel>=1e-6:
    p['shape'].exportBrep(str(out/(p['name']+'_debug.brep')))
    log('VOLUME_DIAGNOSTIC '+json.dumps(dict(name=p['name'],before=before_volume,after=after_volume,valid=back.isValid())))
   assert back.isValid() and rel<1e-6,(p['name'],rel)
   check['structure_roundtrips'].append(dict(name=p['name'],relative_volume_error=rel))
 back=cq.importers.importStep(str(out/'01_完整装配_导入Shapr3D.step')).val();check['reimported_solids']=len(back.Solids())
 assert back.isValid() and len(back.Solids())==check['expected_solids']
 physical=[p for p in parts if p['kind']!='visual'];bb={p['name']:bounds(p['shape']) for p in physical}
 for a,b in itertools.combinations(physical,2):
  if not overlap(bb[a['name']],bb[b['name']]):continue
  vol=a['shape'].intersect(b['shape']).Volume();check['tested_pairs']+=1
  if vol>.015:check['collisions'].append(dict(a=a['name'],b=b['name'],volume_mm3=vol))
 allbox=bounds(cq.Compound.makeCompound([p['shape'] for p in parts]));check['dimensions_mm']=[allbox.xlen,allbox.ylen,allbox.zlen]
 data=[]
 for p in parts:
  v,f=p['shape'].tessellate(.12,.18)
  data.append({k:val for k,val in p.items() if k!='shape'}|{'v':[[a.x,a.y,a.z] for a in v],'f':[list(t) for t in f]})
 (out/'渲染数据.json').write_text(json.dumps(data,ensure_ascii=False,separators=(',',':')))
 (out/'几何检查.json').write_text(json.dumps(check,ensure_ascii=False,indent=2))
 records=[{k:v for k,v in p.items() if k!='shape'}|{'solids':len(p['shape'].Solids()),'volume_mm3':p['shape'].Volume()} for p in parts]
 (out/'参数与零件说明.json').write_text(json.dumps(meta|{'parts':records,'dimensions_mm':check['dimensions_mm']},ensure_ascii=False,indent=2))
 models.append(meta|{'folder':folder,'dimensions_mm':check['dimensions_mm'],'parts':len(parts),'solids':check['expected_solids'],'collisions':check['collisions']})
 log('FINISH '+folder+' '+json.dumps(check,ensure_ascii=False))

# A — TIDE: arched weather/clock enclosure, true 1.8-inch active area.
def arch(half,bottom,topcenter,r,depth,y):
 return cq.Workplane('XZ').moveTo(-half,bottom).lineTo(half,bottom).lineTo(half,topcenter).threePointArc((0,topcenter+r),(-half,topcenter)).close().extrude(depth).translate((0,y+depth/2,0)).val()
outer=arch(43,12,56,43,30,0)
shell=outer.cut(arch(40.5,14.5,56,40.5,25,0))
shell=shell.cut(face(52.6,44.6,7,y=-14,z=60,r=6.3)).cut(face(9.6,4.2,8,y=14,z=22,r=.9))
for x in [-18,18]:shell=shell.cut(cyl(4.3,9,x,-18,30,(0,1,0)))
for z in [34,38,42]:shell=shell.cut(box(8,10,1.6,x=42,y=0,z=z))
front=shell.intersect(box(160,39.8,160,y=-20.1,z=75));rear=shell.intersect(box(160,39.8,160,y=20.1,z=75))
for x,z in [(-33,24),(33,24),(0,88)]:
 front=front.fuse(cyl(3.5,11.9,x,-12.7,z,(0,1,0)).cut(cyl(.85,10.5,x,-10.7,z,(0,1,0))))
 rear=rear.fuse(cyl(3.5,11.9,x,.8,z,(0,1,0)).cut(cyl(1.2,16,x,.4,z,(0,1,0))))
 rear=rear.cut(cyl(1.2,6,x,11.5,z,(0,1,0)))
add('FrontShell','拱形前壳与螺柱',front,'cream',exterior=True,explode=(0,-30,0))
add('RearShell','后壳与通风孔',rear,'sage',exterior=True,explode=(0,34,0))
add('Plinth','低重心底座',rr(96,44,11,z=6.5,r=9),'clay',group='base',exterior=True,explode=(0,0,-17))
feet([-34,34],[-14,14])
bezel=face(52,44,1.3,y=-14.15,z=60,r=6).cut(face(35.9,28.9,3,y=-14.2,z=60,r=.5))
add('DisplayBezel','屏幕面框',bezel,'dark',exterior=True,explode=(0,-28,0))
add('TFTGlass','LCD 玻璃空间',face(35.8,28.8,.8,y=-14.2,z=60,r=.3),'glass','envelope','display',False,(0,-14,0),'按显示区适配的玻璃包络，用户模块板框与厚度仍待测')
add('TFTStack','LCD 背光层空间',face(40,32,2.8,y=-12,z=60,r=.6),'dark','envelope','display',False,(0,-10,0))
add('TFTPCB','1.8寸 TFT 载板空间',box(45,1.6,38,y=-9.6,z=60),'pcb','envelope','display',False,(0,8,0),'显示区 35.04×28.03 mm 为官方参考；45×38 mm 载板是预算，未套用用户实测孔位')
add('ActiveDisplay','35.04×28.03 有效显示区',box(35.04,.02,28.03,y=-14.615,z=60),'black','visual','display')
text('10:25',6.2,0,-14.66,64);text('24C',3.1,-8,-14.66,53,'warm');text('55%',3.1,8,-14.66,53,'sage')
add('SunMark','顶部太阳装饰',cyl(3.4,.06,0,-15.06,88,(0,1,0)),'gold','visual','case',True)
for n,x in enumerate([-18,18]):
 add('Button'+str(n),'前面切换按钮',cyl(3.9,3,x,-16,30,(0,1,0)),'sage','structure','controls',True,(x*.3,-20,0))
 add('SwitchPCB'+str(n),'按键小板空间',box(6,1.2,6,x=x,y=-9,z=30),'pcb','envelope','controls')
 add('Switch'+str(n),'轻触开关空间',box(3.8,2.8,3.8,x=x,y=-11.1,z=30),'chip','envelope','controls')
add('MainPCB','控制板空间',box(55,18,1.6,y=-1,z=23.5),'pcb','envelope','electronics',False,(0,0,13),'控制/供电板预算，尚未设计电路')
add('MCU','无线主控空间',box(18,12,3.4,x=-12,y=-1,z=26.1),'metal','envelope','electronics')
add('SensorPCB','温湿度小板空间',box(8,8,1.6,x=32,z=36),'pcb','envelope','electronics')
add('SHT40','温湿度芯片空间',box(1.5,1.5,.8,x=32,z=37.3),'metal','envelope','electronics',note='SHT40 为温湿度候选；小板尺寸、温升隔离与开孔需实测')
usb(12.9,22)
finish('A_潮汐_天气小站',dict(id='tide-station',name='潮汐 · 天气小站',subtitle='拱形小屏幕、实体按钮与侧边温湿度通风口。',features=['时间显示','室内温湿度','按键切换页面'],sensors=['SHT40 温湿度（候选）'],power='5V USB，暂不放电池',notes='联网天气可后续扩展；当前只完成屏幕、按键和传感器的结构空间布局。'))

# B — ORBIT: sculptural warm ring with one rotary control.
parts=[]
lower=soft(100,56,14,z=8,r=3).cut(rr(95,51,15,z=11,r=5))
for x,y in itertools.product([-39,39],[-18,18]):lower=lower.fuse(cyl(3.5,11.7,x,y,3.3).cut(cyl(.85,10,x,y,6)))
lower=lower.cut(face(9.6,4.2,8,y=27,z=8,r=.9))
add('BaseLower','底座下壳与螺柱',lower,'sage',group='base',exterior=True,explode=(0,0,-17))
cap=rr(100,56,3.6,z=17.2,r=7)
for x,y in itertools.product([-39,39],[-18,18]):cap=cap.cut(cyl(1.2,6,x,y,14.8))
cap=cap.cut(cyl(4,8,31,-9,14))
stem=soft(13,15,27,z=31.5,r=2.2)
channel=ring(40.8,33.7,10.1,y=-2.05,z=82)
frame=ring(43,31.5,12,y=-1,z=82).fuse(stem).fuse(cap).cut(channel).cut(cyl(2.6,32,z=14))
add('RingFrame','灯环骨架与中空支撑',frame,'cream',exterior=True,explode=(0,0,22))
add('Diffuser','前置暖光扩散环',ring(40.5,34.0,1.8,y=-6,z=82),'warm',exterior=True,explode=(0,-22,23),note='半透明扩散件概念，透光均匀度和材料尚未测试')
add('RingRear','灯环后盖',ring(42.8,31.7,1.6,y=6,z=82).cut(box(14,20,29,z=31.5)),'clay',exterior=True,explode=(0,24,23),note='后盖底部为中空支撑柱让位，留名义边隙；最终固定方式待试装')
add('LEDPCB','环形 LED 板空间',ring(40.3,34.2,1.6,y=2,z=82),'pcb','envelope','led',False,(0,13,22),'定制环形 PCB 布局预算，未指定可直接购买的同尺寸成品灯环')
for n in range(16):
 a=2*math.pi*n/16;add('LED'+str(n),'暖光LED空间',box(3,1.5,3,x=37.3*math.cos(a),y=.45,z=82+37.3*math.sin(a)),'white','envelope','led',False,(0,-5,22))
add('ControlPCB','控制与限流板空间',box(58,25,1.6,z=6),'pcb','envelope','electronics',False,(0,0,7))
add('ControlMCU','主控与电源空间',box(19,16,4,x=-13,z=8.9),'metal','envelope','electronics')
add('Encoder','旋钮编码器空间',box(12,12,8.6,x=31,y=-9,z=11.1),'metal','envelope','controls')
add('EncoderShaft','旋钮轴空间',cyl(3,8,31,-9,15.4),'metal','envelope','controls')
knob=cyl(10.5,8,31,-9,19.8).cut(cyl(3.2,5,31,-9,19.6))
add('Knob','顶部调光旋钮',knob,'clay','structure','controls',True,(0,0,15))
add('KnobMark','旋钮标记',box(.8,4,.04,x=31,y=-15,z=27.84),'cream','visual','controls',True)
usb(26,8);feet([-38,38],[-17,17])
finish('B_环光_旋钮呼吸灯',dict(id='orbit-light',name='环光 · 旋钮呼吸灯',subtitle='悬起一圈暖光，用一只旋钮控制明暗与呼吸节奏。',features=['旋钮调光','按压开关','呼吸灯模式'],sensors=['旋转编码器 / 按压开关（候选）'],power='5V USB；限流、功耗与温升需后续验证',notes='16 颗 LED 和环形板是空间预算；不承诺已实现光学均匀性或灯具性能。'))

# C — NOTE: portrait 2.9-inch e-paper, tilted frame in a cupped base.
parts=[]
T=lambda s:s.rotate((0,0,20),(1,0,20),-12)
outer=soft(60,18,108,z=74,r=4)
shell=outer.cut(soft(55,13,103,z=74,r=2)).cut(face(30,68,7,y=-8,z=74,r=1))
shell=shell.cut(face(9.6,4.2,8,y=8.5,z=68,r=.9))
for x in [-13,13]:shell=shell.cut(cyl(3.7,8,x,5.5,113,(0,1,0)))
front=shell.intersect(box(100,39.8,160,y=-20.1,z=75));rear=shell.intersect(box(100,39.8,160,y=20.1,z=75))
for x,z in itertools.product([-24,24],[32,116]):
 front=front.fuse(cyl(2.8,5.9,x,-6.7,z,(0,1,0)).cut(cyl(.85,4.5,x,-5.0,z,(0,1,0))))
 rear=rear.fuse(cyl(2.8,5.9,x,.8,z,(0,1,0)).cut(cyl(1.2,12,x,.4,z,(0,1,0))))
 rear=rear.cut(cyl(1.2,6,x,5.0,z,(0,1,0)))
add('FrameFront','倾斜相框前壳',front,'lavender',exterior=True,explode=(0,-24,14),T=T)
add('FrameRear','相框后壳与按键孔',rear,'cream',exterior=True,explode=(0,26,14),T=T)
stand=rr(76,60,12,z=7,r=11).fuse(rr(50,29,14,y=5,z=20,r=4)).cut(T(soft(60.6,18.6,108.6,z=74,r=4.1)))
add('Stand','带相框定位槽的底座',stand,'clay',group='base',exterior=True,explode=(0,0,-18),note='固定 12° 后倾定位槽，留 0.3mm 名义边隙；打印公差、固定和稳定性待试装')
feet([-27,27],[-20,20])
add('EpaperGlass','2.9寸电子纸玻璃',box(36.7,1.05,79,y=-5.775,z=74),'paper','envelope','display',False,(0,-12,14),'微雪 2.9寸原屏外形 79×36.7×1.05 mm 官方参考，型号版本需复核',T)
add('EpaperActive','29.05×66.89显示区',box(29.05,.02,66.89,y=-6.32,z=74),'paper','visual','display',T=T)
add('EpaperModule','电子纸模块载板空间',box(38,1.6,89.5,y=-2.1,z=74),'pcb','envelope','display',False,(0,7,14),'模块平面 89.5×38mm 为官方参考；PCB厚度、元件和FPC仅预算，未声称全部为实物精确尺寸',T)
text('10.25',5,0,-6.36,96,'dark',T);text('慢慢来',7.8,0,-6.36,77,'dark',T);text('SUNDAY',2.6,0,-6.36,53,'dark',T)
add('MemoRule','屏幕分隔线示意',box(20,.035,.22,y=-6.36,z=89),'dark','visual','display',T=T)
add('WirelessPCB','无线更新小板空间',box(20,1.6,30,y=3,z=45),'pcb','envelope','electronics',False,(0,15,14),'小型 ESP32 路线预算，手机写入流程及固件尚未实现',T)
add('WirelessModule','无线模块高度空间',box(16,2.5,14,y=5.05,z=45),'metal','envelope','electronics',T=T)
for n,x in enumerate([-13,13]):
 add('RearButton'+str(n),'背面翻页或配对按钮',cyl(3.3,3,x,7.2,113,(0,1,0)),'sage','structure','controls',True,(0,20,14),T=T)
 add('RearSwitch'+str(n),'背部轻触开关空间',box(4,2,4,x=x,y=5.8,z=113),'chip','envelope','controls',T=T)
usb(7.8,68,T)
finish('C_留白_电子留言相框',dict(id='note-frame',name='留白 · 电子留言相框',subtitle='一块竖放的电子纸，留下日期、短句和想记住的小事。',features=['日期与短句显示','手机更新留言','背面按键翻页'],sensors=['背部按键（候选）'],power='5V USB；电子纸保图不等于控制系统完全零功耗',notes='2.9寸电子纸需要另购。字样为渲染内容，不表示手机写入、翻页和日期固件已经完成。'))

(R/'三款方案与校验.json').write_text(json.dumps(models,ensure_ascii=False,indent=2))
log('ALL_COMPLETE '+json.dumps([{'name':m['name'],'collisions':m['collisions'],'dimensions':m['dimensions_mm']} for m in models],ensure_ascii=False))
