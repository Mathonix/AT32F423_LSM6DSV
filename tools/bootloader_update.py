#!/usr/bin/env python3
"""AT32F423 user bootloader updater over UART (2 Mbps) or USB CDC."""
import argparse, struct, time, zlib
import serial

MAGIC=b"BL"; VER=1; MAX_CHUNK=256
CMD_HELLO=1; CMD_BEGIN=2; CMD_DATA=3; CMD_END=4; CMD_ABORT=5

def crc32(b): return zlib.crc32(b) & 0xffffffff
def frame(cmd, addr=0, payload=b"", value_crc=None):
    n=len(payload); c=crc32(payload) if value_crc is None else value_crc
    return MAGIC+bytes((VER,cmd,0,0))+struct.pack("<III",addr,n,c)+payload

def read_reply(ser, timeout=2):
    deadline=time.monotonic()+timeout; buf=bytearray()
    while time.monotonic()<deadline:
        buf += ser.read(max(1, min(64, ser.in_waiting or 1)))
        while len(buf)>=14:
            p=buf.find(MAGIC)
            if p<0: del buf[:-1]; break
            if p: del buf[:p]
            if len(buf)<14: break
            r=bytes(buf[:14]); del buf[:14]
            if r[2]!=VER or crc32(r[:10]) != struct.unpack_from('<I',r,10)[0]: continue
            return r[3]&0x7f, r[4], struct.unpack_from('<I',r,6)[0]
    raise TimeoutError("等待 bootloader ACK 超时")

def command(ser, cmd, addr=0, payload=b"", value_crc=None):
    ser.write(frame(cmd,addr,payload,value_crc)); ser.flush()
    rc, st, value=read_reply(ser)
    if rc != cmd or st != 0: raise RuntimeError(f"命令 0x{cmd:02X} 失败: status={st}, value={value}")
    return value

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--port",required=True); ap.add_argument("--image",required=True)
    ap.add_argument("--baud",type=int,default=2000000); ap.add_argument("--chunk",type=int,default=256)
    a=ap.parse_args(); data=open(a.image,"rb").read()
    if len(data) % 4: data += b"\xff" * (4 - len(data) % 4)
    if not data: raise SystemExit("固件为空")
    with serial.Serial(a.port,a.baud,timeout=.05,write_timeout=2) as s:
        time.sleep(.15); s.reset_input_buffer(); s.reset_output_buffer()
        command(s,CMD_HELLO)
        command(s,CMD_BEGIN,0x08008000,b"",crc32(data))
        for off in range(0,len(data),min(MAX_CHUNK,max(4,a.chunk))):
            part=data[off:off+min(MAX_CHUNK,max(4,a.chunk))]
            command(s,CMD_DATA,0x08008000+off,part)
            print(f"\r写入 {min(off+len(part),len(data))}/{len(data)}",end="",flush=True)
        command(s,CMD_END,value_crc=crc32(data))
        print("\n升级完成，等待 bootloader 校验并启动应用。")
if __name__=='__main__': main()
