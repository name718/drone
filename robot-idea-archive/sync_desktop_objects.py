"""Append the three reviewed CAD concepts to the website seed, preserving existing ideas."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
import json,shutil
from PIL import Image

R=Path(__file__).resolve().parent
SOURCE=R.parent/'desktop-objects-20261025/010_桌面电子摆件_三款'
if not SOURCE.exists():SOURCE=R.parent/'AI原创建模/010_桌面电子摆件_三款'
models=json.loads((SOURCE/'三款方案与校验.json').read_text())
records=json.loads((R/'seed.json').read_text());ids={i['id'] for i in records}
when=datetime.now(timezone(timedelta(hours=8))).isoformat(timespec='seconds')
(R/'seed_files').mkdir(exist_ok=True)
details={
 'tide-station':dict(description='拱形双色桌面小站，96×44×99 mm。前面保留两颗实体按钮，侧面开温湿度通风槽，后面 USB-C 供电。\n\n1.8 寸 TFT 有效显示区按 35.04×28.03 mm 官方参考设计，用户载板、孔位与排针尚需实测；控制板为 55×18 mm 预算。温湿度候选为 SHT40。联网天气作为后续扩展，当前不承诺天气服务已经接入。',next_steps='① 测量现有 TFT 的板框与孔位。\n② 选择温湿度小板，验证控制板热量不会显著影响读数。\n③ 局部试装面框、按键与螺柱，再做电路和固件。'),
 'orbit-light':dict(description='暖光灯环与旋钮组成的小型桌面摆件，100×56.9×125 mm。中空支撑柱预留走线，灯环分开骨架、扩散环和后盖。\n\n环形板按 16 颗 LED 布局预算，尚不是已选定的同尺寸成品。旋钮兼顾调光和按压开关，呼吸节奏由后续固件实现。5V USB 供电，限流、均匀度与温升需验证。',next_steps='① 确定 LED 与恒流/限流方案。\n② 打印扩散环试片，比较透光和亮点。\n③ 选择编码器，确认盲孔旋钮的轴配合与手感。'),
 'note-frame':dict(description='竖放的墨水屏留言相框，76×60.5×126.8 mm。薰衣草色面框、暖陶色定位底座，固定 12° 后倾。屏幕可显示日期、短句与简短留言。\n\n需另购 2.9 寸电子纸：原屏外形 79×36.7×1.05 mm、有效显示区 66.89×29.05 mm；载板平面按 89.5×38 mm 参考，背部高度、FPC 与接插件仍是空间预算。手机写入和翻页功能需要后续软硬件实现。',next_steps='① 锁定电子纸版本与驱动模组厚度。\n② 验证 FPC 弯折和后壳空间。\n③ 实现手机写入与按键翻页，再做底座稳定性测试。')}
for m in models:
 if m['id'] in ids:continue
 out=SOURCE/m['folder'];rep=json.loads((out/'几何检查.json').read_text());assert not rep['collisions'] and rep['valid_parts']
 photos=[]
 for n,(file,caption) in enumerate([('06_外观预览.png','外观与显示内容示意'),('07_背面与接口.png','背面、接口与控制位置'),('08_内部器件布局.png','内部器件空间布局'),('09_拆解预览.png','可拆外壳与内部层次')]):
  name=f"{m['id']}-{n:02d}.jpg"
  with Image.open(out/file) as im:im=im.convert('RGB');im.thumbnail((1500,1500));im.save(R/'seed_assets'/name,quality=89,optimize=True)
  photos.append(dict(id=f"{m['id']}-p{n}",filename=name,kind='render',caption=m['name']+' · '+caption,recorded_at=when,created_at=when))
 files=[]
 for suffix,file,label,kind in [('full','01_完整装配_导入Shapr3D.step','完整装配 · 可编辑 STEP','step'),('structure','02_结构外壳.step','结构外壳 · STEP','step'),('color','05_彩色查看.glb','彩色查看 · GLB','glb')]:
  name=f"{m['id']}-{suffix}"+Path(file).suffix;shutil.copyfile(out/file,R/'seed_files'/name)
  files.append(dict(id=f"{m['id']}-{suffix}",filename=name,label=m['name']+' · '+label,kind=kind,bytes=(out/file).stat().st_size,created_at=when))
 fs=[dict(name=f,state='想法' if f=='手机更新留言' else '已建模') for f in m['features']]
 records.append(dict(id=m['id'],title=m['name'],subtitle=m['subtitle'],category='桌面电子摆件',status='待评审',version='概念 V1',created_at=when,updated_at=when,features=fs,sensors=m['sensors'],cover_id=photos[0]['id'],archived=False,photos=photos,files=files,
  source_note=f"本次原创 CAD，{m['parts']} 个命名部件、{m['solids']} 个 STEP 实体；静态包络检查通过。图片是实际 CAD 渲染。器件尺寸、装配、光学、电气、热与固件尚待验证。云盘模型目录：AI原创建模/010_桌面电子摆件_三款/{m['folder']}。",
  events=[dict(date=when,title='完成概念建模并收录档案',body='建立完整装配、结构分件、内部空间、接口与显示示意；完成 STEP 回读及所建实体的静态干涉检查。此时间为本次建模与归档记录时间。',source='design')],**details[m['id']]))
(R/'seed.json').write_text(json.dumps(records,ensure_ascii=False,indent=2))
print(json.dumps(dict(ideas=len(records),photos=sum(len(i['photos']) for i in records),model_files=sum(len(i.get('files',[])) for i in records)),ensure_ascii=False))
