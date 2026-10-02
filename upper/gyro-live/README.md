# gyro-233688（gyro.233688.xyz · AT32 AHRS Web 上位机，可编辑工程）

## 2026-10-01：网页 USB/UART 固件升级（20261001fw1）

设备设置中展开“USB / UART 固件升级”，连接应用串口后选择本板应用 `.bin`，点击“开始升级”。网页本地检查大小、应用向量、CRC32 和 SHA256，发送 `0x16` 进入配套 Bootloader v1，核对分区、逐块上传和整镜像校验；校验通过才启动应用，应用 PING 回复后确认完成并自动重连原端口。文件不传到服务器。COM6 UART 网页实板测试通过，独立 Flash 回读确认镜像精确匹配及 Boot、设置、加速度校准区域保留。

只接受链接到 `0x08008000`、最大208 KiB、栈顶不超过 `0x2000BFF0` 的应用镜像，不接受 Bootloader、整片备份或 `.atfw`。首次 Bootloader 安装仍需 SWD，本板已安装。升级期间保持供电和页面打开，并关闭其他占用端口的软件/页面；v1无断电自动回滚。

失败或停止后可勾选“设备已在升级模式（恢复上传）”从头重传；USB端口变化时手动选择恢复端口，重枚举不按VID/PID猜选其他设备。`npm run test:firmware-browser` 使用模拟设备，不写真实板卡。见 [网页升级验证](../../docs/web-firmware-upgrade-validation.md)。

## 2026-10-01：六面校准与线上同步（20261001acc3）

已发布至 https://gyro.233688.xyz/。新增自动六面加速度计校准的开始/取消、六面进度、XYZ 原始 g 值和放置参考、运动与失败提示，以及终止后的非实时读数标识；拟合失败保留旧校准。合并线上 `20261001f-c1` 紧凑排版。浏览器测试使用模拟串口，六面校准实板验收仍待完成。此次只同步网页，USB/UART 固件升级目前通过独立 Bootloader 工具执行。发布及独立核验见 [线上同步记录](../../docs/web-host-cloudflare-sync-validation.md)。

## 2026-10-01：陀螺仪量程与输出频率（20261001f）

启动设置新增 LSM6DSV ±125/250/500/1000/2000/4000 dps 量程。保存后在重启时配置 CTRL6，原始采样、零偏采样和输出诊断统一使用所选量程对应灵敏度。可勾选立即重启，并自动重连原串口。

USB/UART共用输出频率：设置成功后立即应用并保存，重启恢复保存值。页面同时显示运行频率和断电恢复频率，保存失败保留输入，成功须匹配运行及保存回读。旧固件继续支持临时频率，但明确提示升级后才能保留。

CAN参数新增每类输出频率Hz，与原毫秒间隔双向联动。范围0.1～1000Hz，按整毫秒间隔取最接近值，并显示实际频率；各勾选报文类型分别按此频率发送。请求/应答模式仍按请求发送。CAN断电保存复用现有配置机制。

设备配置协议v3追加当前/保存量程和保存频率，兼容v1/v2。Flash设置v6兼容v2～v5；旧记录迁移时默认±1000dps和固件默认输出频率，保留启动、零偏时长、独立串口和CAN配置。新增回归 `npm run test:range-rate-browser`。

## 2026-10-01：重启自动重连与模型轴向（20261001e）

保存并立即重启时保留当前已授权的 SerialPort 对象、实际打开波特率及解析设置，关闭旧读写流后自动重开同一端口，不再次弹出选口窗口。USB 重枚举或暂时占用时重试，设备启动后重新查询配置；最多等待90秒，可点击“断开”随时取消。物理重启、拔插导致端口丢失时也支持此流程，覆盖其他配置修改后的重启。保存失败不主动断开重连，不以相同 VID/PID 猜选其他设备。网页刷新不保留 SerialPort 对象，首次连接仍需手动选择。

按用户指出的 Pitch/Roll 对应问题调整真实 PCB 安装方向：CAD +X 对应传感器 +Y，CAD +Y 对应传感器 -X，即 CAD 绕 Z 固定旋转 +90°；角度读数和固件坐标定义不改。欧拉角和四元数都先求同一传感器旋转，再应用该固定安装方向。

`npm run test:reconnect-browser` 覆盖 UART 不掉口、USB 延迟返回、ACK 丢失、启动延迟、保存失败、关闭旧流、打开过程中手动取消、多个相同 VID/PID 端口以及保存后物理重启。模型回归读取实际 CAD 世界变换，核验 Pitch/Roll 对应方向。

## 2026-10-01：真实 PCB 模型（20261001d）

