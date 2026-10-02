# Web 上位机独立输出：2026-09-30 验证

工作入口为 `upper/gyro-live/`，来源于用户提供的线上源码 ZIP。原 `tools/web_host/` 中的既有未提交修改没有被覆盖。前端资源版本 `20260930a`。

实现：六轴/九轴/九轴相对角 + 快速启动保存；UART、USB 独立协议与字段掩码；JustFloat 和自定义 AA55 均支持全部 9 个字段自由组合；可关闭遥测并保持控制；V2 设置兼容读取，V3 双槽保存；保留 CAN ID、标定和 Bootloader。

## 已通过

- Web 单元测试 41 项，覆盖 511 个非空组合、两种协议、分包、CRC/长度/非有限数、旧预设兼容。
- 原浏览器回归 14 步 + 新配置回归 11 步（模拟串口）。
- 原生 C 测试三组：协议、设置/Bootloader、USB 控制。
- Python 回归 40 项（上位机 33 + 固件升级 7）。
- Release 九轴、Release 六轴专用、Debug 九轴构建成功。旧有未使用标定变量警告保留。
- WCH-Link `349B8F06B96E` SWD 更新应用，整段回读校验 **70,268 字节**；SHA256 `228fcaebe17cee779469acdadb00b3e065f06fd9a9be53a5025cddf7e9052861`。Bootloader 和预留配置/标定区与烧录前逐字节一致。
- 实板 UART COM6 / USB COM16：56 组输出选择验证；关闭遥测后 PING/配置查询正常；六轴+快速启动开、九轴+快速启动关的重启后回读；两接口保存输出重启后恢复。
- 实板数据驱动网页 28 组选择，控制实际设备，验证独立 UART、关闭输出后的 PING、启动设置保存及恢复。使用仅供测试的本地 COM 字节桥接，**不包含浏览器原生串口授权弹窗/驱动的完整端到端测试**；生产网页仍直接使用 Web Serial，不依赖桥接。
- 新自选输出加速度为 m/s²，角速度为 °/s；旧兼容预设加速度仍保持历史 g 单位。实板全通道样本加速度模长约 9.84 m/s²。

最终实板状态：运行/保存均九轴，快速启动关闭；UART、USB 均保存 JustFloat + Yaw/Pitch/Roll；输出 1000 Hz。

## 文件

- 固件：`build/lsm6dsv_spi_test/release-9axis/lsm6dsv_spi_test.hex`（应用地址 0x08008000）。
- 最初完整 Flash 备份：`artifacts/web-host/board-before-host-upgrade.bin`。后续烧录有独立时间戳备份。
- 证据：`artifacts/web-host/swd-upgrade.json`、`hardware-output-test.json`、`hardware-web-test.json`、`gyro-real-board.png`。
- 已准备部署 ZIP：`artifacts/web-host/gyro-233688-20260930a.zip`，包含对应资源构建产物和可复现源码，不含 node_modules、凭据或设备备份。

SWD 工具退出时必须释放调试暂停、避免留下复位捕获，并保留固件 DWT 时钟；正常 pyOCD 断开会关闭 DEMCR/TRCENA，不适合直接用于该固件。`tools/dap/dap_host_upgrade.py` 使用固定探针和芯片 ID，限制应用范围，先备份、完整回读并核对保留区。

## 实板重复验证（会改配置并重启）

`python tools/serial/test_output_hardware.py --run-hardware`。需先释放 COM6 和 COM16。软件重启会经历 Bootloader 约三秒等待并重新枚举 USB。

网页实板测试需另开本地字节桥接 `python tools/serial/web_test_bridge.py --port COM16`，仅绑定 127.0.0.1，测试结束关闭桥接；运行 `upper/gyro-live/test/hardware.browser.check.mjs`。这是测试夹具，不用于正式部署，不应长期保持开放。

## 线上发布核对

2026-09-30 20:31 北京时间核对：从 `https://gyro.233688.xyz/` 实际获取资源，前端版本为 `20260930a`，三份资源均与本地 `src/public/` **逐字节一致**；`/healthz` 返回 200。线上更新已确认；没有取得 Cloudflare Worker 的新版本 ID，不推断发布调用方、ID 或精确部署时间。

| 资源 | SHA256 |
| --- | --- |
| index.html | d859103e904cc257c7f8dcccd3f504e18b7b40c3cfc12d32d5a85ee662770929 |
| app.js | dcc68fefe0f7a9282aef522a9f3480c7d4d6781ab6be6f55c2427d86f31bbb5c |
| style.css | 2a368ffde2bb7c2c290b4308e845b4547a04b9ae1d8ce74e2a190f4d6591dbd8 |

上方 SWD 校验 SHA256 是 Intel HEX 空洞补 FF 后的 Flash 镜像；BIN 的空洞补零，文件 SHA256 为 `073ba9a35cc447f0a319c602969eaca477a26d3262203d87aa5e03a8581e9b11`，两者不应直接比较。
