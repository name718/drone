"""Curate the user's existing CAD renders, without including credentials or personal photos."""
from pathlib import Path
from PIL import Image,ImageOps
import json,shutil

R=Path(__file__).resolve().parent
SOURCE=Path('/Users/mjt/Library/CloudStorage/OneDrive-Office365/sharp3d/AI原创建模')
OUT=R/'seed_assets';OUT.mkdir(exist_ok=True)
records=[]
def features(names):return [dict(name=n,state=s) for n,s in names]
def add(id,title,subtitle,category,status,version,day,description,fs,sensors,steps,images,events,archived=False):
 photos=[]
 for num,(path,caption,kind,d) in enumerate(images):
  src=SOURCE/path
  if not src.exists():raise FileNotFoundError(src)
  fn=f'{id}-{num:02d}'+('.gif' if kind=='animation' else '.jpg')
  if kind=='animation':shutil.copyfile(src,OUT/fn)
  else:
   with Image.open(src) as im:
    im=ImageOps.exif_transpose(im).convert('RGB');im.thumbnail((1800,1500));im.save(OUT/fn,quality=88,optimize=True)
  photos.append(dict(id=f'{id}-p{num}',filename=fn,kind=kind,caption=caption,recorded_at=d,created_at=d))
 records.append(dict(id=id,title=title,subtitle=subtitle,category=category,status=status,version=version,created_at=day,updated_at=events[-1]['date'] if events else day,description=description,features=features(fs),sensors=sensors,next_steps=steps,archived=archived,photos=photos,events=events,cover_id=photos[0]['id'] if photos else None,source_note='整理自现有 CAD 文件与对话，历史记录按文件日期归档。图片为设计渲染或动作示意；电路、固件、实物装配与测试进度需要另行记录。'))
def ev(day,title,body):return dict(date=day,title=title,body=body)
d24='2026-10-24';d25='2026-10-25'
v9='009_圆耳机器人_V2_小脚点头';v8='008_圆耳机器人_双麦摄像头藏轮'
add('round-ear','圆耳机器人','圆头、双麦耳朵和小脚藏轮。能显示表情、移动、说话与播放，新增独立点头关节。','桌面机器人','推进中','V2 · 小脚点头',d25,
 '我们正在推进的圆耳桌面伙伴。两耳各放一只麦克风，脸部横放现有的 1.8 寸 TFT，额头预留摄像头。两条腿固定，左右脚内藏轮，使用双 N20 霍尔电机差速移动。\n\nV2 选择更换 32 mm 窄轮，让每只鞋从 64×96×50 mm 缩到 52×68×38 mm。脖子增加独立俯仰机构，按上下各 20° 布置；下层连续偏航机构保留。\n\n当前完成外观与机构布局，99 个 STEP 实体、28 组姿态通过所建包络的几何检查。SG90 承载、线束、稳定性与实物尺寸仍需验证，PCB 尚待设计。',
 [('表情显示','已建模'),('藏轮移动','已建模'),('说话与音频播放','已建模'),('双麦录音','已建模'),('摄像头观察','已建模'),('连续转头','已建模'),('上下点头 ±20°','已建模'),('桌沿防跌落','待验证')],
 ['双麦克风','摄像头','前向 ToF 测距','四处向下桌沿探头','IMU','电机霍尔编码','颈部回零霍尔'],
 '① 测量 N20 编码器板、插座与 TFT 载板。\n② 称量头部，验证点头舵机与缩脚后的稳定性。\n③ 确定小轮采购，再设计控制与电源板。',
 [(v9+'/08_小脚点头版_外观.png','V2 小脚点头版外观','render',d25),(v9+'/18_同尺度新旧对比.png','V1 与 V2 同尺度对比：缩小脚部','render',d25),(v9+'/19_低头20度与抬头20度.png','独立俯仰关节两端姿态','render',d25),(v9+'/11_内部器件.png','V2 内部器件空间布局','render',d25),(v9+'/13_小脚动力层.png','32 mm 小轮与霍尔 N20 动力层','render',d25),(v9+'/14_两轴颈部机构.png','偏航加俯仰，两轴颈部布局','render',d25),(v8+'/08_外观预览.png','V1 大脚版，保留作为迭代记录','render',d25),(v9+'/17_点头加转头_动作示意.gif','点头与转头动作示意，非实机运行','animation',d25)],
 [ev(d25,'V1：圆耳与双脚藏轮','用户提出：圆头、戴耳朵，放两个麦克风，带摄像头；横放现有 1.8 寸 TFT，脖子可 360° 转动，两条腿固定，轮子尽量不可见。'),ev(d25,'反馈：脚太大，希望能点头','用户反馈：“脚也太大了吧，能点头吗？”原版只有偏航关节，需要新增俯仰机构。'),ev(d25,'方向确认：允许更换小轮，优先好看','用户确认：“可以换小轮子，优先好看”。据此调整脚部与驱动轮候选尺寸。'),ev(d25,'V2：小脚与独立点头关节','每只鞋调整为 52×68×38 mm，采用 32 mm 轮候选；新增上下各 20° 的点头关节。完成包络几何检查，实物稳定性、承载与配合待验证。')])