姿态预览使用用户提供的 `3D_PCB1_2026-10-01.step` 转换出的真实板卡，保留板体、元件、连接器及源配色。支持欧拉角 ZYX 和四元数驱动、拖动查看、滚轮/双指缩放及复位视角；查看角度不会修改设备姿态。加载失败或 WebGL 不可用时保留简化模型及串口功能。

`src/public/models/pcb.glb` 含 210 个源网格、142832 个三角面，按五种源材质合并为五次模型绘制；以 PCB 中心为旋转原点，保留 STEP X/Y/Z。原文件尺寸、哈希及转换参数记录在 `models/pcb-source.json`。模型轴方向尚未通过实物转动核对，若 PCB 安装方向与传感器机体系不同，需另行设置固定对齐旋转。

模型离线转换：`node scripts/convert-pcb.mjs <STEP路径>`。常规构建只打包 `src/pcb-view.js` 并复制已生成 GLB，无需 CAD 软件，不在浏览器解析 25 MB STEP。Three.js 与模型均随网站发布，无外部 CDN 请求。模型回归：`npm run test:model-browser`。

## 2026-10-01：快速启动与初始化零偏时长（20261001c）

取消快速启动时显示初始化零偏时长，默认 2 秒，可填 0.1～60 秒。勾选快速启动时隐藏此项。启动表单的勾选、模式和时长不会被刷新回读覆盖；保存失败保留修改，匹配保存配置回读后确认成功，支持撤销修改。

配套固件扩展启动配置协议 v2，保存初始化时长，普通启动实际执行完整静止采样。Flash 设置 v2/v3/v4 迁移到 v5 时保留原配置，旧记录初始化时长为 2 秒。旧固件会显示升级提示，不将未支持的采样操作报告为成功。新增 `npm run test:startup-browser` 验证相关交互。

## 2026-10-01：紧凑排版与 CAN 编辑保护（20261001b）

保留 Grok Bot 最新发布的 `20260930b-ui3` 紧凑布局和配色，合并本次 CAN 编辑保护。通道采用短标签及中文提示，避免窄面板文字裁切；支持桌面及 360 px 起的手机布局。另一套分区布局保存在 `artifacts/web-host/gyro-233688-20261001a.zip`，未发布。

CAN 回读更新设备状态时保留未应用的表单修改；“撤销未应用修改”恢复最近读取的设备参数。CAN 应用成功需要当前参数及（勾选保存时的）保存参数回读匹配；只收到 ACK 时仍等待读回，3 秒超时后释放表单并保留草稿。

新增 `npm run test:can-draft-browser`，覆盖延迟回读、刷新期间编辑、保存失败、回读不匹配及 1440/820/390/360 px 布局。PCAN 实板检查见仓库 `tools/serial/test_pcan_hardware.py` 和 `docs/web-host-layout-pcan-validation.md`，已验证 1 Mbps 总线收发并恢复原运行参数。

## 当前开发入口（2026-09-30）

本目录从用户提供的 `E:\Downlload\gyro-233688-latest-src.zip` 恢复，作为当前 Web 上位机开发入口。
恢复时 `src/public/index.html`、`app.js`、`style.css` 与 `https://gyro.233688.xyz/` 对应资源逐字节一致。
当前版本包含严格的 AA55 / JustFloat 3、6 通道解析、3D 姿态显示、输出频率校验、配置待重启提示及串口断线恢复。
`backup/` 保存历史版本，`dist/` 是构建产物；后续修改 `src/`。
仓库中的 `tools/web_host/` 是较旧版本，并包含原有未提交开发，继续本项目请使用本目录。

### Windows 本地开发

需要 Node.js 22.12 或更高版本，以及 Chrome 或 Edge。Wrangler 已固定为本地开发依赖，无需全局安装。

```powershell
Set-Location 'E:\Desktop\1_Program\AT32F423_LSM6DSV_SPI_Test\upper\gyro-live'
npm ci
npm test
npm run build
npm run dev
```

打开 `http://127.0.0.1:8798`。在另一个 PowerShell 终端进入同一目录后，可运行模拟串口的浏览器回归：

```powershell
npm run test:browser
```

浏览器测试自动查找 Chrome/Edge；自定义安装位置可设置 `$env:CHROME` 为可执行文件的完整路径。
测试默认连接本地预览，并模拟设备，不打开真实串口。`npm run build` 只进行本地 dry-run 打包。
`npm run deploy` 会发布到现有 Worker `gyro-233688`，需在准备发布时单独执行。
构建脚本的可选 `WRANGLER` 环境变量现为 Wrangler JS 入口的完整路径。

## 2026-09-30：独立输出与启动配置

前端缓存版本为 `20260930a`。需要本仓库配套的新固件，推荐编译 `SIX_AXIS=0`；此镜像支持运行时选择六轴、九轴和九轴相对角。`SIX_AXIS=1` 是六轴专用构建，网页不会允许切换九轴。

