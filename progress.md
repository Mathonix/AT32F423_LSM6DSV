# 进度交接

更新于 2026-10-04。板上最新为 `20261003e`，新版 BL 与 APP 已配套烧录，T=0/2/60 秒、运动到期历史回退与 USB 升级已验证，见文末补充。新版上位机在 `upper/gyroa`，目标 `gyroa.233688.xyz`，本地更新包未部署。以下 `20261002c` 的发布与板上数据是历史快照。同一天第一版是 `a`，当天再出一版用下一个字母。不要另起 `v1.0.0`。

这份只记进度。命令细节见 `docs/host-agent-*.md`。本次用户已授权更新其提供的 gyroa 网页源码；未请求线上部署。

## 已完成

- 提交 `156968d` 已在 `origin/master`：https://github.com/Mathonix/AT32F423_LSM6DSV.git
- Release：https://github.com/Mathonix/AT32F423_LSM6DSV/releases/tag/20261002c
- 附件名 `20261002c.bin`。说明只有一句：四档滤波（含零角速保持）、运动零偏、静置 VQF 初始化、版本查询 `0x23`、启动零偏历史 `0x33`。
- 九轴应用已用 4 MHz SWD 烧到板上。USB 回读 `0x23` 得到 `20261002c`。`0x33` 已能翻页读出历史。

## 20261002c 历史板上快照

应用区 `0x08008000`–`0x0801F334`，校验 95028 字节。SWD 回读 SHA256 `9b613764bfff58c6a87f34baece5b5e095b0674c16825da275167974e75a19b7`。引导区 Reset 仍是 `0x08003301`。烧录前备份是 `artifacts/web-host/board-before-host-upgrade-20261002-203532.bin`。

正在使用的设置与备份最新记录一致（`0x0803D800`，序号 155）：6 轴、滤波档 0、±1000 °/s、快速启动开、开机采集 2000 ms、输出 1000 Hz。不要改回以前记得的档 3 或 ±2000。

启动零偏历史有 36 条，记录版本 3，序号 129，corrupt 为 0。温度大约 32.8 °C 到 38.5 °C。

发布文件 `20261002c.bin` 的 SHA256 是 `8724340ab1ab18801636ffc9989a1470c56bf88ea74641cc7b1646ecb53d5bd9`。它和 SWD 镜像差 4 个对齐字节：文件里是 `00`，板上 `0x0801E3F4` 是擦除后的 `FF`。网页和串口升级用这个 bin。

## 烧录

在 `tools\dap` 执行 `python -u dap_host_upgrade.py --flash`。脚本已是 4 MHz，只擦应用区 `0x08008000`–`0x0803C000`。默认镜像是 `build/lsm6dsv_spi_test/release-9axis/lsm6dsv_spi_test.hex`。不要整片擦除，不要改用 `firmware_upgrade_swd.py`。

USB 是 VID:PID `2E3C:F401`，序列号 `22EC987C8068`。打开串口后先丢弃大约 16 秒再查询。不要发 `0x16`，那会进入 Bootloader。不要用 `0x1D` 把输出静音。

本机测试：`python tests/run_native.py`。九轴构建是 `mingw32-make DEBUG_BUILD=0 SIX_AXIS=0`，六轴是 `SIX_AXIS=1`。工具链在 `C:\Users\Mathonix\.platformio\packages\toolchain-gccarmnoneeabi\bin`。

## 协议里新加的能力

| 作用 | 命令 | 回复 |
| --- | --- | --- |
| 应用版本 | 空 `0x23` | `0x32`，16 字节，文本 `20261002c` |
| 启动零偏历史 | 空或偏移 `0x33` | `0x34`，60 字节，每帧最多 3 条，0 是最旧 |
| 静置初始化 | `0x29`–`0x2D` | `0x0D`、`0x0E` |
| 零角速保持 | `0x2E`、`0x2F`、`0x31` | `0x0F` |
| 运动零偏 | `0x30` | `0x09`，48 字节 |

`0x09` 不是版本，也不是历史列表。`0x12` 和 `0x1B` 仍回执行失败。旧固件不认识 `0x23` 和 `0x33`，回 ACK `status = 0x01`。

## 还没做

- 线上页面 https://gyro.233688.xyz/ 不会发 `0x23`，版本仍显示 `--`。本地 `upper/gyro-live` 也不要为了追页面去改，除非另说。
- 历史零偏的数值单位还没改。Flash 和 `0x34` 里存的是 rad/s，字段名却是 `bias_dps`。上位机按 °/s 打印时，大约 `+0.28 / +0.05 / −0.37 °/s` 会显示成 `+0.005 / +0.001 / −0.006`。36 条彼此接近，是因为同一只陀螺在相近温度下重复静止采集。
- 固件加密只讨论过，没有实现。若以后要做：先给升级包签名，再由 Bootloader 按包解密，最后才考虑 Flash 读保护。已公开的 `20261002c.bin` 是明文。

## 交接文档

