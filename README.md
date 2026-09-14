# AT32F423 + LSM6DSV/IST8310 2 kHz 姿态解算固件

基于 **AT32F423KCU7-4** 的嵌入式姿态解算工程，使用 ST 官方 Full VQF、LSM6DSV 六轴 IMU 和 IST8310 磁力计，提供 2 kHz 姿态融合、启动静止校准、实时遥测、磁校准和硬件诊断工具。

## 核心特性

- **LSM6DSV 六轴采样**：SPI1、Mode 0、16-bit 帧，HA01 模式下约 2 kHz 输出；陀螺仪量程为 ±1000 dps，加速度计量程为 ±4 g。
- **官方 Full VQF**：保留官方 6D/9D VQF 实现，通过 C 接口封装；支持静止零偏估计、REST 检测、九轴磁融合和诊断输出。
- **IST8310 磁力计**：软件 I2C，50 Hz 非阻塞采样；包含零偏/软铁校准矩阵和传感器到 IMU 的轴映射。是否将磁数据送入 VQF 由源码中的磁融合开关控制。
- **稳健启动校准**：上电后丢弃约 1 s 稳定时间，再采集约 3 s 静止数据；使用 32 样本分块均值和两端各 10% 截尾均值计算陀螺仪零偏。
- **输出姿态滤波**：VOFA/DAP 输出端对 yaw 使用自适应 Kalman 滤波；pitch/roll 保留 VQF 输出。滤波只影响输出姿态，不修改 VQF 内部状态。
- **实时遥测**：USART4 DMA 输出 VOFA JustFloat，DAPLink 可读取 SRAM 中的一致性快照。
- **状态指示**：WS2812B-4020 以蓝色表示启动/校准，绿色呼吸灯表示正常工作，红色表示 IMU/VQF 初始化失败，琥珀色表示磁力计初始化失败。
- **诊断固件**：提供 SPI 频率扫描、SPI 矩阵、安全探测、IST8310 单独测试等独立构建目标。

## 数据流

```text
LSM6DSV
  SPI1 2 kHz
      │
      ├─ 原始值换算 → 温度补偿 → 30 Hz 软件低通
      │
      └──────────────────────────────► Full VQF
                                          │
                                          ├─ 6D/9D 姿态
                                          └─ 输出 yaw KF
                                                   │
                                                   ├─ USART4 / VOFA JustFloat
                                                   └─ DAPLink SRAM 快照

IST8310
  软件 I2C，50 Hz
      │
      └─ 校准矩阵 + IMU 轴映射
             │
             └─ 可选磁融合更新（按 MAG_VQF_DIV 降频）
```

默认数据节奏如下，具体值集中在 `inc/app_config.h`：

| 数据路径 | 默认频率 | 说明 |
|---|---:|---|
| LSM6DSV 采样/VQF | 2000 Hz | 主融合循环 |
| IST8310 读取 | 50 Hz | 非阻塞调度，不等待 5 ms 测量周期 |
| 磁数据送入 VQF | 当前关闭 | `MAG_FUSION_ENABLE=0`；启用后由 `MAG_VQF_DIV=5` 配置为 10 Hz |
| VOFA/DAP 输出 | 200 Hz | `APP_VOFA_OUTPUT_HZ` |
| DAP 一致性快照 | 20 Hz | `vqf_live` / `vqf_tune_live` |

## 硬件连接

| 功能 | MCU 引脚 | 外设/说明 |
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
| 调试串口 TX / RX | PA0 / PA1 | USART4_TX / USART4_RX，AF8 |
| 状态 LED | PB8 | 普通 GPIO |
| WS2812B DIN | PA8 | GPIO 软件时序驱动 |

系统时钟由内部 HICK 经过 PLL 配置为 150 MHz，APB2 为 75 MHz；SPI1 使用 16 分频，SCK 约为 **4.6875 MHz**。

## 默认软件配置

主要参数位于 `inc/app_config.h`：

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `APP_FUSION_HZ` | 2000 | VQF 融合频率 |
| `APP_GYR_LPF_CUTOFF_HZ` | 30 Hz | 陀螺仪软件低通截止频率 |
| `APP_CAL_DROP_MS` | 1000 ms | 启动后丢弃样本时间 |
| `APP_CAL_REST_SECONDS` | 3.0 s | 静止零偏校准时长 |
| `APP_VOFA_OUTPUT_HZ` | 200 Hz | UART/DAP 姿态输出频率 |
| `APP_UART_BAUD` | 2000000 | USART4 波特率 |
| `APP_SFLP_BIAS_ENABLE` | 0 | 是否使用 SFLP GBIAS 初始化零偏 |
| `APP_GYR_TEMP_COMP_ENABLE` | 1 | 温度补偿路径；默认三轴系数均为 0 |

