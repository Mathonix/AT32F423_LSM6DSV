# 项目文件路径迁移说明

## 1. 文档信息

| 项目 | 内容 |
|---|---|
| 文档日期 | 2026-09-14 |
| 迁移基线 | `94467bd docs: add project progress summary` |
| 路径迁移提交 | `9ce9f8b refactor: reorganize project structure and README` |
| 适用范围 | 源码、头文件、工具、文档、第三方参考代码及 Makefile/Keil 工程路径 |

本文档用于记录 `9ce9f8b` 提交中的文件路径调整，方便本地脚本、IDE 工程和开发人员同步修改旧路径。除路径迁移外，该提交还包含 README 重写以及 CAN、USB CDC 等功能更新；这些内容变化不属于本文档的迁移清单范围。

## 2. 迁移目标与目录规则

- `src/` 和 `inc/` 按应用、板级支持、驱动、融合、标定、诊断、配置和遥测职责分层。
- `tools/` 按 DAP、串口、离线分析和历史脚本分类，避免所有 Python 工具平铺在根目录。
- `docs/` 按标定、验证、审查和数据手册分类。
- ST 官方代码、USB HICK 参考工程及芯片寄存器文件统一归入 `reference/`。
- `README.md`、`Makefile`、`.gitignore` 和 `mdk_v5/lsm6dsv_spi_test.uvprojx` 保留在固定入口位置，但内部引用路径已同步更新。

迁移后的主要顶层结构如下：

```text
.
├── inc/{app,bsp,calibration,config,diagnostics,drivers,fusion,telemetry}/
├── src/{app,bsp,diagnostics,drivers,fusion}/
├── tools/{dap,serial,analysis,legacy}/
├── docs/{calibration,validation,review,datasheets}/
├── reference/{st,usb_hick}/
├── mdk_v5/
├── Makefile
└── README.md
```

## 3. 文档路径迁移

| 旧路径 | 新路径 |
|---|---|
| `docs/acc_calibration_20260912.md` | `docs/calibration/acc_calibration_20260912.md` |
| `docs/acc_multipose_workflow.md` | `docs/calibration/acc_multipose_workflow.md` |
| `AT32F423_RM_EN_V2.03.pdf` | `docs/datasheets/AT32F423_RM_EN_V2.03.pdf` |
| `AT32F423_RM_EN_V2.03.txt` | `docs/datasheets/AT32F423_RM_EN_V2.03.txt` |
| `IST8310_datasheet.pdf` | `docs/datasheets/IST8310_datasheet.pdf` |
| `IST8310_datasheet.txt` | `docs/datasheets/IST8310_datasheet.txt` |
| `LSM6DSV_datasheet.pdf` | `docs/datasheets/LSM6DSV_datasheet.pdf` |
| `LSM6DSV_datasheet.txt` | `docs/datasheets/LSM6DSV_datasheet.txt` |
| `problem.md` | `docs/review/problem.md` |
| `docs/sflp_gbias_20260913.md` | `docs/validation/sflp_gbias_20260913.md` |
| `docs/six_axis_calibration_audit_20260912.md` | `docs/validation/six_axis_calibration_audit_20260912.md` |

说明：数据手册由工程根目录移动到 `docs/datasheets/`；流程和验证记录分别移动到 `docs/calibration/`、`docs/validation/`；原根目录的 `problem.md` 移动到 `docs/review/`。

## 4. 头文件路径迁移

| 旧路径 | 新路径 |
|---|---|
| `inc/app_config.h` | `inc/app/app_config.h` |
| `inc/at32f423_clock.h` | `inc/bsp/at32f423_clock.h` |
| `inc/at32f423_int.h` | `inc/bsp/at32f423_int.h` |
| `inc/bsp.h` | `inc/bsp/bsp.h` |
| `inc/ws2812.h` | `inc/bsp/ws2812.h` |
| `inc/acc_calibration.h` | `inc/calibration/acc_calibration.h` |
| `inc/mag_calibration.h` | `inc/calibration/mag_calibration.h` |
| `inc/at32f423_conf.h` | `inc/config/at32f423_conf.h` |
| `inc/spi_matrix.h` | `inc/diagnostics/spi_matrix.h` |
| `inc/ist8310.h` | `inc/drivers/ist8310.h` |
| `inc/lsm6dsv.h` | `inc/drivers/lsm6dsv.h` |
| `inc/vqf.h` | `inc/fusion/vqf.h` |
| `inc/vqf_full.hpp` | `inc/fusion/vqf_full.hpp` |
| `inc/vqf_live.h` | `inc/telemetry/vqf_live.h` |

说明：`inc/lsm6dsv_reg.h` 已作为 ST 参考文件移到 `reference/st/`，不再作为项目公共头文件使用。

## 5. 源文件路径迁移

