"""Create the Chinese end-user manual; screenshots are mock UI illustrations."""
from pathlib import Path
from xml.sax.saxutils import escape
from reportlab.pdfgen import canvas
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, Image, Flowable
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from PIL import Image as PILImage

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/pdf/AT32_AHRS_使用说明书.pdf'
OUT.parent.mkdir(parents=True,exist_ok=True)
pdfmetrics.registerFont(TTFont('YaHei','C:/Windows/Fonts/msyh.ttc',subfontIndex=0))
pdfmetrics.registerFont(TTFont('YaHeiBold','C:/Windows/Fonts/msyhbd.ttc',subfontIndex=0))
pdfmetrics.registerFontFamily('YaHei',normal='YaHei',bold='YaHeiBold',italic='YaHei',boldItalic='YaHeiBold')
INK=colors.HexColor('#172E43'); TEAL=colors.HexColor('#007F86'); LIGHT=colors.HexColor('#EFF6F8'); GRAY=colors.HexColor('#536578')
W,H=A4; CW=W-88
ST={
 'body':ParagraphStyle('body',fontName='YaHei',fontSize=10.2,leading=16,textColor=INK,spaceAfter=7,wordWrap='CJK'),
 'small':ParagraphStyle('small',fontName='YaHei',fontSize=8.3,leading=12,textColor=GRAY,spaceAfter=4,wordWrap='CJK'),
 'h1':ParagraphStyle('h1',fontName='YaHeiBold',fontSize=23,leading=33,textColor=INK,spaceAfter=15,wordWrap='CJK'),
 'h2':ParagraphStyle('h2',fontName='YaHeiBold',fontSize=12,leading=19,textColor=TEAL,spaceBefore=10,spaceAfter=5,wordWrap='CJK'),
 'cell':ParagraphStyle('cell',fontName='YaHei',fontSize=9,leading=14,textColor=INK,wordWrap='CJK'),
 'head':ParagraphStyle('head',fontName='YaHeiBold',fontSize=9,leading=14,textColor=colors.white,wordWrap='CJK'),
 'cover':ParagraphStyle('cover',fontName='YaHeiBold',fontSize=34,leading=51,textColor=INK,spaceAfter=14,wordWrap='CJK'),
}
story=[]
def para(text,style='body'):
 return Paragraph(escape(text).replace('\n','<br/>'),ST[style])
def p(text,style='body'):story.append(para(text,style))
def h(text):p(text,'h2')
def step(n,text):
 story.append(Paragraph(f'<font color="#007F86"><b>{n:02d}</b></font>  '+escape(text),ST['body']))
def table(headers,rows,widths=None,padding=7):
 widths=widths or [CW/len(headers)]*len(headers)
 data=[[para(x,'head') for x in headers]]+[[para(str(x),'cell') for x in row] for row in rows]
 t=Table(data,colWidths=widths,hAlign='LEFT',repeatRows=1)
 t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),TEAL),('VALIGN',(0,0),(-1,-1),'TOP'),
  ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,LIGHT]),('LEFTPADDING',(0,0),(-1,-1),9),
  ('RIGHTPADDING',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),padding),('BOTTOMPADDING',(0,0),(-1,-1),padding),
  ('LINEBELOW',(0,0),(-1,0),.6,TEAL),('LINEBELOW',(0,1),(-1,-1),.3,colors.HexColor('#D7E3EA'))]))
 story.extend([t,Spacer(1,9)])
def note(text):
 t=Table([[para(text,'body')]],colWidths=[CW]);t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),LIGHT),
  ('BOX',(0,0),(-1,-1),.5,colors.HexColor('#C5DDE4')),('LEFTPADDING',(0,0),(-1,-1),12),
  ('RIGHTPADDING',(0,0),(-1,-1),12),('TOPPADDING',(0,0),(-1,-1),9),('BOTTOMPADDING',(0,0),(-1,-1),4)]))
 story.extend([t,Spacer(1,9)])
