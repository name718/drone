from pathlib import Path
import json,hashlib,struct
from PIL import Image
R=Path(__file__).resolve().parent
models=json.loads((R/'三款方案与校验.json').read_text())
sources=[dict(title='1.8寸LCD有效显示区参考',url='https://www.waveshare.com/wiki/1.8inch_LCD_Module',dimensions='35.04×28.03 mm',limit='用户的载板、孔位、排针与厚度尚待测量'),dict(title='SHT40 温湿度候选',url='https://sensirion.com/products/catalog/SHT40',limit='仅温湿度候选，小板与热隔离结构待设计'),dict(title='2.9寸电子纸原屏参考',url='https://www.waveshare.com/product/displays/e-paper/2.9inch-e-paper.htm',dimensions='原屏79×36.7×1.05；有效显示区66.89×29.05 mm',limit='版本、FPC与实际模组厚度待复核'),dict(title='2.9寸电子纸模块参考',url='https://www.waveshare.com/product/2.9inch-e-paper-module.htm',dimensions='模块平面89.5×38 mm',limit='PCB厚度、背部器件、接插件仅为预算')]
(R/'参考来源与尺寸边界.json').write_text(json.dumps(sources,ensure_ascii=False,indent=2))
for m in models:
 out=R/m['folder'];rep=json.loads((out/'几何检查.json').read_text())
 assert rep['valid_parts'] and rep['reimported_solids']==rep['expected_solids'] and not rep['collisions']
 for filename in ['06_外观预览.png','07_背面与接口.png','08_内部器件布局.png','09_拆解预览.png']:
  with Image.open(out/filename) as im:im.verify()
 with (out/'05_彩色查看.glb').open('rb') as f:
  magic,ver,total=struct.unpack('<4sII',f.read(12));length,tag=struct.unpack('<I4s',f.read(8));gltf=json.loads(f.read(length))
 assert magic==b'glTF' and ver==2 and len(gltf['meshes'])==rep['parts'] and total==(out/'05_彩色查看.glb').stat().st_size
 dims='×'.join(f'{v:.1f}' for v in rep['dimensions_mm'])
 note=f'''{m['name']}｜原创概念模型｜2026-10-25

Shapr3D 导入：01_完整装配_导入Shapr3D.step
这是完整外壳、内部器件空间和显示示意的装配，包含 {rep['parts']} 个命名部件、{rep['expected_solids']} 个 STEP 实体。
STEP 可选择实体并直接修改，不包含 Shapr3D 原生建模历史。
结构件单独在 结构分件_STEP/；02只有外壳，03为内部布局，04为爆炸装配。
05 GLB 用于彩色网格查看；继续建模优先 STEP。

功能目标：{'、'.join(m['features'])}。
输入与传感器：{'、'.join(m['sensors'])}。
供电路线：{m['power']}。
整机当前几何包络（宽×深×高）：{dims} mm。
{m['notes']}

当前阶段与边界
这是用于评审外观、拆壳和器件空间的模型，不是已经通电工作的产品。
自研控制板、无线模块、LED/编码器/开关等都有明确标注的包络，尚未完成原理图和PCB。
时钟、温湿度读取、呼吸灯、手机写入等功能仍需电路与固件实现。
显示文字为 visual 类实体，不是打印零件；未将黑色面罩冒充有效显示区。
扩散环渲染不代表实际照度、均匀度和安全温升。
固定螺柱/孔位、面框固定、线束、连接器、装配间隙、强度和稳定性仍需按实物与工艺复核。
请先定型实际器件并做局部试装，再导出最终打印件。

校验
全部实体有效，完整STEP导出再导入的实体数一致。
{len(rep['structure_roundtrips'])} 个结构分件完成导出回读和高精度体积对比。
当前建出的物理包络没有超过 0.015 mm³ 阈值的相交；文字和屏幕图案不参加该检查。
这是静态几何检查，不是强度、光学、电气、热、稳定性或实机验收。
几何检查.json 与 参数与零件说明.json 逐项给出依据。

图片全部由同一套CAD网格渲染，形状与STEP一致。
公共规格来源见上级 参考来源与尺寸边界.json；未下载或冒充精确的成品电路模块。
建模源 build_objects.py、渲染源 render_objects.py 在上级目录。
'''
 (out/'00_先读我.txt').write_text(note)
 for p in out.glob('*_debug.brep'):p.unlink()
