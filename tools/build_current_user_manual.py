"""Build the current Chinese PDF manual from the public usage guide.
Uses embedded Microsoft YaHei fonts. No board access or private snapshots.
"""
from pathlib import Path
from xml.sax.saxutils import escape
from urllib.parse import quote
import hashlib,json,re
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.pagesizes import A4
from reportlab.platypus import SimpleDocTemplate,Paragraph,Spacer,Table,TableStyle,PageBreak,KeepTogether,Flowable

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'docs/使用指南.md'
OUT=ROOT/'output/pdf/AT32_AHRS_使用说明书_20261024c.pdf'
OUT.parent.mkdir(parents=True,exist_ok=True)
pdfmetrics.registerFont(TTFont('CJK','C:/Windows/Fonts/msyh.ttc',subfontIndex=0))
pdfmetrics.registerFont(TTFont('CJKB','C:/Windows/Fonts/msyhbd.ttc',subfontIndex=0))
pdfmetrics.registerFontFamily('CJK',normal='CJK',bold='CJKB',italic='CJK',boldItalic='CJKB')
INK=colors.HexColor('#163046');TEAL=colors.HexColor('#087F8C');MUTED=colors.HexColor('#556B7B');PALE=colors.HexColor('#EDF5F7')
W,H=A4;CW=W-92
styles={
 'body':ParagraphStyle('body',fontName='CJK',fontSize=10,leading=16.4,wordWrap='CJK',textColor=INK,spaceAfter=9),
 'h1':ParagraphStyle('h1',fontName='CJKB',fontSize=20,leading=29,wordWrap='CJK',textColor=INK,spaceAfter=18,keepWithNext=True),
 'small':ParagraphStyle('small',fontName='CJK',fontSize=8.3,leading=12.6,wordWrap='CJK',textColor=MUTED,spaceAfter=7),
 'cell':ParagraphStyle('cell',fontName='CJK',fontSize=9,leading=14,wordWrap='CJK',textColor=INK),
 'head':ParagraphStyle('head',fontName='CJKB',fontSize=9,leading=14,wordWrap='CJK',textColor=colors.white),
 'cover':ParagraphStyle('cover',fontName='CJKB',fontSize=32,leading=46,textColor=INK,spaceAfter=12),
 'sub':ParagraphStyle('sub',fontName='CJKB',fontSize=12,leading=19,textColor=TEAL,spaceAfter=14),
}
def inline(s):
 s=s.replace('—','-').replace('–','-').replace('\u2011','-')
 # Escape plain text, then restore supported inline Markdown as PDF markup.
 tokens={}
 def link(m):
  label,target=m.groups()
  if '://' not in target:
   target='https://github.com/Mathonix/AT32F423_LSM6DSV/blob/master/'+str((Path('docs')/target)).replace('\\','/')
   target=target.replace('docs/../','')
  key=f'LINKTOKEN{len(tokens)}END'
  tokens[key]=f'<link href="{escape(quote(target,safe=":/#?=&%"))}" color="#087F8C">{escape(label)}</link>'
  return key
 s=re.sub(r'\[([^\]]+)\]\(([^)]+)\)',link,s)
 s=escape(s)
 s=re.sub(r'\*\*(.+?)\*\*',r'<b>\1</b>',s)
 s=re.sub(r'`([^`]+)`',r'<font color="#087F8C">\1</font>',s)
 for key,val in tokens.items():s=s.replace(key,val)
 return s
def para(s,kind='body'):return Paragraph(inline(s),styles[kind])
def table(rows,padding=7):
 n=len(rows[0]);widths=[CW*.33,CW*.67] if n==2 else [CW/n]*n
 cells=[[para(c,'head' if i==0 else 'cell') for c in row] for i,row in enumerate(rows)]
 t=Table(cells,colWidths=widths,repeatRows=1,hAlign='LEFT')
 t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),TEAL),('VALIGN',(0,0),(-1,-1),'TOP'),
 ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,PALE]),('LEFTPADDING',(0,0),(-1,-1),9),('RIGHTPADDING',(0,0),(-1,-1),9),
 ('TOPPADDING',(0,0),(-1,-1),padding),('BOTTOMPADDING',(0,0),(-1,-1),padding),('LINEBELOW',(0,1),(-1,-1),.35,colors.HexColor('#D9E5E9'))]))
 return t