磁融合的启用状态及其更新分频以当前源码为准，避免在 README 中复制容易过期的开关值。

## 构建

### 环境要求

- GNU Arm Embedded Toolchain，需包含 `arm-none-eabi-gcc`、`arm-none-eabi-g++`、`arm-none-eabi-objcopy`、`arm-none-eabi-size`
- GNU Make
- Makefile 也会自动查找 PlatformIO 的 GCC Arm 工具链：`~/.platformio/packages/toolchain-gccarmnoneeabi/bin`
- C++ 源文件使用 `gnu++14`、`VQF_SINGLE_PRECISION`、`-fno-exceptions -fno-rtti`

### GCC / Makefile

在工程根目录执行：

```powershell
make -j4
```

默认生成：

```text
build/lsm6dsv_spi_test.elf
build/lsm6dsv_spi_test.hex
build/lsm6dsv_spi_test.bin
build/lsm6dsv_spi_test.map
```

使用独立输出目录，避免覆盖板上调试固件：

```powershell
make BUILD=build_verify -j4
```

清理默认构建目录：

```powershell
make clean
```

### 独立诊断固件

| 命令 | 输出目标 | 用途 |
|---|---|---|
| `make spi-matrix` | `build/spi_matrix.*` | SPI 引脚/模式矩阵测试 |
| `make spi-safe-probe` | `build/spi_safe_probe.*` | 非破坏性 Mode 0 对比探测 |
| `make spi-observe` | `build/spi_observe.*` | SPI1 外设信号观察 |
| `make spi-freq-sweep` | `build/spi_freq_sweep.*` | 扫描 SPI 模式与频率 |
| `make safe-idle` | `build/safe_idle.*` | 安全空闲/故障恢复镜像 |
| `make ist8310` | `build/ist8310_test.*` | IST8310 独立测试 |

也可以直接指定：

```powershell
make TARGET=my_test APP_MAIN=spi_matrix_main.c BUILD=build_my_test -j4
```

### Keil MDK

Keil 工程位于 `mdk_v5/lsm6dsv_spi_test.uvprojx`。工程需要支持 C++14 或更新标准，并定义 `VQF_SINGLE_PRECISION`；构建前应确认工程中的源文件列表与当前 Makefile 一致。

## 烧录

### CMSIS-DAP / PyOCD

安装工具：

```powershell
python -m pip install pyocd pyserial
```

烧录并读取 10 次姿态：

```powershell
python tools/dap_vqf_read.py --flash --count 10 --period 1
```

若固件已烧录，只需读取：

```powershell
python tools/dap_vqf_read.py --count 10 --period 1
```

也可直接使用任意 DAPLink/J-Link 工具烧录：

```text
build/lsm6dsv_spi_test.hex
```

AT32F423 的 Flash 起始地址为 `0x08000000`。

## 运行检查

正常启动至少应满足：

```text
WHO_AM_I = 0x70
init_err = 0
seq 持续更新
fusion_hz 接近 2000
```

`dap_vqf_read.py` 会输出 Roll/Pitch/Yaw、陀螺仪 Z 轴、零偏、低通后角速度、修正后角速度及温度。以 1 秒间隔读取 10 次时，成功结果应包含：

```text
RESULT: 10-second attitude capture OK
```

更长时间的静态漂移统计可使用：

```powershell
python tools/dap_vqf_tune.py --seconds 60 --rate 10
```

上电校准阶段必须保持板卡静止。若姿态持续为零、`WHO_AM_I` 不是 `0x70`，或状态灯保持红色/琥珀色，应先运行硬件诊断脚本，而不是直接调整 VQF 参数。

## 串口协议

USART4 使用 **2 Mbaud、8-N-1**，DMA 周期输出 VOFA+ JustFloat 帧：

```text
[float32 LE 通道 0..N-1][00 00 80 7F]
```

稳定的前三个通道为：

| 通道 | 内容 | 单位 |
|---:|---|---|
| 0 | 输出 yaw（经自适应 KF） | deg |
| 1 | VQF pitch | deg |
| 2 | VQF roll | deg |

完整通道数量和后续字段由 `src/main.c` 中的 `VOFA_N_CH` 与 `ch[]` 赋值决定；历史版本还包含温度、四元数、三轴陀螺仪、三轴加速度、VQF 耗时和诊断字段。解析时应同时依据 `VOFA_N_CH`，不要假定帧长永远相同。

串口抓包示例：

```powershell
python tools/vofa_1khz_log.py --port COM8 --baud 2000000 --seconds 3
```

## 常用工具

工具位于 `tools/`，多数脚本依赖 `pyocd`，离线分析工具还依赖 `numpy`，部分标定拟合工具依赖 `scipy`。

