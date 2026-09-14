# AT32F423 + LSM6DSV/IST8310 2 kHz 姿态解算固件

基于 **AT32F423KCU7-4** 的嵌入式姿态解算工程。固件通过 SPI 读取 LSM6DSV 六轴 IMU，通过软件 I2C 读取 IST8310 磁力计，使用 ST 官方 Full VQF 完成姿态融合，并提供 VOFA+、SRAM 快照和诊断固件等调试手段。

> 当前 `master` 主线已完成 GCC 构建验证。CAN2、USB CDC 和九轴磁融合属于本阶段联调内容，默认配置已启用相关代码，但正式使用前仍应完成对应硬件和长时间验证。

## 1. 功能概览

- **2 kHz IMU 采样与融合**：LSM6DSV 使用 SPI1、Mode 0、16-bit 通信和 HA01 模式；陀螺仪量程为 ±1000 dps，加速度计量程为 ±4 g。
- **ST 官方 Full VQF**：保留官方 6D/9D VQF 实现，通过 C++/C 封装接入固件；支持静止零偏估计、REST 检测、磁融合和调参遥测。
- **IST8310 磁力计**：软件 I2C，50 Hz 非阻塞采样；包含磁零偏、软铁矩阵和传感器到 IMU 的轴映射。
- **九轴磁融合**：当前默认启用，每 5 个磁采样周期向 VQF 更新一次，约 10 Hz。是否启用由 `APP_MAG_FUSION_ENABLE` 和 `APP_MAG_VQF_UPDATE_DIV` 控制。
- **启动静止校准**：上电后先丢弃约 1 s 数据，再采集约 3 s 静止数据；使用 32 样本分块均值和两端各 10% 截尾均值计算陀螺仪零偏。
- **输出姿态滤波**：VOFA/DAP 输出端对 yaw 使用自适应 Kalman 滤波，pitch/roll 保留 VQF 输出；滤波只作用于输出，不修改 VQF 内部状态。
- **实时遥测**：USART4 DMA 以 2 Mbaud 输出三通道 VOFA+ JustFloat；DAPLink 可读取 SRAM 中的一致性快照。
- **CAN2 与 USB CDC 原型**：CAN2 使用 PA2/PA3，周期发送标准帧 ID `0x123`；USB OTG FS 增加 CDC 转发路径。二者仍需完成端到端验证。
- **状态指示**：WS2812B-4020 用于显示启动、校准、正常运行和传感器初始化错误状态。
- **诊断固件**：提供 SPI 矩阵、SPI 安全探测、SPI 频率扫描、SPI 观察、安全空闲和 IST8310 独立测试构建目标。

## 2. 硬件连接

| 功能 | MCU 引脚 | 说明 |
|---|---|---|
| LSM6DSV SCK | PA5 | SPI1_SCK，AF5 |
| LSM6DSV MISO | PA6 | SPI1_MISO，AF5，建议外部上拉 |
| LSM6DSV MOSI | PA7 | SPI1_MOSI，AF5 |
| LSM6DSV CS | PA4 | GPIO 软件片选，低有效 |
| LSM6DSV INT1 / INT2 | PB0 / PB1 | GPIO 输入 |
| IST8310 SCL | PB6 | 软件 I2C，开漏 |
| IST8310 SDA | PB7 | 软件 I2C，开漏 |
| IST8310 DRDY | PB2 | GPIO 输入 |
| IST8310 RSTN | PB3 | GPIO 输出 |
| CAN2 RX / TX | PA2 / PA3 | CAN2 复用，GPIO mux 9，1 Mbit/s |
| 调试串口 TX / RX | PA0 / PA1 | USART4_TX / USART4_RX，AF8 |
| 状态 LED | PB8 | 普通 GPIO |
| WS2812B DIN | PA8 | GPIO 软件时序驱动 |

系统时钟由内部 HICK 经过 PLL 配置为 **150 MHz**，APB2 为 **75 MHz**；SPI1 当前使用 16 分频，SCK 约为 **4.6875 MHz**。

## 3. 数据流

```text
LSM6DSV
  SPI1, 2000 Hz
      |
      +-- 原始值换算 -> 温度补偿 -> 30 Hz 软件低通
      |
      +------------------------------------> Full VQF
                                                |
                                                +-- 6D / 9D 姿态
                                                +-- 输出 yaw Kalman 滤波
                                                        |
                                                        +-- USART4 / VOFA JustFloat
                                                        +-- USB CDC（实验）
                                                        +-- DAPLink SRAM 快照

IST8310
  软件 I2C, 50 Hz
      |
      +-- 零偏/软铁校准 + IMU 轴映射
             |
             +-- VQF 磁更新，当前约 10 Hz
```