class Divider(Flowable):
 def __init__(self):super().__init__();self.width=CW;self.height=18
 def draw(self):self.canv.setStrokeColor(TEAL);self.canv.setLineWidth(2);self.canv.line(0,9,CW,9)

md=SOURCE.read_text(encoding='utf8');lines=md.splitlines();story=[]
story += [Spacer(1,35),para('AT32 AHRS','cover'),para('使用说明书','cover'),para('固件 20261024c  /  2026-10-24','sub'),Divider(),Spacer(1,13)]
story += [table([['适用对象','说明'],['硬件','AT32F423 / LSM6DSV / IST8310'],['连接与输出','原生USB CDC、WebUSB、UART、CAN'],['上位机','[gyroa.233688.xyz](https://gyroa.233688.xyz/)'],['本版变化','SPI DMA采集、当前ESKF频率说明、验收边界、末尾灯语，以及第8节单板性能指标摘要']]),Spacer(1,16)]
story += [para('阅读导航','sub')]
sections=[(int(m[1]),m[2]) for line in lines if (m:=re.match(r'^## (\d+)\. (.+)$',line))]
for number,title in sections:story.append(para(f'{number:02d}  {title.replace("（指南末尾）", "")}','body'))
story += [Spacer(1,12),para('使用前请先读第1节的版本和验收范围。当前 master 并非双套完整逐帧协方差8kHz版本；短测通过不代表全功能生产认证。','small'),para('内容依据仓库中文使用指南，旧版PDF作为历史资料保留；本说明书不包含私有校准记录或原始抓包。','small')]
i=0;inside=False;seen=[]
while i<len(lines):
 line=lines[i].strip()
 if line.startswith('## '):
  inside=True;number=int(re.match(r'## (\d+)',line)[1]);title=re.sub(r'^## \d+\. ','',line).replace('（指南末尾）','')
  story += [PageBreak(),para(f'{number:02d}  {title}','h1')];seen.append(number);i+=1;continue
 if not inside or not line:i+=1;continue
 if line.startswith('|'):
  rows=[]
  while i<len(lines) and lines[i].strip().startswith('|'):
   row=[c.strip() for c in lines[i].strip().strip('|').split('|')]
   if not all(re.fullmatch(r'[:\- ]+',c) for c in row):rows.append(row)
   i+=1
  assert all(len(row)==len(rows[0]) for row in rows)
  story += [table(rows,padding=5 if seen[-1]==7 else 7),Spacer(1,12)];continue
 if line.startswith('```'):
  code=[];i+=1
  while i<len(lines) and not lines[i].startswith('```'):code.append(lines[i]);i+=1
  for command in code:
   # Existing commands fit the text block; CJK wrapping can break long ASCII.
   story.append(para(command,'small'))
  i+=1;continue
 if re.match(r'^\d+\. ',line) or line.startswith('- '):
  story.append(para(line if not line.startswith('- ') else '• '+line[2:]));i+=1;continue
 body=[line];i+=1
 while i<len(lines) and lines[i].strip() and not lines[i].startswith(('## ','|','```','- ')) and not re.match(r'^\d+\. ',lines[i]):body.append(lines[i].strip());i+=1
 story.append(para(' '.join(body)))
assert seen==[1,2,3,4,5,6,7,8]
def furniture(c,doc):
 c.saveState();c.setFont('CJK',8);c.setFillColor(MUTED)
 if doc.page>1:
  c.drawString(46,H-30,'AT32 AHRS  使用说明书');c.drawRightString(W-46,H-30,'20261024c')
  c.setStrokeColor(colors.HexColor('#D9E5E9'));c.line(46,H-37,W-46,H-37)
 c.drawString(46,25,'2026-10-24  |  当前源码使用范围');c.drawRightString(W-46,25,str(doc.page));c.restoreState()
doc=SimpleDocTemplate(str(OUT),pagesize=A4,leftMargin=46,rightMargin=46,topMargin=56,bottomMargin=45,
 title='AT32 AHRS 使用说明书 - 20261024c',author='Mathonix / AT32 AHRS',pageCompression=1)
doc.build(story,onFirstPage=furniture,onLaterPages=furniture)
manifest={'source':str(SOURCE.relative_to(ROOT)),'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
 'pdf':OUT.name,'pdf_sha256':hashlib.sha256(OUT.read_bytes()).hexdigest(),'firmware':'20261024c'}
(OUT.with_suffix('.manifest.json')).write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
print(str(OUT))
