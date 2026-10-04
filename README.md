# AT32F423 AHRS

AT32F423KCU7-4 + LSM6DSV + IST8310 姿态固件，当前应用版本 `20261004d`。仓库保留固件、配套 Bootloader 与新版 gyroa 上位机的构建必需文件和许可证。

## 构建固件

需要 GNU Arm Embedded Toolchain、GNU Make，工具链加入 PATH。Windows 可使用 `mingw32-make`；主 Makefile 也支持本机 PlatformIO 工具链路径。

```sh
# 常用构建，具备九轴能力，可通过保存的配置切换六轴/九轴
make -B DEBUG_BUILD=0 SIX_AXIS=0 all
# 强制六轴构建
make -B DEBUG_BUILD=0 SIX_AXIS=1 all
# 配套 Bootloader
make -C bootloader -B all
```

Windows 将命令中的 `make` 换成 `mingw32-make`。APP 输出在 `build/lsm6dsv_spi_test/release-9axis/` 或 `release-6axis/`，BL 输出在 `bootloader/build/`。生成 `.bin`、`.hex` 和 `.elf`。

| 区域 | 地址 |
| --- | --- |
| Bootloader | `0x08000000`～`0x08007FFF` |
| APP | `0x08008000`～`0x0803BFFF` |
| 配置 / 校准 | `0x0803C000`～`0x0803FFFF` |

首次安装共享启动窗口须用 SWD 配套更新 BL 和 APP，保留配置 / 校准区。网页或串口升级仅更新 APP。已发布 BIN 在 [20261004d Release](https://github.com/Mathonix/AT32F423_LSM6DSV/releases/tag/20261004d)；配套 BL 与 20261003e 相同。

## 上位机

最新源码为 [upper/gyroa](upper/gyroa/README.md)，目标域名 `gyroa.233688.xyz`。需要 Node.js ≥22.12.0，在该目录执行：

```sh
npm ci
npm run build
npm run dev
```

可编辑页面在 `src/public/`，生成目录为 `dist/public/`，本地默认 `http://127.0.0.1:8833/`。真实 PCB 模型和页面所需脚本已包含，构建无需 STEP 原文件或转换工具。发布网站需自行登录目标 Cloudflare 账户后执行 `npm run deploy`。

## 设备使用

- 2 kHz VQF，六轴 / 九轴 / 九轴相对航向；四档滤波、运动零偏、静置零偏估计、静置 VQF 初始化与最多 50 条启动零偏历史。标定基线独立保存，运行时估计基线之上的修正。
- 启动窗口 T=0～60 秒，默认 2 秒；BL 等待和零偏采集并行，到期采集不合格使用历史，不追加 APP 等待。T=0 使用历史立即启动，仍允许主动进入升级模式。
- USB CDC 与 UART：2000000 baud；UART TX=PA0、RX=PA1，共地。USB D+=PA12、D-=PA11。
- CAN2：RX=PA2、TX=PA3，需外接 CAN 收发器；可设置节点、输出字段和输出频率。
- 空命令 `0x23` 查询版本（回复 `0x32`），`0x35` 查询型号（回复 `0x36`，ASCII `AT32`），`0x37` 查询实际启动零偏来源、初值及当前总补偿（回复 `0x38`）。历史回复 `0x34` v2 单位为 dps；旧 v1 为 rad/s。命令使用现有 AA55 二进制帧，不是直接发送 ASCII 命令编号。
- 任一 USB/UART 输入 ASCII `vofa`，将两路输出切换为 JustFloat；无需空格或换行，不写入 Flash。

第三方许可证保留在 `LICENSES/`、SDK 的 `LICENSE` 和上位机 `THIRD-PARTY-NOTICES.txt`。构建产物、旧工程、采集数据及本机工具不纳入当前仓库目录。