主要数据节奏：

| 数据路径 | 默认频率 | 说明 |
|---|---:|---|
| LSM6DSV 采样 / VQF | 2000 Hz | 主融合循环 |
| IST8310 读取 | 50 Hz | 非阻塞调度 |
| 磁数据送入 VQF | 10 Hz | 当前 `APP_MAG_VQF_UPDATE_DIV=5` |
| VOFA/USB 姿态输出 | 200 Hz | `APP_VOFA_OUTPUT_HZ` |
| DAP 一致性快照 | 20 Hz | `vqf_live` / `vqf_tune_live` |

## 4. 当前默认配置

主要参数集中在 `inc/app/app_config.h`。

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `APP_FUSION_HZ` | 2000 | VQF 融合频率 |
| `APP_GYR_LPF_CUTOFF_HZ` | 30 Hz | 陀螺仪软件低通截止频率 |
| `APP_CAL_DROP_MS` | 1000 ms | 启动后丢弃样本时间 |
| `APP_CAL_REST_SECONDS` | 3.0 s | 静止零偏校准时长 |
| `APP_VOFA_OUTPUT_HZ` | 200 Hz | UART/USB 姿态输出频率 |
| `APP_UART_BAUD` | 2000000 | USART4 波特率 |
| `APP_MAG_FUSION_ENABLE` | 1 | 启用 IST8310 九轴磁融合 |
| `APP_MAG_VQF_UPDATE_DIV` | 5 | 每 5 个磁样本更新一次 VQF |
| `APP_SFLP_BIAS_ENABLE` | 0 | 暂不使用 SFLP GBIAS 初始化零偏 |
| `APP_GYR_TEMP_COMP_ENABLE` | 1 | 温度补偿路径；三轴系数当前均为 0 |
| `APP_CAN_ENABLE` | 1 | 启用 CAN2 测试发送 |
| `APP_CAN_TX_PERIOD_MS` | 100 ms | CAN 测试帧发送周期 |
| `APP_CAN_TX_STANDARD_ID` | `0x123` | CAN 标准帧 ID |

`APP_VOFA_OUTPUT_HZ` 必须非零、不得超过融合频率，并且能够整除融合频率，否则编译会直接报错。

## 5. 目录结构

```text
.
├── AT32F423_Firmware_Library/   # Artery 官方外设库与 CMSIS
├── inc/
│   ├── app/                     # 应用级集中配置
│   ├── bsp/                     # 板级、时钟、中断、LED 和状态灯接口
│   ├── calibration/             # 加速度计与磁力计标定参数
│   ├── config/                  # AT32 外设库配置
│   ├── diagnostics/             # 诊断固件专用接口
│   ├── drivers/                 # IMU、磁力计、CAN、USB 驱动接口
│   ├── fusion/                  # VQF 对外接口
│   └── telemetry/               # DAP/SRAM 遥测结构
├── src/
│   ├── app/main.c               # 默认固件入口
│   ├── bsp/                     # MCU/板级实现
│   ├── calibration/            # 预留的标定实现目录
│   ├── diagnostics/             # 各诊断固件入口
│   ├── drivers/                 # 外设与传感器驱动
│   └── fusion/                  # Full VQF 及 C 接口封装
├── mdk_v5/                      # Keil MDK 工程和 J-Link 脚本
├── tools/
│   ├── dap/                     # DAPLink/pyOCD 采集、验证、烧录工具
│   ├── serial/                  # 串口和 VOFA+ 辅助工具
│   ├── analysis/                # 离线拟合与数据分析
│   └── legacy/                  # 历史一次性脚本
├── docs/
│   ├── calibration/             # 标定过程记录
│   ├── validation/              # 验证记录与审查结论
│   ├── review/                  # 问题审查快照
│   └── datasheets/              # 数据手册和芯片手册
├── reference/
│   ├── st/                      # ST 官方 LSM6DSV/传感器融合参考代码
│   └── usb_hick/                # USB 时钟实验参考工程
├── LICENSES/                    # 第三方许可证
├── Makefile                     # GCC 构建入口
├── README.md
└── _vqf_upstream/               # VQF 上游参考树，不参与默认构建
```

### 文件存放规则