| 旧路径 | 新路径 |
|---|---|
| `src/main.c` | `src/app/main.c` |
| `src/at32f423_clock.c` | `src/bsp/at32f423_clock.c` |
| `src/at32f423_int.c` | `src/bsp/at32f423_int.c` |
| `src/bsp.c` | `src/bsp/bsp.c` |
| `src/ws2812.c` | `src/bsp/ws2812.c` |
| `src/ist8310_test_main.c` | `src/diagnostics/ist8310_test_main.c` |
| `src/safe_idle_main.c` | `src/diagnostics/safe_idle_main.c` |
| `src/spi_freq_sweep_main.c` | `src/diagnostics/spi_freq_sweep_main.c` |
| `src/spi_matrix_main.c` | `src/diagnostics/spi_matrix_main.c` |
| `src/spi_observe_main.c` | `src/diagnostics/spi_observe_main.c` |
| `src/spi_safe_probe_main.c` | `src/diagnostics/spi_safe_probe_main.c` |
| `src/ist8310.c` | `src/drivers/ist8310.c` |
| `src/lsm6dsv.c` | `src/drivers/lsm6dsv.c` |
| `src/vqf_full.cpp` | `src/fusion/vqf_full.cpp` |
| `src/vqf_wrapper.cpp` | `src/fusion/vqf_wrapper.cpp` |

说明：主程序移入 `src/app/`，六个诊断入口移入 `src/diagnostics/`，业务驱动和融合实现分别归入 `src/drivers/`、`src/fusion/`。原根目录 `_official_sensor_fusion.c` 和寄存器实现已移动到 `reference/st/`。

## 6. 工具路径迁移

| 旧路径 | 新路径 |
|---|---|
| `tools/analyze_led_ab.py` | `tools/analysis/analyze_led_ab.py` |
| `tools/analyze_mag_validation.py` | `tools/analysis/analyze_mag_validation.py` |
| `tools/analyze_nine_static.py` | `tools/analysis/analyze_nine_static.py` |
| `tools/fit_acc_multipose.py` | `tools/analysis/fit_acc_multipose.py` |
| `tools/fit_acc_six_faces.py` | `tools/analysis/fit_acc_six_faces.py` |
| `tools/dap_acc_multipose.py` | `tools/dap/dap_acc_multipose.py` |
| `tools/dap_acc_six_faces.py` | `tools/dap/dap_acc_six_faces.py` |
| `tools/dap_bus_recover.py` | `tools/dap/dap_bus_recover.py` |
| `tools/dap_flash_and_log.py` | `tools/dap/dap_flash_and_log.py` |
| `tools/dap_gpio_diag.py` | `tools/dap/dap_gpio_diag.py` |
| `tools/dap_i2c_recover.py` | `tools/dap/dap_i2c_recover.py` |
| `tools/dap_ist8310_read.py` | `tools/dap/dap_ist8310_read.py` |
| `tools/dap_led_ab.py` | `tools/dap/dap_led_ab.py` |
| `tools/dap_mag_calibrate.py` | `tools/dap/dap_mag_calibrate.py` |
| `tools/dap_mag_raw_capture.py` | `tools/dap/dap_mag_raw_capture.py` |
| `tools/dap_mag_validate.py` | `tools/dap/dap_mag_validate.py` |
| `tools/dap_nine_static.py` | `tools/dap/dap_nine_static.py` |
| `tools/dap_recover.py` | `tools/dap/dap_recover.py` |
| `tools/dap_recover_3wire.py` | `tools/dap/dap_recover_3wire.py` |
| `tools/dap_sflp_attitude.py` | `tools/dap/dap_sflp_attitude.py` |
| `tools/dap_sflp_diag.py` | `tools/dap/dap_sflp_diag.py` |
| `tools/dap_sflp_fifo_scan.py` | `tools/dap/dap_sflp_fifo_scan.py` |
| `tools/dap_sflp_gbias_read.py` | `tools/dap/dap_sflp_gbias_read.py` |
| `tools/dap_spi_diag.py` | `tools/dap/dap_spi_diag.py` |
| `tools/dap_spi_freq_sweep.py` | `tools/dap/dap_spi_freq_sweep.py` |
| `tools/dap_spi_matrix.py` | `tools/dap/dap_spi_matrix.py` |
| `tools/dap_spi_observe.py` | `tools/dap/dap_spi_observe.py` |
| `tools/dap_spi_safe_probe.py` | `tools/dap/dap_spi_safe_probe.py` |
| `tools/dap_stationary_30min.py` | `tools/dap/dap_stationary_30min.py` |
| `tools/dap_verify_acc_calibration.py` | `tools/dap/dap_verify_acc_calibration.py` |
| `tools/dap_verify_mag_mapping.py` | `tools/dap/dap_verify_mag_mapping.py` |
| `tools/dap_vqf_imu.py` | `tools/dap/dap_vqf_imu.py` |
| `tools/dap_vqf_read.py` | `tools/dap/dap_vqf_read.py` |
| `tools/dap_vqf_tune.py` | `tools/dap/dap_vqf_tune.py` |
| `tools/dap_whoami.py` | `tools/dap/dap_whoami.py` |
| `tools/dap_yaw_kf_compare.py` | `tools/dap/dap_yaw_kf_compare.py` |
| `tools/_dump_recover.py` | `tools/legacy/_dump_recover.py` |
| `tools/_gpio_test.py` | `tools/legacy/_gpio_test.py` |
| `tools/_i2c_probe.py` | `tools/legacy/_i2c_probe.py` |
| `tools/_inspect_recover.py` | `tools/legacy/_inspect_recover.py` |
| `tools/_mode0.py` | `tools/legacy/_mode0.py` |
| `tools/_read3.py` | `tools/legacy/_read3.py` |
| `tools/_recover.py` | `tools/legacy/_recover.py` |
| `tools/_recover_noreset.py` | `tools/legacy/_recover_noreset.py` |
| `tools/mklink_vofa_rpy.py` | `tools/serial/mklink_vofa_rpy.py` |
| `tools/vofa_1khz_log.py` | `tools/serial/vofa_1khz_log.py` |