def screenshot(name,maxh=190):
 path=ROOT/f'tmp/pdfs/manual-{name}.png'
 with PILImage.open(path) as im:iw,ih=im.size
 scale=min(CW/iw,maxh/ih)
 img=Image(str(path),width=iw*scale,height=ih*scale);img.hAlign='CENTER'
 story.extend([img,Spacer(1,4),para('界面示意截图；读数来自模拟设备，不代表实际测量结果。','small'),Spacer(1,5)])
def page(number,title):
 story.append(PageBreak());p(f'{number:02d}  {title}','h1')

class BoardDiagram(Flowable):
 def __init__(self):Flowable.__init__(self);self.width=CW;self.height=115
 def draw(self):
  c=self.canv;c.setFillColor(LIGHT);c.roundRect(0,0,CW,110,12,fill=1,stroke=0)
  items=[(18,78,'IMU','LSM6DSV'),(18,22,'磁力计','IST8310'),(184,48,'MCU','AT32F423'),(355,48,'上位机','浏览器 / CAN')]
  for x,y,a,b in items:
   c.setFillColor(colors.white);c.setStrokeColor(colors.HexColor('#BCD8E0'));c.roundRect(x,y-10,132,39,5,fill=1,stroke=1)
   c.setFillColor(TEAL);c.setFont('YaHeiBold',9);c.drawString(x+10,y+13,a)
   c.setFillColor(INK);c.setFont('YaHei',8);c.drawString(x+10,y,b)
  c.setStrokeColor(TEAL);c.setLineWidth(1.2)
  c.lines([(150,86,168,86),(168,86,168,61),(150,30,168,30),(168,30,168,61),(168,61,184,61),(316,61,355,61)])

# 1. Cover and navigation
p('AT32 AHRS\n使用说明书','cover')
p('LSM6DSV / IST8310 · 网页上位机 · USB / UART / CAN','h2')
p('版本 1.0  |  2026-10-02  |  当前本地新版界面','small')
story.extend([Spacer(1,15),BoardDiagram(),Spacer(1,18)])
note('适用范围：本工程 AT32F423 板卡与配套应用固件。三档滤波及诊断需要本轮滤波增强固件；若按钮不可用，先检查固件能力。温度补偿本轮暂不实施。')
h('首次使用')
p('接好设备并保持静止 → 打开页面、选择串口 → 等待零偏初始化 → 进入设置模式 → 保存配置并核对生效状态。详细步骤见第 2 页。')
h('阅读导航')
table(['内容','页码'],[['连接与快速上手','2'],['启动设置、融合模式与量程','3'],['UART / USB 输出与频率','4'],['滤波模式与诊断记录','5'],['零偏与六面加速度计校准','6'],['CAN 设置','7'],['网页固件升级','8'],['升级恢复与常见问题','9'],['文件位置、数据约定与使用边界','10']],[CW-65,65],padding=4)

# 2. Connect
page(1,'连接与快速上手')
table(['访问方式','使用条件'],[['本地 http://127.0.0.1:8798/','本地服务已启动；使用支持 Web Serial 的 Chrome / Edge。'],['线上 https://gyro.233688.xyz/','需要网络；页面功能以实际发布版本为准。']],[165,CW-165])
step(1,'UART：板卡供电正常，转接器 TX 接板卡 RX，RX 接板卡 TX，并共地；按板卡电平要求连接。USB：使用可传输数据的 USB 线直连板卡。')
step(2,'页面波特率按固件配置选择。本工程 UART 测试使用 2000000；USB 连接可先保持页面默认。点击“连接 USB / UART”，在浏览器弹窗中选设备。')
step(3,'此前测试中 COM6 是 WCH UART 转接器，COM16 是 MCU USB。端口号可能变化，应按设备身份选择，不固定依赖编号。')
step(4,'保持静止直到零偏初始化结束。点击“刷新状态”，确认“串口已连接”以及持续更新的姿态。')
screenshot('live',195)
h('认识显示数据')
p('Yaw 为航向，Pitch 为俯仰，Roll 为横滚，单位为度；角速度单位为 °/s。3D 板卡随接收姿态运动，“复位视角”只调整查看角度。')
p('只输出姿态时，加速度、角速度或温度可能没有新数据。要查看这些数值，需要在当前接口勾选对应通道或选择适合的数据流。数据速率是页面接收帧率，不等同于 2000 Hz 内部融合率。')

