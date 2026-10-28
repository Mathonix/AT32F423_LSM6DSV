"""Windows-only interface-2 transport. Never resets USB or changes drivers."""
import ctypes as c
from ctypes import wintypes as w
import os,uuid,time,struct,binascii

GUID_TEXT='7b926486-7eee-499c-bffc-1ca59c7dd9ed'
# Physical board serial. Kept in the environment so the repository carries no
# device identity; empty accepts the single AT32 interface that is present.
EXPECTED_SERIAL=os.environ.get('GYROA_BOARD_SERIAL','').strip()
class GUID(c.Structure):
    _fields_=[('bytes',c.c_ubyte*16)]
class INTERFACE_DATA(c.Structure):
    _fields_=[('cbSize',w.DWORD),('guid',GUID),('flags',w.DWORD),('reserved',c.c_size_t)]
class INTERFACE_DESCRIPTOR(c.Structure):
    _pack_=1
    _fields_=[(n,c.c_ubyte) for n in ['length','type','number','alternate','endpoints','cls','subcls','protocol','string']]

def paths():
    dll=c.WinDLL('setupapi',use_last_error=True)
    get=dll.SetupDiGetClassDevsW;get.argtypes=[c.POINTER(GUID),w.LPCWSTR,w.HWND,w.DWORD];get.restype=w.HANDLE
    enum=dll.SetupDiEnumDeviceInterfaces;enum.argtypes=[w.HANDLE,c.c_void_p,c.POINTER(GUID),w.DWORD,c.POINTER(INTERFACE_DATA)];enum.restype=w.BOOL
    detail=dll.SetupDiGetDeviceInterfaceDetailW;detail.argtypes=[w.HANDLE,c.POINTER(INTERFACE_DATA),c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.c_void_p];detail.restype=w.BOOL
    destroy=dll.SetupDiDestroyDeviceInfoList;destroy.argtypes=[w.HANDLE];destroy.restype=w.BOOL
    guid=GUID.from_buffer_copy(uuid.UUID(GUID_TEXT).bytes_le);h=get(c.byref(guid),None,None,0x12)
    if h==c.c_void_p(-1).value:raise c.WinError(c.get_last_error())
    result=[]
    try:
        i=0
        while True:
            data=INTERFACE_DATA();data.cbSize=c.sizeof(data)
            if not enum(h,None,c.byref(guid),i,c.byref(data)):
                if c.get_last_error()==259:break
                raise c.WinError(c.get_last_error())
            need=w.DWORD();detail(h,c.byref(data),None,0,c.byref(need),None)
            buf=c.create_string_buffer(need.value);c.cast(buf,c.POINTER(w.DWORD))[0]=8 if c.sizeof(c.c_void_p)==8 else 6
            if not detail(h,c.byref(data),buf,len(buf),c.byref(need),None):raise c.WinError(c.get_last_error())
            path=c.wstring_at(c.addressof(buf)+4)
            if 'vid_2e3c&pid_f401&mi_02' in path.lower():result.append(path)
            i+=1
    finally:destroy(h)
    return result