(R/'00_先看三款.txt').write_text('''010 桌面电子摆件｜三款原创概念｜2026-10-25

先看 00_三款同台预览.png；左：潮汐天气小站，中：环光呼吸灯，右：留白留言相框。
打开预览.html 为本地图片与模型入口，不依赖云服务器。

A 潮汐：时间、室内温湿度、按钮切换，96×44×99 mm。
采用现有1.8寸TFT路线的有效显示区参考；载板与孔位仍要实测。
B 环光：旋钮调光、按压开关、呼吸灯，100×56.9×125 mm。
环形LED板与扩散件是定制布局，需要另做电路、限流和透光测试。
C 留白：日期短句、手机更新留言、背面翻页，76×60.5×126.8 mm。
使用需另购的2.9寸电子纸路线，竖放，固定12°后倾。

每款导入 01_完整装配_导入Shapr3D.step 可修改；分件也已导出。
三款合计的校验见 三款方案与校验.json，各款还有详细校验与零件说明。
当前为结构与功能布局概念，不是电路或可直接批量打印的生产定稿。
网页档案同步收录这三款的功能、图片、时间、评分入口和模型下载。
文件写入本机OneDrive目录不等于已核验其他设备同步。
''')
html='<!doctype html><html lang="zh-CN"><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>桌面电子摆件｜三款</title><style>body{margin:0;background:#f4f4ed;color:#304d3d;font:16px/1.8 -apple-system,"PingFang SC",sans-serif}main{max-width:1250px;margin:auto;padding:30px 22px}h1{font-size:34px}.hero{width:100%;border-radius:18px}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:18px}.card{background:white;padding:18px;border-radius:16px}.card img{width:100%;border-radius:12px}.card h2{font-size:22px}.card p{font-size:14px;color:#7a8c70}a{color:#315c42}.note{padding:17px;background:#e5ebda;border-radius:10px;margin:24px 0}@media(max-width:750px){.grid{grid-template-columns:1fr}}</style><main><small>DESK OBJECTS / 010</small><h1>给桌面，添一点电子小物。</h1><img class="hero" src="00_三款同台预览.png" alt="三款真实CAD渲染"><p class="note">这三款已建立完整实体与内部布局，并通过静态几何检查。电路、固件、实际器件配合和打印工艺还需验证。</p><div class="grid">'
for m in models:
 f=m['folder'];html+=f'<article class="card"><img src="{f}/06_外观预览.png" alt="{m["name"]}"><h2>{m["name"]}</h2><p>{m["subtitle"]}</p><p>{"、".join(m["features"])}</p><a href="{f}/01_完整装配_导入Shapr3D.step">可编辑完整STEP ↓</a><p><a href="{f}/08_内部器件布局.png">内部布局</a> · <a href="{f}/09_拆解预览.png">拆解图</a> · <a href="{f}/00_先读我.txt">尺寸与边界</a></p></article>'
html+='</div></main></html>';(R/'打开预览.html').write_text(html)
manifest=[]
for p in sorted(R.rglob('*')):
 if p.is_file() and p.name!='SHA256清单.json' and '__pycache__' not in p.parts:manifest.append(dict(file=str(p.relative_to(R)),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(R/'SHA256清单.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
print(json.dumps(dict(models=3,parts=sum(m['parts'] for m in models),solids=sum(m['solids'] for m in models),files=len(manifest)+1,MiB=round(sum(p['bytes'] for p in manifest)/1048576,2)),ensure_ascii=False))