# 3. Startup
page(2,'启动设置、融合模式与量程')
step(1,'点击“进入设置模式”，读取当前启动配置。选择融合模式、快速启动选项、初始化零偏时长和陀螺仪量程。')
step(2,'点击“保存启动设置”。勾选“应用后立即重启”时，设备保存后重启，上位机会尝试重连之前授权的串口。')
table(['设置','含义与生效时间'],[['六轴','陀螺仪 + 加速度计；航向没有磁北约束，长期可能漂移。'],['九轴','加入磁力计修正航向；磁场受干扰时会拒绝磁更新。'],['九轴相对角','Yaw 以启动参考为 0；Pitch / Roll 保留重力倾角。'],['普通启动','取消“快速启动”后填写初始化零偏时长，默认 2 秒；重启时连续静止采样。'],['快速启动','优先复用温度匹配的历史零偏，再由 VQF 在运行中估计；不代表冷启动或热漂已消除。'],['陀螺仪量程','可选 ±125 / 250 / 500 / 1000 / 2000 / 4000 °/s；保存后重启生效。']],[112,CW-112])
screenshot('startup',175)
note('“当前”是设备正在使用的值，“已保存”是下次启动采用的值。保存成功但未重启时，两者可能不同。“撤销修改”只恢复表单，不撤销已经写入设备的设置。')
p('量程应覆盖最大实际角速度并留有余量，过小会饱和。缩小量程不能保证零漂变小。手动“Yaw 置零”仅改变输出航向参考，不替代零偏校准。')

# 4. Outputs
page(3,'UART / USB 输出与频率')
p('UART 与 USB 可以分别选择数据协议和通道。USB / UART 的公共输出频率由“数据流”区域设置；CAN 输出频率独立设置。')
screenshot('outputs',195)
step(1,'在目标接口中选择 JustFloat 或“自定义协议（AA55 + CRC）”，勾选需要的通道。通道顺序固定为 Yaw、Pitch、Roll、Ax、Ay、Az、Gx、Gy、Gz，未选通道跳过。')
step(2,'按需要勾选“保存到设备，断电保留”，点击“应用 UART 输出”或“应用 USB 输出”。不保存时仅本次运行有效。全不选表示关闭该接口遥测，命令回复仍可使用。')
table(['数据形式','使用要点'],[['JustFloat','适合 VOFA+ 等工具；通道数必须匹配。常用 3 通道为 Yaw / Pitch / Roll。'],['自定义 AA55 + CRC','包含通道掩码和校验，适合上位机自动识别字段。'],['历史 6 通道预设','顺序为 Yaw / Pitch / Roll / Gz / Az / Temp；历史预设加速度单位为 g。'],['自选通道输出','姿态为度，加速度为 m/s²，角速度为 °/s。']],[120,CW-120])
h('设置输出频率')
p('输入频率并点击“设置输出频率”，配套固件会立即应用并保存。频率必须是整数、在 1-2000 Hz 内且能整除融合率 2000 Hz；常用 50、100、200、250、500、1000、2000 Hz。')
p('频率与通道数共同决定串口带宽。出现丢帧时先降低输出频率，减少通道，并核对波特率。滤波增强固件的内部输出姿态滤波固定为 1000 Hz；2000 Hz 发送时可能重复最近的滤波姿态。')

