# AT32F423 LSM6DSV固件

AT32F423KCU7 + LSM6DSV 姿态传感器固件、Bootloader、USB/UART/CAN 输出和状态指示灯功能。

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

## 陀螺仪启动与零偏校准

陀螺仪启动采用“静止校准 + 可选历史零偏快速启动”的设计，保证正常版本的可靠性，同时为台架测试提供快速进入姿态输出的方式。

### 正常启动模式

生产版本默认关闭快速启动：

1. 上电后初始化 LSM6DSV；
2. 丢弃启动初期不稳定样本；
3. 检测设备是否处于静止状态；
4. 在静止窗口内采集陀螺仪和加速度计数据；
5. 计算当前陀螺仪零偏并注入姿态融合算法；
6. 进入正常姿态输出。

正常启动模式不会直接信任旧的零偏记录，适合产品固件和长期运行场景。

### 历史零偏快速启动

调试版本可使用历史零偏快速启动：

- 优先读取 Flash 中与当前温度匹配的历史零偏；
- 读取成功后立即开始姿态输出；
- 后台继续进行静止检测；
- 确认设备静止后，以小步长修正当前零偏；
- 零偏变化达到条件时保存新的温度关联记录；
- 没有有效历史记录时使用固定默认零偏，并通过灯效提示。

快速启动仅在编译时开启，不建议默认用于生产版本：

```powershell
# 关闭快速启动，生产/常规测试
make -B DEBUG_BUILD=0 SIX_AXIS=1 all

# 开启快速启动，台架验证
make -B DEBUG_BUILD=1 SIX_AXIS=1 all
```

相关配置位于 `inc/app/app_config.h`：

- `APP_GYR_FAST_START_ENABLE`：快速启动开关；
- `APP_GYR_FAST_START_REST_MS`：后台静止确认时间；
- `APP_GYR_FAST_START_SAVE_MS`：允许更新历史零偏的最短时间；
- `APP_GYR_BIAS_TEMP_WINDOW_C`：历史零偏温度匹配范围；
- `APP_GYR_FAST_START_BLEND`：后台零偏渐进修正系数。

## WS2812 灯效状态

WS2812 连接在 PA8，用于提示当前传感器、融合和校准状态。灯效不会改变数据协议，仅用于现场快速判断设备状态。

| 灯效 | 含义 |
|---|---|
| 正常呼吸灯 | 设备已经运行，姿态融合和数据输出处于正常工作状态 |
| 六轴工作状态 | 当前按六轴模式运行，磁力计不参与融合 |
| 九轴工作状态 | 当前按九轴模式运行，磁力计有效并参与融合 |
| 绿色/黄色提示 | 九轴模式下磁力计暂不可用、磁场异常或融合暂时退化为有效的六轴姿态 |
| 红灯短闪两次 | 没有有效历史零偏，快速启动使用了固定默认零偏；后台仍会继续静止修正 |
| 红色故障灯 | 传感器初始化失败、校准失败或其他严重运行错误 |
| 设置/校准闪烁 | 正在执行设置、零偏校准或六面加速度计校准流程 |

启动后如果看到正常呼吸灯夹杂两次红灯闪烁，表示设备仍可正常工作，但本次没有找到可用的历史零偏。保持设备静止一段时间，后台校准完成后会更新零偏历史。
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
- `docs/`。

已支持的主机命令包括 Ping、查询状态、流模式切换、融合模式设置、CAN 节点 ID 设置和运行时输出频率设置。部分设置需要先进入 Settings 模式并复位后生效。

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

## 测试视频

以下视频用于记录当前固件的六轴、九轴和六轴快速启动测试结果：

#### 九轴模式测试

<video controls preload="metadata" width="720" src="docs/videos/nie_axis.mp4">
  您的浏览器不支持 HTML5 视频播放，请[点击下载视频](docs/videos/nie_axis.mp4)。
</video>

#### 六轴模式测试

<video controls preload="metadata" width="720" src="docs/videos/six_axis.mp4">
  您的浏览器不支持 HTML5 视频播放，请[点击下载视频](docs/videos/six_axis.mp4)。
</video>

#### 六轴快速启动测试

<video controls preload="metadata" width="720" src="docs/videos/six_axis_fastboot.mp4">
  您的浏览器不支持 HTML5 视频播放，请[点击下载视频](docs/videos/six_axis_fastboot.mp4)。
</video>

视频文件位于 `docs/videos/`，也可直接下载查看。

### 测试数据概括

以下数据来自三段视频中约每 15 秒取一个样本、共约 21 个点的 I0（Yaw）记录，数值单位按界面显示的度数统计：

| 视频 | 起始 I0 | 5 分钟末 I0 | 首尾变化 | 极差 | 现象概括 |
|---|---:|---:|---:|---:|---|
| `nie_axis.mp4` | 170.8872 | 170.8856 | **-0.0016°** | **0.4797°** | 长期不漂，但有明显来回摆动，体现磁力计修正带来的波动 |
| `six_axis.mp4` | -0.4836 | -0.5981 | **-0.1145°** | 0.1283° | 曲线非常平滑，但存在持续单向漂移，符合纯陀螺积分特征 |
| `six_axis_fastboot.mp4` | -0.0788 | +0.0188 | **+0.0976°** | 0.1558° | 启动后先向负方向漂移，再反向变化，表现出快速启动零偏后台收敛过程 |

> 注：上述数据用于当前三段视频的工程测试对比，不代表所有温度、姿态和安装条件下的最终指标。

## 注意事项

- CAN 外部通信需要正确的 CAN 收发器、终端电阻和总线连接；回环测试不能替代实车总线测试；
- USB CDC 枚举依赖 USB 时钟、VBUS 检测和 PA11/PA12 硬件连接；
- 不要把 CAN 帧当成 JustFloat 解析；
- 生产版本建议保持快速启动关闭，台架验证历史零偏时再使用 `DEBUG_BUILD=1`。