- 应用流程、状态机和固件入口放在 `src/app/`，集中配置放在 `inc/app/`。
- MCU 时钟、中断、Board Support 和状态灯实现放在 `src/bsp/`，对应接口放在 `inc/bsp/`。
- 传感器及外设协议实现放在 `src/drivers/`，不要把调试入口混入驱动源文件。
- VQF 算法、封装及其接口放在 `src/fusion/`、`inc/fusion/`。
- 独立诊断固件入口统一放在 `src/diagnostics/`，专用接口放在 `inc/diagnostics/`。
- DAPLink、串口、离线分析脚本分别放入 `tools/dap/`、`tools/serial/`、`tools/analysis/`；历史脚本只放入 `tools/legacy/`。
- 标定记录、验证报告、历史审查和数据手册分别放入 `docs/` 对应子目录。
- ST、USB 实验等外部参考实现统一放在 `reference/`，默认构建不得依赖其中的文件。
- Artery 固件库保持独立目录；编译输出、日志、Python 缓存和临时文件统一忽略或归档到 `build/`，不进入 Git。

## 6. 构建

### 6.1 环境要求

- GNU Arm Embedded Toolchain，包含 `arm-none-eabi-gcc`、`arm-none-eabi-g++`、`arm-none-eabi-objcopy` 和 `arm-none-eabi-size`。
- GNU Make。
- Makefile 也会自动查找 PlatformIO 工具链：`~/.platformio/packages/toolchain-gccarmnoneeabi/bin`。
- C++ 源码使用 `gnu++14`、`VQF_SINGLE_PRECISION`、`-fno-exceptions -fno-rtti`。

### 6.2 GCC / Makefile

在工程根目录执行：

```powershell
make -B -j4
```

默认输出：

```text
build/lsm6dsv_spi_test.elf
build/lsm6dsv_spi_test.hex
build/lsm6dsv_spi_test.bin
build/lsm6dsv_spi_test.map
```

建议每次结构变更后使用独立构建目录进行完整验证：

```powershell
make -B -j4 BUILD=build/verify_structure
```

### 6.3 诊断固件

```powershell
make -B -j4 BUILD=build/verify_spi_matrix       spi-matrix
make -B -j4 BUILD=build/verify_spi_safe_probe   spi-safe-probe
make -B -j4 BUILD=build/verify_spi_observe      spi-observe
make -B -j4 BUILD=build/verify_spi_freq_sweep   spi-freq-sweep
make -B -j4 BUILD=build/verify_safe_idle        safe-idle
make -B -j4 BUILD=build/verify_ist8310          ist8310
```

各目标使用不同入口文件：

| Make 目标 | 输出固件 | 入口 |
|---|---|---|
| `spi-matrix` | `spi_matrix` | `src/diagnostics/spi_matrix_main.c` |
| `spi-safe-probe` | `spi_safe_probe` | `src/diagnostics/spi_safe_probe_main.c` |
| `spi-observe` | `spi_observe` | `src/diagnostics/spi_observe_main.c` |
| `spi-freq-sweep` | `spi_freq_sweep` | `src/diagnostics/spi_freq_sweep_main.c` |
| `safe-idle` | `safe_idle` | `src/diagnostics/safe_idle_main.c` |
| `ist8310` | `ist8310_test` | `src/diagnostics/ist8310_test_main.c` |

### 6.4 Keil MDK

Keil 工程位于：

```text
mdk_v5/lsm6dsv_spi_test.uvprojx
```

工程文件和 Makefile 已同步当前分层源码路径。Keil 工程当前依赖 AT32F423 DFP 和 Arm Compiler 5；本机本次仅完成 XML/路径一致性检查，未安装 `UV4.exe`，因此不能把 GCC 构建成功等同于 Keil 构建成功。

## 7. 烧录与运行检查

### 7.1 烧录

- Keil 工程生成的文件位于 `mdk_v5/objects/`，可使用 `mdk_v5/flash.jlink` 配合 J-Link 烧录。
- GCC 生成的文件位于 `build/`，可使用 DAPLink/pyOCD 或对应烧录器写入。
- 部分 `tools/dap/` 脚本包含本机路径或固定 COM 口，使用前先检查脚本头部参数。

### 7.2 VOFA+ 串口

USART4 参数：

```text
波特率：2000000
数据位：8
停止位：1
校验位：无
协议：JustFloat
通道：yaw, pitch, roll
帧长：16 字节
```

协议结构：

