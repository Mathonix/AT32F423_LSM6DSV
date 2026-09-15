# AT32F423 LSM6DSV SPI Test

## 项目简介

本工程面向 AT32F423KCU7 + LSM6DSV，提供 SPI 传感器采集、六轴/九轴 VQF 姿态融合、启动静置零偏校准、状态灯和三路数据输出。

当前协议保持不变：

- **UART**：JustFloat 二进制帧（yaw、pitch、roll），USART4，PA0=TX、PA1=RX，默认 2 Mbps。
- **USB**：USB FS CDC，PA11=DP、PA12=DM，输出 JustFloat 数据。
- **CAN**：CAN2，PA2=RX、PA3=TX，1 Mbps，采用达妙 IMU 格式。
- **WS2812**：PA8。

## 目录

```text
src/                 MCU 应用与驱动
inc/                 应用头文件和配置
bootloader/          用户 Bootloader 源码及升级工具
tools/dap/            DAPLink 烧录、复位和遥测工具
tools/usb_host/      Python 上位机
tools/electron_host/ Electron 上位机
reference/           官方例程和参考代码
docs/                校准、协议和调试文档
```

主应用地址为 `0x08008000`。六面加速度计自动校准逻辑已保留，但默认关闭：

```c
#define APP_ACC_CAL_ENABLE 0U
```

## 编译

在仓库根目录执行：

```powershell
make -B -j2
```

产物位于 `build/`：

```text
build/lsm6dsv_spi_test.elf
build/lsm6dsv_spi_test.hex
build/lsm6dsv_spi_test.bin
```

构建产物、Python 缓存、Node.js 依赖、采集 CSV 和压缩包均已加入 `.gitignore`，不要将这些生成文件提交到 Git。

## DAPLink 烧录与复位

先安装 Python 依赖：

```powershell
py -3 -m pip install pyocd
```

使用 DAPLink 烧录并自动复位运行（SWD 速度不低于 1 MHz）：

```powershell
py -3 tools/dap/dap_flash_and_log.py `
  --hex build/lsm6dsv_spi_test.hex `
  --swd-frequency 1000000
```

如果系统中默认的 `python` 不是 Python 3.10，可使用：

```powershell
py -3.10 -m pip install pyocd
py -3.10 tools/dap/dap_flash_and_log.py --hex build/lsm6dsv_spi_test.hex --swd-frequency 1000000
```

烧录后应检查 `program done`、`spot verify OK` 和 `automatic reset OK`。如果只显示旧数据，先确认目标已复位且应用已运行，不要把旧 SRAM 中残留的遥测结构当成实时零漂数据。

## 运行检查

DAPLink 读取遥测前确认：

- `vqf_live.magic == 0x56465131`；
- `seq` 持续递增；
- `millis` 持续递增；
- `whoami == 0x70`；
- `fusion_hz` 为预期值。

若传感器初始化失败，应用会进入可重试路径，持续更新故障遥测并周期性重试 SPI/LSM6DSV 初始化，而不是永久停在故障死循环中。

## 主要配置

配置集中在 `inc/app/app_config.h`，常用项目包括：

- `APP_FUSION_HZ`：融合循环频率；
- `APP_GYR_LPF_CUTOFF_HZ`：陀螺仪低通截止频率；
- `APP_CAL_REST_SECONDS`：启动静置校准窗口；
- `APP_CAL_DROP_MS`：启动后丢弃样本时间；
- `APP_VQF_TAU_ACC`、`APP_VQF_TAU_MAG`：VQF 加速度计/磁力计校正时间常数；
- `APP_VQF_MOTION_BIAS_ENABLE`：运动中零偏估计开关，快速运动和振动场景通常保持关闭；
- `APP_VOFA_OUTPUT_HZ`：VOFA/JustFloat 输出频率；
- `APP_ACC_CAL_ENABLE`：六面加速度计校准开关。

调整后必须重新编译、烧录，并使用静置和受控旋转数据验证，不能只依据一次短时读数判断长期零漂。

## 协议说明

UART 和 USB 输出使用 VOFA+ JustFloat 格式，接收端按 little-endian `float32` 解析；CAN 输出保持达妙 IMU 协议，具体帧 ID、缩放和字段定义见 `docs/` 及 `inc/telemetry/protocol.h`。不要将 CAN 帧误当作 JustFloat，也不要修改 UART/USB 的既有协议而不同时更新上位机。

## Git 推送故障排查

此前 `git push` 出现 HTTP 408，主要原因是最新提交误包含了约 28 MB 的采集 CSV、Node.js `node_modules`、Electron/Workerd 可执行文件以及 ZIP 压缩包，导致 Git 通过 HTTPS 打包上传耗时过长。`.gitignore` 现在排除了这些生成内容，但忽略规则只对以后未跟踪的文件生效。

提交前执行：

```powershell
git diff --check
git status --short
git diff --cached --stat
```

正常推送并核对远程提交：

```powershell
git add .
git commit -m "chore: clean generated files and update documentation"
git push origin HEAD:master
git ls-remote origin refs/heads/master
git status -sb
```

远程返回的 commit 必须与以下命令一致：

```powershell
git rev-parse HEAD
```

如果仍遇到 HTTP 408，先压缩本地对象再重试：

```powershell
git gc --prune=now
git repack -Ad
git config --global http.version HTTP/1.1
git push origin HEAD:master
```

`http.postBuffer` 不是清理大文件的替代方案；如果大文件已经存在于远程历史，需要在确认所有协作者后使用 `git filter-repo` 或 BFG 重写历史，不能仅依靠 `.gitignore`。

## 硬件注意事项

- LSM6DSV SPI 的 `WHO_AM_I` 应为 `0x70`；
- DAPLink SWD 速度最低使用 1 MHz；
- USB FS 使用 PA11/PA12，时钟和 VBUS 忽略配置应与官方 USB CDC 例程一致；
- CAN 通信必须经过正确的 CAN 收发器，回环模式不能证明外部总线、电阻和收发器连接全部正常。

## Bootloader 应用跳转验证

Bootloader 位于 `0x08000000`，应用位于 `0x08008000`。Bootloader 跳转前会关闭 SysTick、清除 NVIC 中断使能和挂起位，设置 `SCB->VTOR = 0x08008000`，恢复特权 Thread 模式并加载应用 MSP/PC。Bootloader 工程显式使用 `VECT_TAB_OFFSET=0x0000`，应用工程使用 `VECT_TAB_OFFSET=0x8000`。

验证应用启动时应看到：

```text
VTOR    = 0x08008000
PC      在 0x08008000 ~ 0x0803C000
vqf_live.magic = 0x56465131
whoami  = 0x70
seq     持续递增
fusion_hz 约等于 2000
```

验证前需要先烧录 Bootloader，再烧录应用：

```powershell
py -3 tools/dap/dap_flash_and_log.py --hex bootloader/build/at32f423_bootloader.hex --swd-frequency 1000000
py -3 tools/dap/dap_flash_and_log.py --hex build/lsm6dsv_spi_test.hex --swd-frequency 1000000
```

Bootloader 的应用有效性检查同时验证 SRAM 栈顶、Thumb 复位地址和应用地址范围，避免空 Flash 或损坏镜像被跳转。
