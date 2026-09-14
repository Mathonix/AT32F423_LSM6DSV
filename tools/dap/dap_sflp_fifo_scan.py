from pyocd.core.helpers import ConnectHelper
from dap_sflp_attitude import DapSpi, enable_sflp, fifo_level, REG_WHO_AM_I, REG_FIFO_DATA_OUT_TAG, WHO_AM_I_VAL

s=ConnectHelper.session_with_chosen_probe(target_override="cortex_m", options={"connect_mode":"halt","frequency":1000000})
s.open(); t=s.target; t.halt(); spi=DapSpi(t)
try:
 print(f"WHO_AM_I=0x{spi.read_reg(REG_WHO_AM_I):02X}")
 enable_sflp(spi)
 print("FIFO samples:")
 for n in range(20):
  cnt=fifo_level(spi)
  if cnt:
   raw=spi.read_bytes(REG_FIFO_DATA_OUT_TAG,7)
   print(n, "level",cnt,"tag",hex(raw[0]>>3),"raw",bytes(raw).hex())
  else:
   print(n,"empty")
finally:
 spi.restore(); t.reset(); t.resume(); s.close()
