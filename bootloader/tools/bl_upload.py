"""AT32F423 user bootloader uploader for USB CDC or UART.
Protocol is shared by both transports; USB CDC is exposed as a COM port.
"""
import argparse, binascii, struct, time, zlib
from pathlib import Path
import serial

MAGIC=b"BL"; VERSION=1; APP_BASE=0x08008000; MAX_CHUNK=256

def packet(cmd, seq, addr, length, image_crc, payload=b""):
    h=struct.pack("<2sBBHIII", MAGIC, VERSION, cmd, seq & 0xffff, addr, length, image_crc)
    return h+payload

def ack_ok(data, cmd):
    if len(data)!=14 or data[:2]!=MAGIC or data[2]!=VERSION or data[3]!=(cmd|0x80): return None
    status=data[4]; value=struct.unpack_from("<I",data,6)[0]
    got=struct.unpack_from("<I",data,10)[0]
    if (zlib.crc32(data[:10]) & 0xffffffff)!=got: return None
    if status: raise RuntimeError(f"bootloader rejected cmd 0x{cmd:02X}, status={status}, value=0x{value:08X}")
    return value

def read_ack(ser, cmd, timeout=3.0):
    end=time.monotonic()+timeout; buf=bytearray()
    while time.monotonic()<end:
        buf += ser.read(min(4096, ser.in_waiting or 1))
        while len(buf)>=14:
            i=buf.find(MAGIC)
            if i<0:
                del buf[:len(buf) - int(buf[-1:] == MAGIC[:1])]
                break
            if i: del buf[:i]
            if len(buf)<14: break
            frame=bytes(buf[:14])
            v=ack_ok(frame,cmd)
            if v is not None:
                del buf[:14]
                return v
            del buf[0]
    raise TimeoutError(f"timeout waiting ACK for 0x{cmd:02X}")

def request(ser, cmd, seq, addr, length, image_crc, payload=b""):
    ser.write(packet(cmd,seq,addr,length,image_crc,payload)); ser.flush()
    return read_ack(ser,cmd)

def enter_bootloader(ser):
    # The legacy four-byte reset requests APPLICATION reset, not update mode.
    body = bytes((0x16, 0, 0))
    ser.write(b"\xAA\x55" + body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF)))
    ser.flush(); time.sleep(0.4)

def validate_image(data):
    if not 8 <= len(data) <= 0x34000:
        raise ValueError("image size outside application partition")
    sp, pc = struct.unpack_from("<II", data)
    if not (0x20000000 < sp <= 0x2000C000 and sp % 8 == 0 and
            pc & 1 and APP_BASE <= (pc & ~1) < APP_BASE + len(data)):
        raise ValueError("invalid application vectors (link the image at 0x08008000)")

def upload(port, baud, path, enter):
    data=Path(path).read_bytes()
    validate_image(data)
    image_crc=zlib.crc32(data)&0xffffffff
    ser=serial.Serial(port, baudrate=baud, timeout=0.05, write_timeout=2)
    try:
        if enter: enter_bootloader(ser); time.sleep(1.0)
        ser.reset_input_buffer(); request(ser,1,0,0,0,0)
        request(ser,2,1,APP_BASE,len(data),image_crc)
        for off in range(0,len(data),MAX_CHUNK):
            chunk=data[off:off+MAX_CHUNK]
            next_off=request(ser,3,off//MAX_CHUNK,APP_BASE+off,len(chunk),zlib.crc32(chunk)&0xffffffff,chunk)
            if next_off!=off+len(chunk): raise RuntimeError(f"offset mismatch: expected {off+len(chunk)}, got {next_off}")
            print(f"\r{next_off}/{len(data)} ({next_off*100/len(data):5.1f}%)",end="",flush=True)
        request(ser,4,0,APP_BASE,len(data),image_crc)
        request(ser,6,0,0,0,0)
        print("\nUpload complete; application startup requested.")
    finally: ser.close()

def main():
    ap=argparse.ArgumentParser(description="AT32F423 USB CDC/UART user bootloader uploader")
    ap.add_argument("image", help="application .bin file (linked at 0x08008000)")
    ap.add_argument("--port",required=True,help="COM port, USB CDC or UART adapter")
    ap.add_argument("--baud",type=int,default=2000000)
    group = ap.add_mutually_exclusive_group()
    group.add_argument(
        "--enter",
        action="store_true",
        help="best-effort framed bootloader-entry request to the running application",
    )
    group.add_argument(
        "--no-enter",
        action="store_true",
        help="do not send an application request; recommended",
    )
    a=ap.parse_args(); upload(a.port,a.baud,a.image,a.enter)
if __name__=="__main__": main()