分类规则：

- `tools/dap_*.py` → `tools/dap/dap_*.py`
- `tools/analyze_*.py`、`tools/fit_acc_*.py` → `tools/analysis/`
- `tools/_*.py` → `tools/legacy/`
- VOFA 和串口辅助脚本 → `tools/serial/`

## 7. 第三方参考路径迁移

| 旧路径 | 新路径 |
|---|---|
| `src/lsm6dsv_reg.c` | `reference/st/lsm6dsv_reg.c` |
| `inc/lsm6dsv_reg.h` | `reference/st/lsm6dsv_reg.h` |
| `_official_sensor_fusion.c` | `reference/st/official_sensor_fusion.c` |

`reference/usb_hick/` 为本次新增的 USB 时钟参考工程；`reference/st/` 保存 ST 官方传感器融合和 LSM6DSV 寄存器相关代码。

## 8. 新增文件

以下文件在路径迁移提交中新增，因此没有旧路径：

| 新路径 |
|---|
| `inc/drivers/can_test.h` |
| `inc/drivers/usb_cdc.h` |
| `reference/usb_hick/inc/at32f423_clock.h` |
| `reference/usb_hick/inc/at32f423_conf.h` |
| `reference/usb_hick/inc/at32f423_int.h` |
| `reference/usb_hick/inc/usb_conf.h` |
| `reference/usb_hick/Makefile` |
| `reference/usb_hick/src/at32f423_clock.c` |
| `reference/usb_hick/src/at32f423_int.c` |
| `reference/usb_hick/src/main.c` |
| `src/drivers/can_test.c` |
| `src/drivers/usb_cdc.c` |
| `tools/dap/dap_can_read.py` |
| `docs/review/file_path_migration_20260914.md` |

CAN 与 USB CDC 驱动已加入 `inc/drivers/`、`src/drivers/`；`tools/dap/dap_can_read.py` 用于 CAN 数据读取；USB HICK 参考文件统一存放在 `reference/usb_hick/`。

## 9. 构建配置变更

### 9.1 Makefile

- 默认应用入口由 `main.c` 改为 `app/main.c`。
- C/C++ 源文件路径改为 `src/app`、`src/bsp`、`src/drivers`、`src/fusion` 和 `src/diagnostics`。
- 头文件搜索路径增加 `inc` 下各职责子目录。
- 诊断目标入口改为 `diagnostics/*.c`。
- CAN 与 USB CDC 驱动源文件及 AT32 固件库对应源文件已加入构建。

### 9.2 Keil MDK 工程

- `mdk_v5/lsm6dsv_spi_test.uvprojx` 中的源文件路径已同步到新的 `src/` 分层目录。
- IncludePath 增加 `inc/app`、`inc/bsp`、`inc/calibration`、`inc/config`、`inc/diagnostics`、`inc/drivers`、`inc/fusion` 和 `inc/telemetry`。
- AT32 固件库和 CMSIS 文件的相对路径已修正。
- 工程中加入 `src/drivers/can_test.c`、`src/drivers/usb_cdc.c` 及对应固件库源文件。

### 9.3 忽略规则

`.gitignore` 已补齐构建产物、Python 缓存、日志和临时文件规则，避免迁移后的对象文件、列表文件和本地生成物进入版本库。

## 10. 迁移后的验证结果

- GCC 默认固件及 6 个诊断目标均构建成功。
- 7 份构建日志均为 `warnings=0 errors=0`。
- 默认镜像占用：`text=54828`、`data=612`、`bss=10232`、`dec=65672`。
- `python -m compileall -q tools` 执行成功。
- Keil 工程共检查 24 条路径，未发现缺失。
- 当前环境缺少 `UV4.exe`，因此未执行 Keil 命令行编译；Keil 路径变更需在安装 MDK 的机器上再做一次实际编译确认。

## 11. 使用与兼容性说明

- 拉取该提交后，本地脚本中的旧路径必须替换为本文档中的新路径。
- 诊断构建示例：`make APP_MAIN=diagnostics/spi_matrix_main.c`。
- 若本地保留旧对象文件，建议先执行清理再重新构建，避免旧路径依赖残留。
- Git 能识别大部分文件为重命名；少量文件在同次提交中同时修改了内容，因此相似度小于 100%。