- `docs/host-agent-firmware-version.md`
- `docs/host-agent-bias-history-list.md`
- `docs/host-agent-bias-history-50.md`
- `docs/host-agent-bias-collect-fallback.md`
- `docs/host-agent-motion-bias.md`
- `docs/host-agent-vqf-static-init.md`
- `docs/host-agent-rest-trial.md`
- `docs/host-agent-zaru.md`
- `docs/host-agent-zaru-limits.md`
- `docs/host-agent-zaru-restore.md`
- `docs/host-agent-zaru-threshold.md`

## 2026-10-03 型号查询补充（已烧录及实测）

新增空命令 `0x35`，成功仅回 `0x36`，4 字节 ASCII `AT32`，没有末尾 NUL；非空载荷回参数错误 ACK。USB CDC、UART、WebUSB 均按原来源回复，保持请求 SEQ。版本已从工作区 `20261003c` 递增为 `20261003d`。

全部原生回归和九轴构建通过。独立输出 `build/device_model_query/release-9axis/device_model_query.bin`，96476 字节，SHA256 `50ee691a3871ed819bb0818a9e4be5f3ffac80f818ff41deb08ec68536557f97`。随后按用户指令通过 MicroLink `04CF952C7A94199D`、4 MHz SWD 烧录应用区 `0x08008000`–`0x0801F8DC`；完整回读校验通过，Bootloader 和配置/校准区逐字节保持一致。烧录前全 Flash 备份为 `artifacts/web-host/board-before-host-upgrade-20261003-222200.bin`。

SWD 回读 SHA256 `2d1ccaaad494a33cc6da041b50fb07696fcda05140cd8935b0cf3b7c0dadab6b`。HEX/SWD 与 bin 仅在 `0x0801E89C`–`0x0801E89F` 四个对齐字节不同，分别为 `FF` / `00`。

2026-10-03 22:23（北京时间）通过板载 USB COM29、2000000 波特率实测：`0x35 → 0x36` 返回 `AT32`，`0x23 → 0x32` 返回 `20261003d`，非法非空型号查询回参数错误 ACK，然后正常查询仍成功。MCU 已重启运行。证据是 `artifacts/diagnostics/model-query-flash-20261003d.json` 和 `artifacts/diagnostics/model-query-hardware-20261003d.json`。本次没有修改网页或部署。接入说明见 `docs/host-agent-device-model.md`。


## 2026-10-03 共享 BL 等待与上电零偏窗口（20261003e，待烧录）

按用户确认修改：T 持久化可设 0～60 秒（默认 2 秒），BL 等待与零偏采集在同一个 T 窗口并行；到期采集不合格立即用历史，不追加 APP 采集或延长窗口。T=0 检查 APP 向量后直接启动并采用历史。显式 0x16 升级和 APP 无效仍留在恢复入口。

BL 复用 IMU、加速度校准和启动校验，64 位 DWT 扩展覆盖 60 秒；BL/APP 顶部 SRAM 256 字节保留，64 字节 CRC 交接结果消费一次。历史优先 ±5°C 内均值，否则最近温度记录，再否则编译默认；没有添加温度补偿。新配置回复版本 4、载荷 28 字节，T=0 允许保存，fast 标志由 T==0 派生。上位机 v4 显示 T=0～60 输入，旧固件 UI 保留，量程/频率功能仍可用。

15 项原生回归、62 项网页单元测试、3 组模拟浏览器测试（共享窗口、旧启动、量程/频率）通过；BL、六轴/九轴 release 和网页构建成功。BL text=21688、data=616，处于 32KB 区内。九轴 APP 位于 build/shared_boot_startup/release-9axis/shared_boot_startup.{hex,bin}；六轴在 release-6axis；BL 在 bootloader/build/at32f423_bootloader.{hex,bin}；网页发布目录 upper/gyro-live/dist/public。

必须配套更新 BL 和 APP；当前网页/串口升级仅能写 APP，BL 首次更新需 SWD，保留配置/校准区，不整片擦除。**本轮未烧录、未部署；实板最后已验证仍是 20261003d。** 实板上电时间与并行采集通过率待验证。详情 docs/host-agent-shared-startup.md。


## 2026-10-04 共享启动配套烧录与实板测试（已完成）

用户授权烧录测试后，MicroLink 04CF952C7A94199D、4MHz SWD 安装新版 BL 与 20261003e APP。完整备份 artifacts/diagnostics/shared-startup-before-20261004-005857.bin；APP 和 BL 完整分区回读校验，复位前配置/校准区不变。USB COM29 实测型号 AT32、版本 20261003e。随后完成同镜像 USB 升级（95636 字节），CRC 与 SWD 全镜像一致，APP SHA256 6a003b4697e0c03fe71b6c0b2db203518b5ed4b78f65f147b117f17f6387d8d8。

实板：T=0 历史零偏、0 样本、无新增历史；T=2 秒采集 4001 样本；T=60 秒采集 120073 样本；均在配置 T 到期且 APP 不再追加采样。APP 融合就绪（主机发重启至观察）约 0.456 / 2.535 / 60.569 秒，包含额外硬件初始化开销。60 秒采集中连续三次 USB BL HELLO 正常回复。T=0 显式 0x16 仍保持维护 3.5 秒，能够完整 USB 升级并 BOOT，使用历史。

