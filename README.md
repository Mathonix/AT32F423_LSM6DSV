# AT32F423 LSM6DSV SPI Test

AT32F423KCU7 + LSM6DSV 姿态传感器固件、Bootloader、USB/UART/CAN 输出和 Motion Studio 上位机工程。

> 当前主线测试固件：**六轴模式、快速启动关闭、JustFloat 1000 Hz**。

## 功能概览

- LSM6DSV SPI 读取与 2 kHz 姿态融合；
- 六轴 / 九轴 VQF 模式；九轴使用 IST8310 磁力计；
- 启动静止陀螺仪零偏校准；可选历史零偏快速启动；
- USB FS CDC 与 USART4 数据输出；
- VOFA+ JustFloat 三通道/六通道输出；
- 二进制姿态、紧凑姿态和 IMU 帧协议；
- CAN2 输出与节点 ID 配置；
- WS2812 工作状态指示；
- 用户 Bootloader、DAPLink SWD 烧录工具；
- `upper/Motion_Studio` Tauri + React 桌面上位机。

## 硬件接口

| 功能 | 接口/引脚 | 默认配置 |
|---|---|---|
| LSM6DSV | SPI（见 `src/drivers/lsm6dsv.c`） | `WHO_AM_I = 0x70` |
| USART4 | PA0 TX / PA1 RX | 2,000,000 baud |
| USB CDC | PA11 DP / PA12 DM | 虚拟串口，Windows 通常显示为 USB Serial Device |
| CAN2 | PA2 RX / PA3 TX | 默认 1 Mbit/s |
| WS2812 | PA8 | 工作状态灯 |

应用程序链接地址为 `0x08008000`，Bootloader 位于应用区之前。正常应用烧录不会覆盖 Bootloader。

## 固件构建

环境要求：

- GNU Arm Embedded Toolchain；
- GNU Make；
- Windows 可使用已安装的轻量 MinGW-w64；
- 工程默认从 `C:\Users\Mathonix\.platformio\packages\toolchain-gccarmnoneeabi` 查找 ARM 工具链。

### 六轴调试固件

关闭快速启动、关闭磁力计融合：

```powershell
make -B DEBUG_BUILD=0 SIX_AXIS=1 all
```

### 九轴调试固件

```powershell
make -B DEBUG_BUILD=0 SIX_AXIS=0 all
```

### 快速启动调试版本

快速启动是编译期选项。仅在台架验证时开启：

```powershell
make -B DEBUG_BUILD=1 SIX_AXIS=1 all
```

- `DEBUG_BUILD=0`：快速启动关闭；
- `DEBUG_BUILD=1`：快速启动开启；
- `SIX_AXIS=1`：强制六轴运行并关闭磁力计融合；
- `SIX_AXIS=0`：保留九轴磁力计融合能力。

生成文件：

```text
build/lsm6dsv_spi_test.elf
build/lsm6dsv_spi_test.hex
build/lsm6dsv_spi_test.bin
```

## SWD 烧录

安装依赖：

```powershell
py -3 -m pip install pyocd
```

连接 DAPLink/SWD 后执行：

```powershell
python tools\dap\dap_flash_and_log.py `
  --hex build\lsm6dsv_spi_test.hex `
  --swd-frequency 1000000
```

确认输出包含：

```text
program done
spot verify OK
automatic reset OK
target running
```

该脚本默认只烧录应用区。烧录 Bootloader 使用 Bootloader 目录中的专用构建配置，烧录前必须确认地址和目标镜像。

## 输出协议

默认输出为 VOFA+ JustFloat little-endian `float32`：

```text
Yaw, Pitch, Roll, 0x00, 0x00, 0x80, 0x7F
```

当前三通道帧长度为 16 字节。应用协议帧使用：

```text
AA 55 | msg_id | len | seq | payload | CRC16-CCITT
```

协议命令和结构体见：

- `inc/telemetry/protocol.h`；
- `src/drivers/protocol.c`；
- `docs/`；
- `upper/Motion_Studio/src-tauri/src/services/telemetry.rs`。

已支持的主机命令包括 Ping、查询状态、流模式切换、融合模式设置、CAN 节点 ID 设置和运行时输出频率设置。部分设置需要先进入 Settings 模式并复位后生效。

## Motion Studio 上位机

目录：

```text
upper/Motion_Studio
```

功能包括串口/USB CDC 连接、实时姿态、曲线、数据记录、JustFloat 解析、中文设置页面、六轴/九轴选项、输出协议和频率配置、CAN 配置页面以及柔和浅色主题。

安装依赖并检查：

```powershell
cd upper\Motion_Studio
npm install
npm run check
npm run build
```

启动 Web 开发版：

```powershell
npm run dev
```

启动 Tauri 桌面版：

```powershell
npm run dev:desktop
```

Windows 桌面版不要求安装完整 Visual Studio IDE；Tauri/Rust 的 Windows 原生编译依赖需按本机工具链配置。硬件连接时优先选择设备枚举出的 USB CDC 串口，例如 `COM16`，波特率使用 `2000000`。

## 运行检查

连接后建议确认：

- 串口能持续接收数据；
- `Frames` 持续增加；
- `Parser` 错误保持为 0；
- 输出频率接近 1000 Hz；
- 设备转动时 Roll/Pitch/Yaw 随之变化；
- USB 重新插拔后设备能重新枚举；
- DAPLink 读取的 `vqf_live.magic` 为 `0x56465131`，`seq` 和 `millis` 持续递增。

## 重要配置

主要配置位于 `inc/app/app_config.h`：

- `APP_FUSION_HZ`：融合循环频率；
- `APP_VOFA_OUTPUT_HZ`：默认输出频率；
- `APP_GYR_FAST_START_ENABLE`：快速启动默认值；
- `APP_MAG_FUSION_ENABLE`：磁力计融合开关；
- `APP_ACC_CAL_ENABLE`：六面加速度计校准开关；
- `APP_VQF_TAU_ACC` / `APP_VQF_TAU_MAG`：VQF 时间常数。

调整传感器、时钟、USB 或协议后，必须重新编译、烧录，并使用静止和受控旋转数据验证。

## Git 约定

仓库只提交源代码、配置、脚本、文档和必要的锁文件，不提交：

- `build/`、`build_sync/`、编译产物；
- `node_modules/`、`dist/`、Rust `target/`；
- 采集 CSV、浏览器缓存、测试截图和临时 artifacts；
- 下载的 SDK 压缩包、可执行文件和本地工具目录。

提交前检查：

```powershell
git diff --check
git status --short
git diff --cached --stat
```

## 注意事项

- CAN 外部通信需要正确的 CAN 收发器、终端电阻和总线连接；回环测试不能替代实车总线测试；
- USB CDC 枚举依赖 USB 时钟、VBUS 检测和 PA11/PA12 硬件连接；
- 不要把 CAN 帧当成 JustFloat 解析；
- 生产版本建议保持快速启动关闭，台架验证历史零偏时再使用 `DEBUG_BUILD=1`。
