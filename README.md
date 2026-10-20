# AT32F423 双套 8 kHz ESKF

本分支默认提供 `20261020a`：基于 `20261017j`，关闭启动零偏写入温度模型和温度模型自动保存。目标为 AT32F423KCU7-4（150 MHz）+ LSM6DSV + IST8310。原有 J 构建 profile 和精确 BIN 保留。

- 六轴与九轴两套独立 ESKF，每个实际 IMU 样本执行 8 kHz 预测和完整 3×3 协方差传播。
- 独立 1 kHz 姿态生成，保留 J 原有加速度、地磁观测调度和 USB/UART/CAN 接口。
- VOFA+ 输出六轴 Yaw / Pitch / Roll；九轴滤波器仍独立运行。`VOFA_6AXIS=1` 与关闭九轴的 `SIX_AXIS=1` 含义不同，默认保留 `SIX_AXIS=0`。
- 保留 SRAM 向量表、SRAM DRDY/SPI DMA 链路、256 帧 IMU 环形缓冲和 UART RX DMA。
- 默认 ESKF 配置关闭温度补偿及温度学习，保留现有模型的存储格式与读取代码。
- 启动静置校准仍执行并将新零偏用于本次运行；不会因此修改温度表 RAM fallback 或自动申请保存。已有加速度、磁力计和温度模型记录保留。

`20261020a` 已在用户明确授权后仅更新 APP，并完成 4 秒启动校准和短测。原有 Flash 记录与保护位未改变，dirty/pending 均为 0。实测范围见 [短测报告](docs/TEMP_AUTOSAVE_OFF_20261020A_HARDWARE.md)；全功能生产验收尚未完成，ABI5/Observer 等后续诊断实验未纳入。

## 构建

需要 GNU Arm Embedded Toolchain 和 GNU Make。复现所附 BIN 使用 `arm-none-eabi-gcc 7.2.1 20170904 (release)`；其他版本可能产生不同镜像。Windows 可使用 PlatformIO 的 `toolchain-gccarmnoneeabi` 和 `mingw32-make`。

```sh
# 默认：20261020a，完整双 ESKF，启动新零偏只用于本次运行
make -j4 all

# Windows
mingw32-make -j4 all
```

产物：`build/lsm6dsv_eskf8k_ram_bias/release-9axis/lsm6dsv_eskf8k_ram_bias.{bin,hex,elf,map}`。

默认参数见 [make/eskf8k-ram-bias.mk](make/eskf8k-ram-bias.mk)，沿用 [J 参数](make/eskf8k-j.mk)，构建规则见 [make/firmware.mk](make/firmware.mk)。本分支没有调整 ESKF 数学、Q/R、静置门限或姿态调度。

```sh
# 精确复现历史 J，保留其原保存行为
make PROFILE=eskf8k-j -j4 all

# 保留原有 VQF/MEKF 路径，使用独立输出目录
make PROFILE=legacy -j4 all

# 使用主机 C 编译器检查新策略、legacy 默认与 updater 保护
python tools/check_startup_bias_policy.py --cc gcc
```

`PROFILE=legacy` 默认版本为 `20261012g`，不是 J 的 8 kHz 模式。旧版使用说明保留在 [docs/README_LEGACY.md](docs/README_LEGACY.md)，其中温度学习、控制命令和输出行为不能直接套用到 J。

## APP 镜像与存储范围

关闭自动保存版：[firmware/20261020a/lsm6dsv_eskf8k_ram_bias.bin](firmware/20261020a/lsm6dsv_eskf8k_ram_bias.bin)，171,780 B。

```text
SHA256 09962f1e265b7d67ae0c597f29bb3394e4a8446f73d18f43d3f70c2b7af7c951
```

精确 J APP：[firmware/20261017j/lsm6dsv_eskf8k_j.bin](firmware/20261017j/lsm6dsv_eskf8k_j.bin)。

```text
SHA256 a5a7a67ecc84044255573c10c8222f744310d268fd86d41b776b2f26b02aaa0c
```

| 区域 | 地址 |
| --- | --- |
| Bootloader | `0x08000000`～`0x08007FFF` |
| APP | `0x08008000`～`0x0803BFFF` |
| 设置、校准及模型保留区 | `0x0803C000`～`0x0803FFFF` |

所附 BIN 仅包含 APP，从 `0x08008000` 开始。此次已授权操作采用 SWD APP-only 更新，完整读回 BIN，Bootloader、设置/校准/温度模型保留区及保护位均验证未改变。

## 验证范围

历史 J 正常 USB 测试曾测得约 83.22% 有效 CPU 占用；这是既定负载下的历史数据，不代表本次新实测或任意 USB/UART/CAN 组合均低于 85%。

UART 严格物理零丢帧、全负载 CAN 组合、Flash 并发、动态精度、宽温及长期生产验收仍须分别验证。六轴 Yaw 没有绝对航向观测，静置漂移与输出保持应独立评估。J 的运行时 rest-bias setter 未完整转发到 ESKF 控制路径，不能据命令 ACK 判断内部开关已改变；本分支保留其默认行为。

新版本的策略、回归与上板审查见 [docs/TEMP_AUTOSAVE_OFF_20261020A.md](docs/TEMP_AUTOSAVE_OFF_20261020A.md) 和 [firmware/20261020a/manifest.json](firmware/20261020a/manifest.json)。[J 基线说明](docs/ESKF8K_J_BASELINE.md) 及 [J manifest](firmware/20261017j/manifest.json) 对应父提交 `2f9ca811` 的冻结源文件；当前兼容 profile 仍可生成同一 J BIN。

新版上位机仍在 [upper/gyroa](upper/gyroa/README.md)，第三方许可证保留在 `LICENSES/`、SDK 的 `LICENSE` 与上位机的 notices 中。原始传感器录制、私有校准、温度模型及 Flash 备份不随本分支发布。