```text
float32 yaw + float32 pitch + float32 roll + 0x00 0x00 0x80 0x7F
```

采样日志示例：

```powershell
python tools/serial/vofa_1khz_log.py --port COM8 --baud 2000000 --seconds 3
```

### 7.3 DAPLink 快照

`inc/telemetry/vqf_live.h` 定义 SRAM 快照的 magic、序列号和字段布局。读取时使用：

```powershell
python tools/dap/dap_vqf_read.py --count 10 --period 1
```

高频 DAP 读取可能短暂 halt CPU；该方式适合标定和诊断，不应替代 USART4 实时数据链路。

### 7.4 上电检查

1. 确认上电时板卡保持静止，以便完成约 1 s 丢弃和 3 s 静止零偏校准。
2. 确认 WS2812 状态灯能够从启动状态进入正常运行状态。
3. 使用 DAP 工具检查 WHO_AM_I、`fusion_hz`、`skip_n`、`mag_err` 和 `mag_updates`。
4. 连接 USART4 后确认 2 Mbaud、三通道 JustFloat 帧连续输出。
5. 如果 CAN2 或 USB CDC 参与联调，确认总线终端、主机枚举和错误计数。

## 8. 工具说明

| 工具位置 | 用途 |
|---|---|
| `tools/dap/dap_whoami.py` | 读取 LSM6DSV WHO_AM_I |
| `tools/dap/dap_vqf_read.py` | 周期读取 VQF 实时快照 |
| `tools/dap/dap_vqf_tune.py` | 调参数据采集和回读 |
| `tools/dap/dap_stationary_30min.py` | 长时间静止漂移采集 |
| `tools/dap/dap_yaw_kf_compare.py` | VQF yaw 与输出 Kalman yaw 对比 |
| `tools/dap/dap_spi_*.py` | SPI 通信、频率、模式和安全探测 |
| `tools/dap/dap_sflp_*.py` | SFLP FIFO、姿态和 GBIAS 诊断 |
| `tools/dap/dap_ist8310_read.py` | IST8310 连通性检查 |
| `tools/dap/dap_mag_*.py` | 磁原始数据、标定和轴映射验证 |
| `tools/dap/dap_acc_*.py` | 加速度计六面及多姿态采集 |
| `tools/dap/dap_can_read.py` | CAN 状态和收发计数读取 |
| `tools/serial/vofa_1khz_log.py` | USART4 JustFloat 帧率检查 |
| `tools/serial/mklink_vofa_rpy.py` | DAPLink 姿态转 VOFA+ |
| `tools/analysis/analyze_*.py` | 日志统计和验证分析 |
| `tools/analysis/fit_acc_*.py` | 加速度计标定参数拟合 |

查看具体参数：

```powershell
python tools/dap/dap_vqf_tune.py --help
```

## 9. 校准资料

| 内容 | 路径 |
|---|---|
| 加速度计六面标定 | `docs/calibration/acc_calibration_20260912.md` |
| 加速度计多姿态流程 | `docs/calibration/acc_multipose_workflow.md` |
| 六轴标定审查 | `docs/validation/six_axis_calibration_audit_20260912.md` |
| SFLP GBIAS A/B 验证 | `docs/validation/sflp_gbias_20260913.md` |
| 问题审查快照 | `docs/review/problem.md` |
| 数据手册 | `docs/datasheets/` |

标定参数分别位于 `inc/calibration/acc_calibration.h` 和 `inc/calibration/mag_calibration.h`。标定结果只对当前板卡、安装方式和温度条件有效，复制到其他硬件前必须重新验证。

## 10. 注意事项

- 当前磁融合默认开启；如果调整磁融合开关或分频，应重新检查 yaw 收敛、磁干扰拒绝和动态响应。
- 加速度计标定已经过抽样验证，但仍需要独立多姿态数据复核，不能直接宣称全局精度已经改善。
- `APP_GYR_TEMP_COMP_ENABLE` 虽已开启，但当前三轴温度系数均为 0；在完成温度扫描前不会产生实际补偿。
- CAN2 和 USB CDC 已有实现，但双节点、总线错误、主机枚举、吞吐和热插拔恢复仍需验证。
- DAPLink 快照与 VOFA 输出布局相互独立；修改 `inc/telemetry/vqf_live.h` 后必须同步检查读取脚本。
- `build/`、日志、分析输出和 Python 缓存不提交到 Git；仓库只保留源码、构建配置、文档和必要参考资料。

## 11. 许可证