v3='003_桌面陪伴机器人_V1';v5='005_桌面陪伴机器人_V2.1_霍尔编码电机修订'
add('box-companion','方头陪伴机器人','从最初喜欢的方头造型开始，逐步补齐内部器件、音频与霍尔电机尾板空间。','桌面机器人','待验证','V2.1',d24,
 '方形圆角脸、可拆头壳和轮式底盘。最初外观得到“这个模型我喜欢”的反馈，随后开始补充完整内部布局、说话和录音的器件空间。\n\nV2.1 根据现有带霍尔编码的 N20 尾板与白色插座修订动力层。电路板与模块空间不能代表已完成电气设计。',
 [('表情显示','已建模'),('双轮移动','已建模'),('录音与说话','已建模'),('音频播放','已建模'),('霍尔电机尾板让位','已建模')],['麦克风','前向测距','桌沿检测','电机霍尔编码'],
 '核对实际屏幕与编码电机尺寸，先试装局部固定件，再决定是否推进该外观。',
 [(v5+'/03_预览/04_完整装配.png','V2.1 完整装配','render',d24),(v5+'/03_预览/05_内部布局.png','内部器件与主板空间','render',d24),(v5+'/03_预览/01_霍尔电机尾板与插座.png','霍尔尾板与白色插座让位','render',d24),(v3+'/04_预览/01_正面装配.png','最初 V1 外观','render',d24)],
 [ev(d24,'确定方头陪伴机器人方向','用户反馈喜欢这款外观，继续讨论屏幕采购、轮式支撑、传感器和内部器件。'),ev(d24,'补齐内部布局与音频路线','根据现有 ESP32-S3、N20、电池等记录评估适配，不强行装入不合适的已有大主板。'),ev(d24,'V2.1：修订编码电机尾部','用户确认使用带霍尔编码的 N20，补入尾部电路板、白色插座和接线空间。')])
series='007_桌面机器人系列_表情移动语音'
robots=[('tuanzi','团仔','A_团仔_一体圆团','封闭一体外壳，圆润大脸和琥珀色表情，橙色轮毂。','一体圆团'),('diandian','点点','B_点点_分体大头','分体大头、青色笑眼与紫色轮毂，预留一轴点头机构。','分体大头'),('cruiser','巡游','C_巡游_低趴侦察车','固定宽脸、深蓝灰外壳与橙色护条，像一辆低趴的小车伙伴。','低趴侦察车')]
for id,title,folder,desc,label in robots:
 add(id,title,desc,'桌面机器人','待评审','概念 V1',d24,desc+'\n\n按表情、移动、说话、音频播放四项基础能力设计。差速双轮加后球轮支撑。屏幕选型、紧固、线束与电路尚未冻结。',
  [('表情显示','已建模'),('双轮移动','已建模'),('说话','已建模'),('音频播放','已建模')]+([('点头关节','想法')] if id=='diandian' else []),['麦克风','前向测距','四处桌沿探头','电机霍尔编码'],
  '先评审外观；若继续推进，锁定屏幕与真实模块，再验证结构及电路。',
  [(series+'/'+folder+'/06_外观预览.png',label+'外观方案','render',d24),(series+'/'+folder+'/07_内部器件预览.png','内部器件布局','render',d24)],
  [ev(d24,'机器人系列重新定义','用户明确希望桌面机器人最少具有表情、移动、说话与播放功能，据此制作三款方案。')])
