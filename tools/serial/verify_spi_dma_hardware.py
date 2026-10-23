"""Independent raw CRC/sequence/DMA/DWT short-window verification."""
import argparse,binascii,json,math,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args()
tx=(a.folder/'tx.bin').read_bytes();off=0
while off<len(tx):
 assert tx[off:off+2]==b'\xaa\x55' and off+7<=len(tx)
 cmd,n=tx[off+2:off+4];end=off+n+7;assert end<=len(tx)
 assert binascii.crc_hqx(tx[off+2:end-2],65535)==int.from_bytes(tx[end-2:end],'little')
 assert cmd in [0x23,0x50,0x74,0x76,0x78,0x82]
 assert n==0 or (cmd==0x78 and n==1 and tx[off+5]<16)
 off=end
raw=(a.folder/'rx.private.bin').read_bytes();rows=[];pos=0
while pos+7<=len(raw):
 at=raw.find(b'\xaa\x55',pos)
 if at<0:break
 n=raw[at+3]+7
 if n>71:pos=at+1;continue
 if at+n>len(raw):break
 if binascii.crc_hqx(raw[at+2:at+n-2],65535)!=int.from_bytes(raw[at+n-2:at+n],'little'):pos=at+1;continue
 rows.append((at,n,raw[at+2],raw[at+4],raw[at+5:at+n-2]));pos=at+n
assert rows
assert all(x[0]+x[1]==y[0] for x,y in zip(rows,rows[1:])),'corruption/unparsed bytes between frames'
def words(msg,size):return [struct.unpack('<'+'I'*size,x[4]) for x in rows if x[2]==msg]
ds=words(0x83,16);assert len(ds)>=3
for d in ds:
 assert d[0:2]==(1,1) and d[2] in [0,1]
 assert ((d[4]+d[7]+d[14])&0xffffffff)==d[3]
 assert ((d[5]+d[8]+d[9]+d[13]+d[2])&0xffffffff)==d[4]
 assert ((d[6]+d[10])&0xffffffff)==d[5]
 assert all(d[i]==0 for i in [7,8,9,10,13,14])
 assert ((d[6]-d[15])&0xffffffff)<=64
rt=words(0x75,15);assert len(rt)>=3
for v in rt:assert v[4:6]==rt[0][4:6] and v[6]==v[7] and v[10]==v[11]
timing=words(0x77,15)
assert timing and all(t[3]==timing[0][3] for t in timing),'missing timing proof or deadline regression'
perf=[v for v in words(0x79,16) if v[1]==0];assert len(perf)==2
s,e=perf;assert s[0]==e[0]==2 and s[2]==e[2]==150000000 and s[11]==e[11]==s[13]==e[13]==0
u64=lambda v,i:v[i]+(v[i+1]<<32)
wall=u64(e,3)-u64(s,3);idle=u64(e,14)-u64(s,14)
assert 0<wall<245*150000000 and 0<=idle<=wall
cpu=100*(1-idle/wall);assert cpu<85
cuts=[x[0] for x in rows if x[2]==0x83]
streams=[x for x in rows if x[2]==6 and cuts[0]<=x[0]<cuts[-1]]
assert len(streams)>1000
gaps=[{'from_offset':x[0],'to_offset':y[0],'from_seq':x[3],'to_seq':y[3],
       'missing_mod256':(y[3]-x[3]-1)%256} for x,y in zip(streams,streams[1:]) if (y[3]-x[3])%256!=1]
for x in streams:assert len(x[4])==40 and all(math.isfinite(v) for v in struct.unpack_from('<9f',x[4],4))
out={'verdict':'FAIL_USB_SEQUENCE' if gaps else 'PASS_LIMITED_RAW_WINDOW','DWT_s':wall/s[2],'CPU_percent':cpu,'telemetry_frames':len(streams),
 'CRC_or_internal_byte_gaps':0,'observable_seq8_gaps':len(gaps),'missing_mod256_sum':sum(g['missing_mod256'] for g in gaps),'gap_evidence':gaps,'DMA_start_delta':(ds[-1][4]-ds[0][4])&0xffffffff,
 'DMA_success_delta':(ds[-1][5]-ds[0][5])&0xffffffff,'published_delta':(ds[-1][6]-ds[0][6])&0xffffffff,
 'DMA_busy_error_timeout_overflow':0,'ring_high_since_boot':ds[-1][11],'max_transfer_us_since_boot':ds[-1][12]/150,
 'new_pose_late_events':0,'pose_late_since_boot':timing[-1][3],'pose_max_age_us_since_boot':rt[-1][12]/150,
 'scope':'Single-interface raw short window; seq8 only, no endpoint enqueue ledger/physical SOURCE/strict simultaneous cuts'}
(a.folder/'independent-verification.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