# 5. Profile
page(4,'滤波模式与诊断记录')
p('配套滤波增强固件支持三档模式。先进入设置模式，选择模式，点击“应用模式”；勾选“重启后保留”时保存到设备，不勾选则只临时应用。模式切换无需重启。')
table(['模式','适用场景','取舍'],[['响应优先','快速转动、反馈观察','较少静态平滑，转动响应优先。'],['均衡（默认）','日常观察与测量','兼顾静态平滑、修正速度和动态响应。'],['静态稳定','静止展示、低频读数','更强静态平滑，停转后的修正更慢。']],[105,165,CW-270])
screenshot('filter',205)
note('更平滑的姿态显示不等同于传感器真实零漂消失。三档均持续修正，不把静止输出锁死。参数是初始工程方案；改善幅度需要相同条件下的实板对比。')
h('记录并导出诊断数据')
step(1,'展开“零偏与噪声诊断”。“读取诊断”显示三轴估计零偏、去零偏残余角速度、静止状态和零偏估计不确定度。')
step(2,'将板卡放稳，点击“开始记录”，完成后点击“停止记录”并“导出 CSV”。记录约 20 Hz，最多 50000 点；重新开始记录会清空此前记录，应先导出。')
step(3,'比较模式时使用相同摆放、量程和采样时长。建议热稳定后静态记录至少 10 分钟，长期漂移观察至少 30 分钟。')
p('CSV 中设备诊断时间与显示姿态接收时间分别记录；稀疏通道或无姿态流时，显示姿态可能缺失或过期。估计不确定度不是实测噪声指标。')

# 6. Calibration
page(5,'零偏与六面加速度计校准')
h('启动陀螺仪零偏校准')
p('取消快速启动，填写初始化零偏时长，默认 2 秒。保存并重启后把板卡放在稳固支撑面上，保持静止。运动、明显振动或采样异常会重新开始连续采样窗口，输出可能延后。')
p('不要手持板卡完成零偏采样，也不要在缓慢旋转时校准。延长时长有助于平均部分随机噪声，但不能补偿温度变化。界面的“陀螺重标定”和“运行时陀螺标定 60s”目前为禁用功能。')
h('自动六面加速度计校准')
step(1,'连接设备，进入设置模式，点击“开始六面校准”。')
step(2,'让传感器 X、Y、Z 三个轴分别朝上和朝下，共六面，顺序不限。参考上位机原始三轴加速度，目标轴接近 ±1 g，其余两轴接近 0 g。')
table(['六面','目标原始加速度（g）'],[['-X / +X','X 接近 -1 / +1，Y、Z 接近 0'],['-Y / +Y','Y 接近 -1 / +1，X、Z 接近 0'],['-Z / +Z','Z 接近 -1 / +1，X、Y 接近 0']],[115,CW-115])
step(3,'每面摆正并稳定约 1.5 秒，等待该面高亮完成后再翻到下一面。翻动期间暂停采样，重复摆同一面不会增加完成面数。')
step(4,'六面齐全后等待拟合与保存完成，以设备反馈“完成”及有效参数为准。不要看到六个面已亮就立即断电。')
step(5,'需要中止时点击“取消校准”。取消或失败会保留旧校准参数；根据页面错误原因重新摆正或重试。')
note('“原始加速度”区域用于判断摆放方向。板卡模型的查看角度不代表传感器轴方向；请按设备识别的面与原始读数操作。六面校准改善加速度偏置和比例误差，不是磁力计校准。')
h('九轴航向的环境条件')
p('尽量远离磁铁、电机、大电流导线和铁磁物体。磁干扰提示出现时，先检查环境。当前界面没有完整的磁力计校准向导；临时校准参数仍需正式实测验证。')