light='006_轻量桌面产品_四款概念'
lights=[('doudou','豆豆','A_豆豆_表情番茄钟','表情番茄钟','25/5 分钟计时与表情反馈。',[('番茄钟计时','已建模'),('表情反馈','已建模')]),('yaya','芽芽','B_芽芽_触摸小夜灯','触摸小夜灯','叶片触摸帽、暖光与亮度切换。',[('触摸开关','已建模'),('暖光亮度切换','已建模')]),('baobao','抱抱','C_抱抱_手机座','手机座','纯机械手机座，固定后倾，预留充电走线和姓名牌。',[('手机支撑','已建模'),('充电线开口','已建模'),('可换姓名牌','已建模')]),('kedu','刻度','D_刻度_旋钮计时器','旋钮计时器','旋钮设定时间、按压开始暂停与结束提示。',[('旋钮计时','已建模'),('按压开始暂停','已建模'),('结束提示','想法')])]
for id,title,folder,label,desc,fs in lights:
 add(id,title+' · '+label,desc,'桌面小产品','暂存','概念 V1',d24,desc+'\n\n属于较早的轻量桌面产品探索。用户对这一批方案整体不满意，之后明确改为有表情、能移动、能说话和播放的桌面机器人系列。保留此档案供回看，不代表当前推进或已经验证好卖。',fs,[] if id=='baobao' else (['触摸输入'] if id=='yaya' else ['旋钮或按键输入']),
  '暂存，不作为当前主线。若重启，需要先重新评审外观与使用场景。',[(light+'/'+folder+'/04_外观预览.png',label+'概念渲染','render',d24)],
  [ev(d24,'轻量桌面产品探索','按功能少、零件数量少的方向制作四款概念。销量、成本与真实需求尚未验证。'),ev(d24,'用户反馈：整体不喜欢，转向机器人系列','用户明确要求至少有表情、移动、说话与播放；这批小产品转为暂存参考。')],True)
base='002_双N20桌面机器人底座'
add('n20-base','双 N20 机器人底座','用两只 N20 电机探索桌面移动底座，记录动力层与结构的起点。','机械底座','暂存','原型 V1',d24,'作为后续桌面机器人的轮式底座探索，保留动力层、底板与装配图。是机构布局原型，实物电机、轮子与固定方式需要复核。',[('双电机差速移动','已建模'),('模块分层安装','已建模')],[], '作为底盘设计参考，优先推进圆耳机器人完整方案。',[(base+'/04_预览与尺寸/01_完整装配.png','双 N20 底座完整装配','render',d24),(base+'/04_预览与尺寸/02_下层机械布局.png','底座下层机械布局','render',d24)],[ev(d24,'从精度演示转向可移动底座','用户提出使用两个 N20 电机创作底座，作为桌面移动结构的初步探索。')],True)
existing=json.loads((R/'seed.json').read_text()) if (R/'seed.json').exists() else []
generated={i['id'] for i in records}
records.extend(i for i in existing if i['id'] not in generated)
(R/'seed.json').write_text(json.dumps(records,ensure_ascii=False,indent=2))
print(json.dumps({'ideas':len(records),'media':sum(len(i['photos']) for i in records),'MiB':round(sum(p.stat().st_size for p in OUT.iterdir())/1048576,2)},ensure_ascii=False))