class WinUSB:
    def __init__(self):
        self.file=None;self.handle=w.HANDLE();self.closed=False
        self.kernel=c.WinDLL('kernel32',use_last_error=True);self.dll=c.WinDLL('winusb',use_last_error=True)
        create=self.kernel.CreateFileW;create.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE];create.restype=w.HANDLE
        self.kernel.CloseHandle.argtypes=[w.HANDLE];self.kernel.CloseHandle.restype=w.BOOL
        specs={'WinUsb_Initialize':([w.HANDLE,c.POINTER(w.HANDLE)],w.BOOL),'WinUsb_Free':([w.HANDLE],w.BOOL),
               'WinUsb_QueryInterfaceSettings':([w.HANDLE,c.c_ubyte,c.POINTER(INTERFACE_DESCRIPTOR)],w.BOOL),
               'WinUsb_GetDescriptor':([w.HANDLE,c.c_ubyte,c.c_ubyte,w.USHORT,c.c_void_p,w.ULONG,c.POINTER(w.ULONG)],w.BOOL),
               'WinUsb_SetPipePolicy':([w.HANDLE,c.c_ubyte,w.ULONG,w.ULONG,c.c_void_p],w.BOOL),
               'WinUsb_ReadPipe':([w.HANDLE,c.c_ubyte,c.c_void_p,w.ULONG,c.POINTER(w.ULONG),c.c_void_p],w.BOOL),
               'WinUsb_WritePipe':([w.HANDLE,c.c_ubyte,c.c_void_p,w.ULONG,c.POINTER(w.ULONG),c.c_void_p],w.BOOL)}
        for n,(args,result) in specs.items():f=getattr(self.dll,n);f.argtypes=args;f.restype=result
        options=paths()
        if len(options)!=1:raise RuntimeError(f'Expected exactly one AT32 WinUSB interface, got {len(options)}')
        self.path=options[0]
        try:
            self.file=create(self.path,0xc0000000,3,None,3,0x40000000,None)
            if self.file==c.c_void_p(-1).value:self.file=None;raise c.WinError(c.get_last_error())
            self._ok(self.dll.WinUsb_Initialize(self.file,c.byref(self.handle)))
            desc=INTERFACE_DESCRIPTOR();self._ok(self.dll.WinUsb_QueryInterfaceSettings(self.handle,0,c.byref(desc)))
            if (desc.number,desc.cls,desc.endpoints)!=(2,255,2):raise RuntimeError('Unexpected USB interface')
            raw=self.descriptor(1,0,0);vid,pid=struct.unpack_from('<HH',raw,8)
            if (vid,pid)!=(0x2e3c,0xf401):raise RuntimeError('Wrong USB VID/PID')
            self.serial=self.descriptor(3,raw[16],0x409)[2:].decode('utf-16-le')
            if EXPECTED_SERIAL and self.serial!=EXPECTED_SERIAL:raise RuntimeError('Wrong physical board serial')
            for pipe in [3,0x83]:
                timeout=w.ULONG(250);self._ok(self.dll.WinUsb_SetPipePolicy(self.handle,pipe,3,c.sizeof(timeout),c.byref(timeout)))
        except BaseException:self.close();raise
    @staticmethod
    def _ok(result):
        if not result:raise c.WinError(c.get_last_error())
    def descriptor(self,kind,index,lang):
        buf=c.create_string_buffer(256);n=w.ULONG()
        self._ok(self.dll.WinUsb_GetDescriptor(self.handle,kind,index,lang,buf,len(buf),c.byref(n)))
        return buf.raw[:n.value]
    def write(self,data):
        for i in range(0,len(data),64):
            part=data[i:i+64];buf=c.create_string_buffer(part);n=w.ULONG()
            self._ok(self.dll.WinUsb_WritePipe(self.handle,3,buf,len(part),c.byref(n),None))
            if n.value!=len(part):raise RuntimeError('Short WinUSB OUT transfer')
        return len(data)
    def read(self):
        buf=c.create_string_buffer(64);n=w.ULONG()
        if not self.dll.WinUsb_ReadPipe(self.handle,0x83,buf,64,c.byref(n),None):
            code=c.get_last_error()
            if code==121:return b''
            raise c.WinError(code)
        return buf.raw[:n.value]
    def close(self):
        if self.handle.value:self.dll.WinUsb_Free(self.handle);self.handle=w.HANDLE()
        if self.file is not None:self.kernel.CloseHandle(self.file);self.file=None
        self.closed=True

class AppLink:
    def __init__(self,transport):self.io=transport;self.buf=bytearray();self.seq=0;self.raw=bytearray();self.telemetry=0;self.crc_errors=0
    def read(self):
        data=self.io.read();self.raw+=data;self.buf+=data;out=[]
        while len(self.buf)>=7:
            i=self.buf.find(b'\xaa\x55')
            if i<0:self.buf=self.buf[-1:];break
            if i:del self.buf[:i]
            if len(self.buf)<7:break
            n=self.buf[3]
            if n>64:del self.buf[0];continue
            end=n+7
            if len(self.buf)<end:break
            if binascii.crc_hqx(self.buf[2:end-2],65535)!=int.from_bytes(self.buf[end-2:end],'little'):
                self.crc_errors+=1;del self.buf[0];continue
            msg,seq,payload=self.buf[2],self.buf[4],bytes(self.buf[5:end-2]);del self.buf[:end]
            if msg in [1,2,3,4,6]:self.telemetry+=1
            out.append((msg,seq,payload))
        return out
    def query(self,cmd,reply,payload=b'',timeout=2):
        self.seq=(self.seq+1)&255;body=bytes([cmd,len(payload),self.seq])+payload
        self.io.write(b'\xaa\x55'+body+struct.pack('<H',binascii.crc_hqx(body,65535)))
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            for msg,seq,data in self.read():
                if msg==reply and seq==self.seq:return data
        raise TimeoutError(f'WinUSB query {cmd:#x} -> {reply:#x}')

if __name__=='__main__':
    t=WinUSB()
    try:
        l=AppLink(t);print({'serial':t.serial,'interface':2,'firmware':l.query(0x23,0x32).hex(),'temp':l.query(0x39,0x3a).hex()})
    finally:t.close()