- 进入设置模式后保存融合模式及快速启动，下次重启生效；可勾选立即重启。
- 快速启动开：复用温度匹配的历史陀螺零偏；无可用历史时回退默认零偏，由后台静置估计继续收敛。关：沿用当前工程普通启动（默认零偏 + 后台估计），不是新增四秒阻塞校准。
- UART / USB 分别选择 JustFloat 或 AA55 自定义协议，并任选 Yaw、Pitch、Roll、Ax、Ay、Az、Gx、Gy、Gz。默认勾选断电保存，可取消以仅改变本次运行。
- 全不选关闭该接口遥测，不关闭控制命令。3D 姿态需要同时选择三个欧拉角；没有选中的数据不会沿用旧值。
- 旧固件仍可使用原兼容预设，新功能会提示升级而不是发送不支持的设置。

`npm test` 包含全部 511 个非空通道组合；`npm run test:config-browser` 补测独立接口、回读、断电保存/临时设置、稀疏数据、关闭输出及保存失败。两个浏览器测试使用模拟串口，实板联调另行记录。
协议见 [docs/output-config-protocol.md](docs/output-config-protocol.md)。固件设置从 V2 自动兼容读取，首次保存升级为 CRC 保护的 V3 双槽记录。刷回旧固件前应恢复备份的 V2 设置，否则旧固件无法读取 V3。

## 历史来源与原有说明

来源：线上 Worker `gyro-233688` 修复前版本 `33276162-733e-4970-b4cb-c7fba240999e`（2026-09-18 20:52 北京时间部署）。
`backup/live-deployed/` 为线上原样备份（Worker 脚本来自 Cloudflare API，静态资源从 https://gyro.233688.xyz 下载，
`version-33276162.json` 为该版本的配置元数据）；`backup/live-export/` 为最初的导出目录（其中 wrangler.toml 的
compatibility_date=2026-09-08、run_worker_first=true 与线上实际值 2026-09-15 / false 不符，本工程以线上 API 为准）。

## 结构
- `src/index.js` — Worker 源码（`/healthz` 返回 JSON，其余交给 Workers Static Assets）。经 wrangler(esbuild) 打包后与线上脚本逐字节一致。
- `src/public/` — 静态资源：`index.html`、`app.js`、`style.css`（资源 URL 带 `?v=20260930a` 缓存版本号）。
  `app.js` 中 `// ==== GYRO-CORE BEGIN/END ====` 段为纯协议 / 解析 / 3D 投影逻辑，单元测试直接抽取该段。
- `scripts/build.mjs` — `src/public` → `dist/public`（原样复制），`wrangler deploy --dry-run` → `dist/worker/index.js`，
  并校验 Worker 脚本与线上备份逐字节一致；`--verify-original` 用未修改的线上源码走同一流程，校验 4 个产物全部逐字节一致。
- `wrangler.jsonc` — name / 自定义域 gyro.233688.xyz / compatibility_date 2026-09-15 / assets（ASSETS 绑定、SPA、run_worker_first=false）与线上一致。
- `test/` — `npm test`（node:test：protocol / justfloat / sync / attitude3d / legacy-repro），
  `test/browser.check.mjs`（无头 Chrome + `mock-serial.js` 模拟 Web Serial 与按固件 main.c 应答的设备）。

## 命令
```
npm run build            # dist/public + dist/worker，校验 Worker 与线上一致
npm run verify:original  # 未修改的线上源码 → 构建产物逐字节等于线上
npm test                 # 单元测试
npm run dev              # wrangler dev（http://127.0.0.1:8798）
BASE=http://127.0.0.1:8798 node test/browser.check.mjs
npm run deploy           # build + test + wrangler deploy
BASE=https://gyro.233688.xyz SHOT=/workspace/gyro-live-prod.png node test/browser.check.mjs
```

## 回滚
`wrangler rollback 33276162-733e-4970-b4cb-c7fba240999e --name gyro-233688`
## 新增 CAN 配置（20260930b）

网页 CAN 面板现在支持 CAN_ID、MST_ID、8 档波特率、主动/请求模式、每类报文间隔、四类输出勾选及断电保存。需要当前新固件，旧固件自动禁用面板。USB/UART 仍独立输出，不因选择 CAN 而关闭。

详见 [CAN 配置与兼容边界](docs/can-config-protocol.md)。CAN 按固定帧分组选择，不是 UART/USB 的逐轴 JustFloat；400 Kbps 档实际约 401.07 Kbps。原厂手册未定义的数据宽度/掩码位义属于本项目扩展，不能据此声称原厂固件配置完全兼容。