# 7. CAN
page(6,'CAN 设置')
step(1,'接好 CAN 收发器与总线，在正确位置配置终端电阻。点击“进入设置模式”，再读取 CAN 配置。')
screenshot('can',200)
table(['参数','含义'],[['CAN_ID','11 位标准 ID，0-0x7FF；用于请求及主动输出。'],['MST_ID','应答 ID，0-0x7FF；支持十进制或 0x 十六进制输入。'],['波特率','与总线其他节点一致；400 Kbps 选项实际约 401.07 Kbps。'],['发送模式','主动周期输出，或请求 / 应答。'],['间隔 / 频率','每类报文的周期与频率，不是所有报文合计频率。'],['输出组','加速度 + 温度、角速度、欧拉角、四元数，可按需求组合。']],[115,CW-115])
step(2,'设置输出频率，例如 50 Hz 对应 20 ms。周期按整数毫秒表示，输入 60 Hz 会近似为 17 ms，即约 58.824 Hz；以页面换算提示为准。')
step(3,'按需要勾选保存，点击“应用 CAN 配置”，读取“当前”和“已保存”值确认。CAN 配置可独立于 USB / UART 输出设置。')
note('开启多个输出组会增加总线负载，不能把每组频率理解为总帧率。参数无效或负载过高时，先降低每类频率、减少组数，再重试。')
p('CAN 使用本工程的达妙格式与扩展约定；接入其他设备时应核对本工程协议，不能直接假设所有同名设备的寄存器与单位完全相同。')

# 8. Firmware
page(7,'网页固件升级')
note('前提：板上已安装配套 Bootloader，文件是本板应用固件 .bin。首次安装 Bootloader 仍需 SWD。升级期间保持供电，当前 Bootloader 不支持断电自动回滚。')
screenshot('firmware',175)
step(1,'连接设备并确认持续收到数据。停止诊断记录和校准，先保存或撤销未应用的表单修改。展开“USB / UART 固件升级”。')
step(2,'选择本板应用 .bin。页面检查文件范围、向量和大小，显示 CRC32 与 SHA256；仍需确认文件适用于当前板卡。最大应用文件为 208 KiB。')
step(3,'点击“开始升级”。页面进入 Bootloader，核对应用分区，再擦除应用、分块上传并校验完整镜像。普通配置及串口读写由升级流程独占。')
step(4,'UART 使用同一个转接器串口继续升级。USB 在复位后重新枚举，上位机释放旧端口并等待已授权的同一块板卡重新出现。')
step(5,'等待完整校验、启动应用和重新连接。应看到“升级完成”且新应用已响应；只看到上传达到 100% 不能单独证明应用正常运行。')
step(6,'应用重启后保持静止，等待零偏初始化，再刷新设备配置。配置和校准保留在专用区域；新版设置迁移应保持原有选择。')
h('固件文件的选择')
p('可选本工程构建目录中的应用 .bin。不要选择 Bootloader、整片 Flash 备份、.hex 或压缩包。当前文件仅在本地浏览器读取，不会作为上传文件发到服务器。')

# 9. Troubleshoot
page(8,'升级恢复与常见问题')
h('升级失败或设备已经在 Bootloader')
step(1,'保持供电，不把残缺上传当作可启动固件。关闭占用串口的其他页面或串口工具。')
step(2,'勾选“设备已在升级模式（恢复上传）”，点击“选择恢复端口”，选当前设备的 Bootloader 串口；USB 重新枚举后端口号可能变化。')
step(3,'重新选择正确应用 .bin，执行完整上传。只有完整校验和应用响应都确认后才算升级完成。“停止上传”不会启动残缺应用。')
table(['现象','处理顺序'],[['选不到串口','确认数据线、驱动和供电；使用支持 Web Serial 的浏览器，并检查端口授权。'],['端口被占用','断开其他上位机、VOFA+、串口调试工具或其他浏览器页面，再连接。'],['已连接但无数据','保持静止等待启动校准；核对波特率、RX / TX、通道是否全不选。'],['姿态数值异常 / 失步','点击刷新状态；核对解析方式、JustFloat 通道数和实际输出格式。'],['配置保存后没有变化','核对“当前”与“已保存”；启动模式、快速启动、时长、量程需要重启。'],['滤波按钮不可用','进入设置模式；若提示不支持，升级滤波增强固件。'],['Yaw 没从 0 开始','核对是否为九轴相对角；普通九轴不要求启动为 0。Pitch / Roll 本来就保留倾角。'],['Yaw 漂移或抖动','检查启动静止、振动、磁场环境和量程；分开比较原始姿态与滤波姿态。'],['USB 升级重连超时','只保留目标板卡，等待重新枚举；必要时选择新出现的 USB 恢复端口。'],['六面迟迟不通过','参考原始 g 值摆正，稳定支撑，确认不是重复面；查看页面提示原因。']],[127,CW-127])

