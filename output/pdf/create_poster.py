from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor
from reportlab.lib.units import mm

OUT=Path(__file__).parent
pdfmetrics.registerFont(TTFont('JH','C:/Windows/Fonts/msjh.ttc',subfontIndex=0))
pdfmetrics.registerFont(TTFont('JHB','C:/Windows/Fonts/msjhbd.ttc',subfontIndex=0))
W,H=1120,1586
c=canvas.Canvas(str(OUT/'ESP32_PQC_A1_draft.pdf'),pagesize=(594*mm,841*mm))
c.setTitle('ESP32-S3 後量子安全影像傳輸系統 | A1 專題展示初稿')
c.scale(594*mm/W,841*mm/H)
navy='#102A43'; teal='#007F86'; cyan='#5CE0D1'; ink='#18364A'; muted='#526B7B'; pale='#EDF5F7'; white='#FFFFFF'
def box(x,y,w,h,color,r=0):
 c.setFillColor(HexColor(color)); c.setStrokeColor(HexColor(color))
 if r:c.roundRect(x,H-y-h,w,h,r,fill=1,stroke=0)
 else:c.rect(x,H-y-h,w,h,fill=1,stroke=0)
def text(x,y,s,size=18,color=ink,bold=False):
 c.setFillColor(HexColor(color));c.setFont('JHB' if bold else 'JH',size);c.drawString(x,H-y-size,s)
def lines(x,y,ss,size=18,color=ink,leading=29,bold=False):
 for i,s in enumerate(ss):text(x,y+i*leading,s,size,color,bold)
def line(x,y,x2,y2,color='#C8DCE2',width=1):
 c.setStrokeColor(HexColor(color));c.setLineWidth(width);c.line(x,H-y,x2,H-y2)
def arrow(x,y,x2,y2,color=teal):
 line(x,y,x2,y2,color,2.5)
 import math
 a=math.atan2(y2-y,x2-x)
 for d in [-.5,.5]:line(x2,y2,x2-10*math.cos(a+d),y2-10*math.sin(a+d),color,2.5)
def section(x,y,n,title):
 box(x,y,34,30,teal,5);text(x+7,y+3,n,17,white,True);text(x+46,y-1,title,24,navy,True)

box(0,0,W,H,'#F8FBFC')
box(0,0,W,300,navy)
box(46,41,7,32,cyan)
text(68,43,'PQC  /  EMBEDDED VISION  /  ESP32-S3',17,cyan,True)
text(46,94,'ESP32-S3 後量子安全',48,white,True)
text(46,156,'影像傳輸系統',48,white,True)
text(49,234,'從雙向身分驗證、金鑰建立到多相機加密串流',23,'#C9E4EC')
for x,top,sub in [(46,'驗證彼此身分','ML-DSA'),(397,'建立共享祕密','ML-KEM-768'),(748,'保護影像內容','AES-256-GCM')]:
 box(x,321,326,82,white,10);text(x+18,331,top,21,navy,True);text(x+18,367,sub,16,teal,True)

section(46,434,'01','研究動機與目標')
lines(46,482,['連網相機除了傳送影像，也需要確認通訊對象、保護影像內容，',
              '並持續管理連線與金鑰。本專題以 ESP32-S3 建置嵌入式原型，',
              '整合後量子密碼、多裝置連線及定期換鑰，驗證實作可行性。'],21,leading=32)

section(46,605,'02','系統架構')
box(46,651,1028,230,pale,12)
for y,label in [(674,'相機 01'),(765,'相機 02')]:
 box(68,y,236,70,white,8)
 box(82,y+18,35,30,navy,5)
 c.setFillColor(HexColor(cyan));c.circle(99.5,H-y-33,9,fill=1,stroke=0)
 text(132,y+9,label,18,navy,True);text(132,y+38,'ESP32-S3 / JPEG',14,muted)
 arrow(310,y+35,434,742)
box(446,698,234,92,navy,10)
text(480,711,'Wi-Fi / TCP',24,white,True);text(477,750,'各台獨立連線與工作階段',15,'#C9E4EC')
arrow(689,744,756,744)
box(770,680,278,130,white,10)
text(794,693,'電腦端 Host',24,navy,True)
lines(794,735,['驗證身分與影像完整性','解密、顯示及連線管理'],17,leading=27)
text(447,831,'mDNS 探索地址 → 已登錄公鑰配對 → 連線後驗證身分',16,muted)

section(46,910,'03','安全通訊流程')
steps=[('01','探索與配對','mDNS ＋ 公鑰指紋'),('02','雙向認證','ML-DSA 簽章'),('03','金鑰建立','ML-KEM-768'),('04','加密串流','AES-256-GCM')]
for i,(n,title,sub) in enumerate(steps):
 x=46+i*264
 box(x,956,236,105,white,8);text(x+16,964,n,15,teal,True);text(x+16,990,title,23,navy,True);text(x+16,1028,sub,16,muted)
 if i<3:arrow(x+240,1009,x+259,1009)
box(46,1078,1028,67,navy,8)
text(64,1088,'持續保護',19,cyan,True)
text(180,1088,'Pipeline 提前準備下一把影像金鑰，於門檻切換。',18,white)
text(180,1117,'v7 記錄層：HMAC-SHA256、連線綁定與序號檢查；控制文字不加密。',16,'#C9E4EC')

section(46,1174,'04','實測成果')
text(640,1178,'v6 基準  /  2026-09-30  /  非 v7 效能',16,muted)
for x,value,label in [(46,'2 台','同步連線測試'),(397,'600 秒','持續串流觀察'),(748,'0 / 0','傳輸中斷 / 握手錯誤（兩台皆同）')]:
 box(x,1220,326,104,pale,8);text(x+18,1229,value,37,teal,True);text(x+18,1290,label,15,muted)
text(46,1341,'接收驗證吞吐',19,navy,True)
for y,label,fps in [(1378,'相機 01',36.31),(1413,'相機 02',40.50)]:
 text(46,y,label,17,muted);box(139,y+4,fps/45*430,21,teal,4);text(585,y-1,f'{fps:.2f} FPS',21,navy,True)
lines(775,1343,['目前 v7 進度','雙台串流、兩種換鑰模式','與斷網恢復短測已通過。','長期、攻擊與撤銷驗收待補。'],16,leading=25)
text(46,1452,'條件：QVGA、每 10 幀換鑰、pipeline、顯示開啟、手機熱點。FPS 非獨立拍攝更新率。',14,muted)
text(46,1475,'觀察：仍有偶發約 2.5 秒停頓；以上結果為功能與串流驗證，尚非完整安全驗收。',14,muted)

line(46,1512,1074,1512)
text(46,1528,'學校／系所：',16,muted);line(150,1552,367,1552)
text(399,1528,'組員：',16,muted);line(457,1552,715,1552)
text(747,1528,'指導教授：',16,muted);line(835,1552,1074,1552)
text(46,1566,'資料：專案 README、CURRENT_STATUS、CONTROL_RECORDS_V7　｜　專題展示初稿 01',10,muted)
c.showPage();c.save()
print(OUT/'ESP32_PQC_A1_draft.pdf')
