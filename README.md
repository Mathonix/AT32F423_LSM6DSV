# AT32F423KCU7-4 + LSM6DSV SPI1 姿态测试

## 已验证状态（2026-09-12，重新上电后）

- GPIO 慢速 bit-bang：`WHO_AM_I = 0x70`，连续验证正常。
- 参考文章的关键配置已经实测：全双工主机、PA4 GPIO 软件 CS、PA6 上拉、
  8-bit/MSB、Mode 0、CS 前后延时。
- 硬件 SPI 目前仅在异常低速下稳定：

| 模式 | 稳定通过的 SCK | 结果 |
|---|---:|---:|
| Mode 0 | 488.3 / 976.6 / 1953.1 Hz | 各 100/100 |
| Mode 3 | 3906.2 Hz | 100/100 |

Mode 0 在 3.906 kHz 已不稳定，7.812 kHz～250 kHz 均为 0/100；
150 MHz 系统时钟下从 9.155 kHz 起也没有稳定配置。失败时 MISO 基本返回
`0xFF`，但每组后的 250 us GPIO 参考读取仍为 10/10。MCU 自观察同时确认
PA5 有完整 SCK、PA7 发出的地址字节确实是 `0x8F`。

因此，参考文章中的 SPI 软件配置不是当前主要阻塞点。当前现象更像 SCK/MOSI
网络存在严重 RC 负载、钳位、错误电容/阻值、焊接高阻或漏电。LSM6DSV 的
2 kHz 六轴读取至少需要约 208 kbit/s 的纯数据时钟，考虑片选和软件开销应让
SPI SCK 稳定达到至少 500～600 kHz，推荐 1 MHz；当前速度无法运行 2 kHz
六轴采样和 MCU BasicVQF。

安全扫描命令：

```powershell
# 文章采用的 Mode 0
python tools/dap_spi_freq_sweep.py --mode 0 --timeout 180

# 对照 Mode 3
python tools/dap_spi_freq_sweep.py --mode 3 --timeout 180
```

最新结果：

```text
build/spi_freq_sweep_results_20260912_004521.csv  # Mode 3
build/spi_freq_sweep_results_20260912_004638.csv  # Mode 0
```

> 在硬件问题修复前，不应把当前极低速窗口当成最终驱动配置，也不应宣称已达到
> 2 kHz 主机采样率。

## 编译

```powershell
make -j4
```

生成：

```text
build/lsm6dsv_spi_test.elf
build/lsm6dsv_spi_test.hex
build/lsm6dsv_spi_test.bin
```

## 烧录并严格读取 10 次姿态

```powershell
C:\Users\Mathonix\AppData\Local\Programs\Python\Python310\python.exe `
  tools\dap_vqf_read.py --flash --count 10 --period 1
```

脚本会等待第一帧有效姿态，再按 1 秒周期严格读取 10 次，并列出：

```text
Roll/Pitch/Yaw、陀螺仪 Gx/Gy/Gz、加速度 Ax/Ay/Az
```

成功条件：

```text
WHO=0x70
init_err=0
seq 持续增加
RESULT: 10-second attitude capture OK
```

## 关键文件

- `src/at32f423_clock.c`：HICK + PLL 150 MHz，LDO 1.3 V，Flash wait 4
- `src/lsm6dsv.c`：SPI1 Mode 0、16-bit、SCK ≈ 4.7 MHz，HA01 2000 Hz
- `src/main.c`：六轴读取、BasicVQF、`vqf_live` 快照
- `tools/dap_vqf_read.py`：DAP 烧录并严格采集指定次数的姿态
- `tools/dap_whoami.py`：DAP GPIO 独立检查 `WHO_AM_I`