| 工具 | 用途 |
|---|---|
| `dap_vqf_read.py` | 烧录（可选）并读取指定次数的 VQF 姿态快照 |
| `dap_vqf_tune.py` | 连续读取遥测并分析噪声、零偏和漂移 |
| `dap_stationary_30min.py` | 长时间静止采集和漂移统计 |
| `dap_yaw_kf_compare.py` | 比较 VQF yaw 与输出 KF yaw |
| `dap_vqf_imu.py` | 通过 DAP 直连传感器并在主机运行 BasicVQF |
| `vofa_1khz_log.py` | 从 USART4 读取并解析 JustFloat 数据 |
| `mklink_vofa_rpy.py` | 通过 MicroLink SWD 读取姿态并转发到 VOFA+ TCP |
| `dap_whoami.py` | 独立检查 LSM6DSV `WHO_AM_I` |
| `dap_spi_diag.py` | 直接驱动 SPI1，隔离固件与传感器问题 |
| `dap_spi_freq_sweep.py` | SPI Mode/SCK 频率扫描 |
| `dap_spi_matrix.py` | SPI 接线和模式矩阵测试 |
| `dap_sflp_*.py` | SFLP FIFO、姿态和 GBIAS 诊断 |
| `dap_ist8310_read.py` | 独立读取 IST8310 |
| `dap_mag_*.py` | 磁数据采集、校准、映射和验证 |
| `dap_acc_*.py` | 六面或多姿态加速度计采集/验证 |
| `analyze_*.py` | 对采集 CSV/JSON 进行离线统计 |
| `fit_acc_*.py` | 拟合并验证加速度计标定参数 |

运行任意脚本查看其参数：

```powershell
python tools/dap_vqf_tune.py --help
```

## 目录结构

```text
.
├─ src/                        应用、驱动、VQF 封装和诊断固件
│  ├─ main.c                   主循环、初始校准、磁调度和输出滤波
│  ├─ lsm6dsv.c                LSM6DSV SPI 驱动
│  ├─ ist8310.c                IST8310 软件 I2C 驱动
│  ├─ vqf_full.cpp             官方 Full VQF 算法
│  ├─ vqf_wrapper.cpp          C 接口封装
│  └─ *_main.c                 独立诊断固件入口
├─ inc/
│  ├─ app_config.h             应用级参数
│  ├─ mag_calibration.h        磁零偏、软铁矩阵和轴映射
│  ├─ acc_calibration.h        加速度计临时标定参数
│  └─ vqf_live.h               DAP 遥测数据布局
├─ tools/                      DAPLink、串口、标定和分析脚本
├─ docs/                       标定与验证记录
├─ mdk_v5/                     Keil MDK 工程
├─ AT32F423_Firmware_Library/  AT32 官方固件库
├─ Makefile                    GCC 构建入口
└─ problem.md                  代码审查和历史问题记录
```

`lsm6dsv_reg.c/.h`、`_official_sensor_fusion.c` 和 `_vqf_upstream/` 是官方参考实现；默认固件并不编译全部参考代码。正式构建的实际文件列表以 `Makefile` 或 Keil 工程为准。

## 校准与调参

- 加速度计参数位于 `inc/acc_calibration.h`，来源和验证边界见 `docs/acc_calibration_20260912.md`。
- 磁力计零偏、软铁矩阵和轴映射位于 `inc/mag_calibration.h`；该校准当前属于临时标定，应通过 `dap_verify_mag_mapping.py` 和新的独立姿态数据复核。
- 启动零偏校准会拒绝明显运动样本，但上电后仍必须让板卡静止；多次校准失败会进入故障循环。
- 修改融合频率、输出频率、低通或磁更新分频后，应重新检查实时帧率、CPU 占用和姿态稳定性。
- `APP_VOFA_OUTPUT_HZ` 必须非零、不得超过融合频率，并且能够整除融合频率，否则编译时直接报错。

## 注意事项

- 默认九轴/六轴输出策略、磁融合开关或遥测字段可能随开发进度变化；以当前提交中的 `inc/app_config.h`、`inc/vqf_live.h` 和 `src/main.c` 为准。
- 磁力计和加速度计标定仅对当前板卡、安装方式和采集时的温度条件有效，不应直接复制到其他硬件。
- 高频 DAP 读取会短暂 halt CPU；用于标定和诊断时可以接受，不应将其当作实时数据总线。实时数据应使用 USART4 JustFloat。
- `build/`、日志文件和分析输出不会提交到 Git；仓库只保留源码、构建配置、必要参考文档和数据手册。

## 许可证

- VQF 相关代码依照 `LICENSES/VQF-MIT.txt` 中的 MIT 许可证使用。
- AT32F423 固件库的使用条件见 `AT32F423_Firmware_Library/LICENSE`。
- ST 官方参考代码和数据手册的权利归原权利人所有；使用和再分发前请确认对应许可和条款。