- VQF 相关代码依照 `LICENSES/VQF-MIT.txt` 中的 MIT 许可证使用。
- AT32F423 固件库的使用条件见 `AT32F423_Firmware_Library/LICENSE`。
- ST 官方参考代码和数据手册的权利归原权利人所有；使用和再分发前请确认对应许可和条款。

---

## 项目进度（截至 2026-09-14）

> 状态口径：“已完成”表示代码已纳入主线并完成过至少一轮构建或硬件抽样验证；“进行中”表示当前工作区已有实现，但仍缺少端到端或长时间验证。

### 已完成

- AT32F423KCU7-4 系统时钟与板级支持已完成：HICK + PLL 150 MHz、SPI1、USART4 DMA、DWT 延时和 WS2812 状态指示均已工作。
- LSM6DSV 主采样链路已完成：SPI1 Mode 0、16-bit、约 4.6875 MHz SCK、HA01 2000 Hz，陀螺仪 ±1000 dps、加速度计 ±4 g。
- 六轴 Full VQF 姿态主线已完成：包含启动静止校准、分块截尾均值零偏估计、30 Hz 陀螺仪低通和输出端自适应 yaw Kalman 滤波。
- USART4 遥测已完成内部抽样验证：2 Mbaud、三通道 JustFloat、200 Hz 调度；历史测试中融合频率约 1995–2002 Hz、输出约 199 Hz。
- 诊断工具链已完成：覆盖 SPI 模式/频率扫描、WHO_AM_I、IST8310、磁轴映射、加速度计标定、SFLP GBIAS、VQF 静态漂移和串口日志分析。
- 加速度计六面标定已临时写入，标定算式和实时遥测通过 DAP 抽样验证；仍缺少独立多姿态复核。
- SFLP GBIAS 启动零偏路径已完成硬件验证；60 秒静止 A/B 测试中，相对 MCU 静止均值方案的 yaw 标准差低约 25%、线性漂移低约 27%。该结果来自短时单次测试，当前主线默认仍未启用。

### 进行中

- 九轴磁融合：当前默认 `APP_MAG_FUSION_ENABLE=1`、`APP_MAG_VQF_UPDATE_DIV=5`，需要继续验证航向准确度、磁干扰、动态运动和长时间漂移。
- CAN2 测试：PA2/PA3、1 Mbit/s、100 ms 周期发送和接收计数已实现，尚需双节点联调、总线错误和断电恢复测试。
- USB CDC 原型：OTG FS CDC 枚举和 VOFA 转发路径已实现，尚需验证不同主机兼容性、持续吞吐和热插拔恢复。
- VOFA 输出：当前已压缩为 yaw/pitch/roll 三通道 16 字节帧，仍需完成外部串口长时间丢帧和接线复核。

### 本次结构整理与构建验证

- 源码已按 `app`、`bsp`、`drivers`、`fusion`、`diagnostics` 分层；头文件按相同职责归入 `inc/`。
- 工具已按 `dap`、`serial`、`analysis`、`legacy` 分类；文档已按 `calibration`、`validation`、`review`、`datasheets` 分类。
- ST 和 USB 实验参考实现已迁入 `reference/`，不参与默认 Makefile 构建。
- Makefile 与 Keil 工程已同步新路径；Keil XML 中 24 个 `FilePath` 均已验证存在。
- 默认固件 `make -B -j4 BUILD=build/verify_structure` 构建成功，大小统计为 `text=54828`、`data=612`、`bss=10232`、`dec=65672`。
- 6 个诊断目标全部构建成功；连同默认固件共 7 份验证日志，检查结果为 `warnings=0`、`errors=0`。
- `python -m compileall -q tools` 执行成功。
- Keil XML 和文件路径检查通过；由于当前环境没有 `UV4.exe`，Keil 尚未实际编译。

### 后续工作

1. 使用独立多姿态数据验证加速度计临时标定，检查三轴均值、模长误差和交叉轴参数。
2. 完成九轴磁融合在无干扰、电机/电源干扰和不同安装姿态下的对比，确认 yaw 长期稳定性。
3. 采集温度扫描数据，拟合并验证三轴陀螺仪温度补偿系数。
4. 完成 USART4 三通道帧、实际帧率和长时间丢帧检查。
5. 完成 CAN2 双节点、USB CDC 主机兼容性和长时间运行可靠性测试。
6. 在安装 Keil MDK 的环境中执行完整构建、烧录和硬件验证。