# 陀螺仪量程、串口频率持久化与 CAN 频率验证

日期：2026-10-01。上位机工程：`upper/gyro-live`。页面资源版本：`20261001f`。

## 最终行为

- 启动设置新增 ±125/250/500/1000/2000/4000 dps 陀螺仪量程。保存后重启生效，可勾选立即重启；初始化、原始数据换算和姿态融合均采用所选量程。默认 ±1000 dps。
- USB/UART 共用输出频率。应用时先保存到 Flash，再更新运行频率；重启自动恢复。页面回读当前及已保存频率后才显示保存成功，保存失败保留编辑值。
- CAN 增加 Hz 输入，和原有毫秒周期双向联动，显示实际频率。周期取整到 1–10000 ms，因此输入 60 Hz 时实际为 58.824 Hz（17 ms）。主动输出频率作用于每个勾选的消息组；请求模式仍按请求返回。
- 配置协议 v3 保留 v2 前缀，追加当前量程、已保存量程和已保存串口频率。旧固件可继续使用，页面明确提示旧固件频率只在本次运行生效，并禁用量程修改。
- Flash 记录升级为 v6，继续读取 v2–v5 配置并保留原启动、输出通道、CAN 参数；旧记录量程默认 1000 dps，串口频率默认原固件 1000 Hz。

## 自动检查

- 原生固件测试 6 组通过，覆盖 Flash 迁移、双槽掉电回退、非法参数、协议及 CAN 调度。
- 六轴和九轴固件构建通过。
- 上位机构建、47 项逻辑测试通过。
- 原页面回归、输出配置、启动设置、CAN 16 种组合、CAN 草稿与布局、重启重连回归通过。
- 新增 8 项浏览器场景通过：频率保存、保存期间继续编辑、刷新和保存失败保留草稿、六档量程、量程重启及频率恢复、量程保存失败、CAN Hz 换算及边界、旧固件兼容。1440/820/390/360 屏宽无横向溢出。

## 实板检查与恢复

通过 WCH-Link 更新实板九轴应用，应用起始地址 `0x08008000`，完整回读比对 73760 字节通过。Bootloader、配置和校准区域保持不变，更新前备份整片 262144 字节 Flash。

应用 SHA256：`c48f6cf8d24e8a43a62e058e82538de330dcaedd7684134ff5238d77aef7ac7c`。

COM6 UART 实板逐一保存并重启六档量程，当前与已保存量程回读均匹配；WHO_AM_I 为 `0x70`，传感器初始化无错误，驱动完成量程寄存器读回校验。500 Hz 串口频率在六次重启后均保留。非法 750 dps 和 300 Hz 被拒绝。CAN 50 Hz（20 ms）保存后重启，当前及已保存周期均恢复。

测试结束已恢复原参数：九轴、快速启动开启、初始化时长 2000 ms、1000 dps、串口 1000 Hz；UART 自定义格式及原掩码、USB JustFloat 及原掩码不变；CAN 节点 1、主控 ID 0x6FF、主动模式、周期 1 ms、1 Mbps、原输出掩码不变。

本轮 CAN 实板检查验证配置保存和重启回读；未用外部 CAN 接收器测量总线实际发送频率。USB CDC 未连接，真实串口检查使用 UART；USB/UART 共用同一频率持久化路径。

## 发布证据

- 发布包：`artifacts/web-host/gyro-233688-20261001f.zip`，3478274 字节，SHA256 `8f45df098c74f226a449a72d90de921a7a83ad6a2a5b2859b8b5a4ade99646cb`。
- 回滚点：20261001e，Cloudflare 版本 `f175e51d-2ba0-42b9-9acc-aea13fc38cea`。
- 实板烧录回读：`artifacts/web-host/swd-range-rate-upgrade-20261001.json`。
- 实板配置验证：`artifacts/web-host/range-rate-hardware-20261001.json`。
- 更新前完整备份：`artifacts/web-host/board-before-host-upgrade-20261001-100808.bin`。
- 本地浏览器验证：`artifacts/web-host/range-rate-browser-local-20261001f.json`。
- 发布资源清单：`artifacts/web-host/deployment-manifest-20261001f.json`。

20261001f 已发布至 https://gyro.233688.xyz/ 。独立核对时间为 2026-10-01 10:24:56（北京时间）：7 份静态资源的字节数和 SHA256 全部匹配 manifest，`/healthz` 返回 HTTP 200、ok:true。

线上新增 8 项浏览器场景全部通过，使用模拟串口；真实板卡另按上文使用 COM6 验证。新页面加载真实 PCB GLB，210 个元件网格，安装矩阵仍为绕 Z 轴 +90°。

- 线上资源核验：`artifacts/web-host/online-deployment-verification-20261001f.json`。
- 线上浏览器回归：`artifacts/web-host/range-rate-browser-online-20261001f.json`。
- 未打开串口的线上模型状态：`artifacts/web-host/pcb-online-capture-20261001f.json`。
- 线上截图：`artifacts/web-host/host-20261001f-online.png`、`artifacts/web-host/settings-20261001f-online.png`、`artifacts/web-host/frequency-settings-20261001f-online.png`（均未打开串口）。
