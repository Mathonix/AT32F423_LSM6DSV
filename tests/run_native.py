"""Build and run hardware-free C regressions (GCC/MinGW; no board access)."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    os.chdir(ROOT)
    compiler = os.environ.get("HOST_CC")
    if not compiler and os.name == "nt" and shutil.which("mingw32-make"):
        compiler = str(Path(shutil.which("mingw32-make")).with_name("gcc.exe"))
    compiler = compiler or "gcc"
    output = ROOT / "build" / "native-tests"
    output.mkdir(parents=True, exist_ok=True)
    common = [compiler, "-std=gnu11", "-O1", "-flto", "-Wall", "-Wextra",
              "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
    usb_inc = ["inc/config", "middleware/usb_drivers/inc", "middleware/usbd_class/cdc",
               "AT32F423_Firmware_Library/libraries/cmsis/cm4/core_support",
               "AT32F423_Firmware_Library/libraries/cmsis/cm4/device_support",
               "AT32F423_Firmware_Library/libraries/drivers/inc"]
    cases = [
        ("protocol", ["tests/test_protocol.c", "src/drivers/protocol.c"], ["-Iinc/telemetry"]),
        ("boot_settings", ["tests/test_boot_settings.c", "tests/test_memory.c",
                           "bootloader/src/bl_protocol.c", "src/calibration/fusion_settings.c"],
         ["-Itests/stubs", "-Ibootloader/inc", "-Iinc/config", "-Iinc/app"]),
        ("usb_cdc", ["tests/test_usb_cdc.c", "tests/test_memory.c",
                     "middleware/usbd_class/cdc/cdc_class.c", "middleware/usb_drivers/src/usbd_int.c",
                     "middleware/usb_drivers/src/usbd_sdr.c"],
         ["-DAT32F423KCU7_4", "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
          "-Wno-unused-parameter"] + ["-I" + p for p in usb_inc]),
    ]
    for name, sources, flags in cases:
        exe = output / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run(common + flags + sources + ["-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