用户转动板子进行 6 秒回退测试：14 次不合格（块内噪声），到 6000 ms 结束，使用历史，没有追加历史记录。最后放稳重启，2 秒采集 4000 样本，已运行。最终恢复原有六轴模式 0、T=2000 ms、±2000 dps、2000 Hz；输出/CAN/滤波/ZARU 设置不变，旧 fast 标志规范为 0。原有 36 条历史保留，成功启动新增 7 条至 43 条，sequence136。其他校准区未修改；测试期间设置记录序号递增到167。

最终证据 artifacts/diagnostics/shared-startup-final-rest-20261004.json；细节、每组测试证据和安装方式见 docs/host-agent-shared-startup.md。新增复用工具 tools/dap/flash_shared_startup.py 和 tools/serial/test_shared_startup_hardware.py。本轮没有完全断电再上电测试或 UART 实板升级；没有部署线上 gyro.233688.xyz，网页需配套 v4 支持。


## 2026-10-04 gyroa 上位机更新（本地完成，未部署）

用户提供 `E:/Downlload/gyroa-src-20261003.zip`，并确认域名 `gyroa.233688.xyz`。在独立 `upper/gyroa` 合并共享启动配置 v4，没有覆盖旧 `upper/gyro-live`。保留该包的新版五页布局、真实 PCB 模型、移动端、波形、输出/CAN、四档滤波、阈值、运动零偏、历史及 VQF 初始化。

v4 显示启动窗口 0～60 秒（默认 2 秒），隐藏旧快速启动复选框；fast 按 T=0 派生，保存 ACK+配置回读确认，非法输入在进入设置模式前提示，失败保留草稿。高级配置表也用启动窗口标签，隐藏旧 fast 行。旧 v1/v2/v3 兼容；v4 量程/保存频率与重启自动重连可用。连接及升级后重连查询 0x35，升级页显示 AT32；旧固件未知命令仅显示 --。

可编辑源码 `upper/gyroa/src/public`，原包预编译 PCB bundle/GLB 及第三方声明保留；构建脚本检查 JS 并复制到 dist/public，生成静态 SHA256 manifest 和 Worker bundle。域名、路由、Worker 服务逻辑沿用原包。资源缓存标签 20261004startup1。

83 项单元测试通过；模拟浏览器：共享窗口（0/2/60、非法输入、失败草稿、量程/频率保存、T=0/60 快捷融合与同端口重连、旧配置兼容、型号）、版本查询 13 步、快捷设置 11 步、完整 app4 25 步（含模拟升级）通过。桌面 1440/1024 和手机 390 宽布局已检查。测试没有操作实板；真实 60 秒计时属于前一节固件测试。

发布源码包 `E:/Downlload/gyroa-src-20261004-startup1.zip`，预构建部署包 `E:/Downlload/gyroa-deploy-20261004-startup1.zip`，同份归档在 artifacts/web-host/gyroa-20261004-startup1。包内无 node_modules 或账户凭证。构建说明见 upper/gyroa/README.md。本轮没有 Cloudflare 部署或外部发送。


## 2026-10-04 最新 Release 已发布（20261003e）

用户要求推送当前项目并附上上位机后，代码已提交 `8db84a8f47175db3aff8903ef9b455d9fff87426` 并推送 origin/master。随后按用户要求发布正式 Release `20261003e` 并标为 Latest：https://github.com/Mathonix/AT32F423_LSM6DSV/releases/tag/20261003e 。保留旧 20261002c Release 与标签。

8 个附件：20261003e.bin、20261003e.hex、20261003e-bootloader.bin、20261003e-bootloader.hex、gyroa-src-20261004-startup1.zip、gyroa-deploy-20261004-startup1.zip、SHA256SUMS.txt、release-manifest.json。全部远程附件 digest 与本地 SHA256、长度一致，已检查发布状态非草稿非预发布，Latest 指向 20261003e。

APP bin 95636 字节，SHA256 6a003b4697e0c03fe71b6c0b2db203518b5ed4b78f65f147b117f17f6387d8d8，与实板 USB 升级最终回读一致。Bootloader bin 22304 字节，SHA256 f19027ed665a9126767a3fc515d4f0bbd649792e9de97cec20dee0463b4c3872。发布 APP HEX 从已验证 bin 生成，补齐构建 HEX 未映射的 0x0801E554～0x0801E557 四个零填充对齐字节；所有有效映射保持一致，完整 HEX/BIN 逐字节一致。BL HEX 沿用构建文件。

上位机包已重新生成以包含推送时的完整文件；两个 ZIP 的当前校验以 Release SHA256SUMS.txt 为准。本轮没有重新烧录或 Cloudflare 部署。发布前复验 15 项原生回归、83 项网页单元测试及 8 项 WebUSB 工具测试通过。