# 10. Appendix
page(9,'文件位置、数据约定与使用边界')
h('当前工程文件')
p('工程根目录：E:/Desktop/1_Program/AT32F423_LSM6DSV_SPI_Test')
table(['内容','相对于工程根目录的位置'],[['上位机源码','upper/gyro-live/src/public/'],['构建后可发布静态文件','upper/gyro-live/dist/public/'],['滤波增强应用固件','build/lsm6dsv_noise/release-9axis/lsm6dsv_noise.bin'],['本说明书','output/pdf/AT32_AHRS_使用说明书.pdf']],[125,CW-125])
p('本地新版文件与线上已发布版本可能不同；将文件复制到发布目录或生成构建产物，不等同于已经发布到 Cloudflare。说明书所示功能以配套固件和实际页面能力为准。')
h('VOFA+ 快捷协议切换')
p('从 USB 或 UART 发送字符串 vofa 即可把两个端口的输出协议切换为 JustFloat。ASCII 字节为 76 6F 66 61，不需要空格或回车；原有选择的通道沿用。此快捷操作不写入持久配置，需要断电保留时在上位机保存输出。')
h('诊断数据的解读')
p('零偏单位为 °/s；残余角速度是实际送入 VQF 的低通角速度减去当前零偏。原始姿态用于对照最终输出。CSV 的设备毫秒时间与主机接收时间分别标注；不要把不同时间基准直接相减。')
p('用去趋势标准差描述短期抖动，用展开角度后的变化斜率描述漂移。Yaw 在 ±180° 附近会跨界，应先展开角度再计算趋势。姿态输出平滑、零偏估计、真实传感器噪声是不同指标。')
h('当前使用边界')
table(['项目','状态 / 说明'],[['温度补偿','按本轮要求暂不实施；温度读数仍可显示，不能据此认为已有有效热漂补偿。'],['运行时陀螺仪重标定','两个相关按钮目前禁用；使用普通启动静止采样。'],['三档滤波参数','初始工程预设；软件回归通过不等于长期实板性能认证。'],['六面 / 磁场校准','算法与界面支持六面流程；最终效果需要实际摆放和独立数据验证。'],['固件升级','保留配置区；支持恢复完整上传，当前无断电自动回滚。']],[125,CW-125])
p('本说明书依据当前本地上位机界面、应用协议及项目实现编写。界面截图为模拟设备示意，不含实板性能结论。','small')

def decorate(c,doc):
 c.saveState()
 c.setStrokeColor(colors.HexColor('#D2E1E8'));c.setLineWidth(.6);c.line(44,H-31,W-44,H-31)
 c.setFont('YaHei',8);c.setFillColor(GRAY);c.drawString(44,H-23,'AT32 AHRS  /  网页上位机使用说明')
 c.line(44,31,W-44,31);c.drawString(44,19,'V1.0  |  2026-10-02');c.drawRightString(W-44,19,f'{doc.page} / 10')
 c.restoreState()
doc=SimpleDocTemplate(str(OUT),pagesize=A4,rightMargin=44,leftMargin=44,topMargin=48,bottomMargin=44,
 title='AT32 AHRS 使用说明书',author='AT32 AHRS 项目',subject='USB UART CAN 上位机操作、校准、诊断和固件升级')
doc.build(story,onFirstPage=decorate,onLaterPages=decorate)
print(OUT)
