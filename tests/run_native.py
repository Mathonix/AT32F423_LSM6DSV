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
        ("attitude_output", ["tests/test_attitude_output.c", "src/fusion/attitude_output.c"], ["-Iinc/fusion"]),
        ("zaru_heading_hold", ["tests/test_zaru_heading_hold.c", "src/fusion/zaru_heading_hold.c", "src/fusion/attitude_output.c"],
         ["-Iinc/fusion", "-Iinc/app"]),
        ("acc_six_face", ["tests/test_acc_six_face.c", "tests/test_memory.c", "src/calibration/acc_six_face.c", "src/calibration/acc_calibration.c"],
         ["-Itests/stubs", "-Iinc/calibration", "-Iinc/app"]),
        ("relative_yaw", ["tests/test_relative_yaw.cpp", "src/fusion/vqf_wrapper.cpp", "src/fusion/vqf_full.cpp"],
         ["-Iinc/fusion", "-Iinc/app", "-DVQF_SINGLE_PRECISION"]),
        ("motion_bias", ["tests/test_motion_bias.cpp", "src/fusion/vqf_wrapper.cpp", "src/fusion/vqf_full.cpp",
                         "src/fusion/zaru_heading_hold.c"],
         ["-Iinc/fusion", "-Iinc/app", "-DVQF_SINGLE_PRECISION"]),
        ("gyro_startup_calibration", ["tests/test_gyro_startup_calibration.c", "src/calibration/gyro_startup_calibration.c"],
         ["-Iinc/calibration", "-Iinc/app"]),
        ("protocol", ["tests/test_protocol.c", "src/drivers/protocol.c"], ["-Iinc/telemetry"]),
        ("can_protocol", ["tests/test_can_protocol.c", "src/drivers/can_protocol.c"], ["-Iinc/telemetry", "-Iinc/app"]),
        ("can_driver", ["tests/test_can_driver.c", "src/drivers/can_test.c", "src/drivers/can_protocol.c"],
         ["-Itests/stubs/can", "-Iinc/telemetry", "-Iinc/app", "-Iinc/drivers", "-Wno-unused-but-set-variable"]),
        ("boot_settings", ["tests/test_boot_settings.c", "tests/test_memory.c",
                           "bootloader/src/bl_protocol.c", "src/calibration/fusion_settings.c",
                           "src/fusion/zaru_heading_hold.c", "src/drivers/protocol.c", "src/drivers/can_protocol.c"],
         ["-Itests/stubs", "-Ibootloader/inc", "-Iinc/config", "-Iinc/app", "-Iinc/telemetry", "-Iinc/fusion"]),
        ("gyro_bias_history", ["tests/test_gyro_bias_history.c", "tests/test_memory.c",
                               "src/calibration/gyro_bias_history.c"],
         ["-Itests/stubs", "-Iinc/calibration", "-Iinc/app"]),
        ("vqf_static_cal", ["tests/test_vqf_static_cal.c", "tests/test_memory.c",
                            "src/calibration/vqf_static_cal.c"],
         ["-Itests/stubs", "-Iinc/calibration", "-Iinc/app"]),
        ("usb_cdc", ["tests/test_usb_cdc.c", "tests/test_memory.c",
                     "middleware/usbd_class/cdc/cdc_class.c", "middleware/usb_drivers/src/usbd_int.c",
                     "middleware/usb_drivers/src/usbd_sdr.c"],
         ["-DAT32F423KCU7_4", "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
          "-Wno-unused-parameter"] + ["-I" + p for p in usb_inc]),
    ]
    for name, sources, flags in cases:
        exe = output / (name + (".exe" if os.name == "nt" else ""))
        case_common = common
        if any(source.endswith(".cpp") for source in sources):
            cxx = os.environ.get("HOST_CXX") or str(Path(compiler).with_name("g++.exe" if os.name == "nt" else "g++"))
            case_common = [cxx, "-std=c++11"] + common[2:]
        subprocess.run(case_common + flags + sources + ["-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
