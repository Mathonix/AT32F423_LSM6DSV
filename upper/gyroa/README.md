# gyroa · Sensor Studio

AT32F423 AHRS 的网页上位机。浏览器直连板卡，不装客户端、不需要本地服务。

**[在线使用 →](https://ag.233688.xyz/)**　[GitHub Pages 镜像](https://mathonix.github.io/AT32F423_LSM6DSV/)　[原 Cloudflare 镜像](https://gyroa.233688.xyz/)

[Cloudflare Pages 入口](https://gyroa-upper.pages.dev/)

[10 月 23 日旧界面](https://gyroa-upper-previous.pages.dev/)（`20261023runtime1`）

![状态页](docs/preview-status.jpg)

需要桌面版 Chrome / Edge（Web Serial）。手机浏览器没有 Web Serial，可加 `?mock=1` 看演示。

## 连接

| 方式 | 说明 |
|---|---|
| Web Serial | 板卡 USB 直连，Chrome / Edge 桌面版 |
| USB / UART 串口 | 外接 USB-TTL，选端口与波特率（板卡默认 2 Mbaud） |
| WebUSB | 需要 WinUSB 驱动的场景，固件升级用 |

`?mock=1` 是模拟设备，用于演示与前端回归，不代表实板结果。

WebUSB 接收更新 `20261025webusb1` 使用四个有界 IN 请求提前接收，并隔离关闭前后的会话，避免旧 reader 的取消或迟到结果影响新连接。开关操作后的实板复测约 950～1000 帧/s；页面「数据速率」表示浏览器接收并解析的遥测帧率，板内处理速率请看 ESKF 的消费 / 预测速率。通信统计的悬停提示包含 WebUSB 字节率、接收队列和错误数。

固件更新页进入后只读取本仓库的 Release 列表。用户选择版本和应用 `.bin` 后，点击「下载所选固件」，校验文件大小、应用向量以及 Release 提供的 SHA-256；再手动点击「开始升级」写入设备。列表和版本选择不会下载或刷写固件，仍支持本地文件。

算法参数按组显示为小卡片。传感器校准四项和设备偏好各项分别汇总在同一页面，手机端自动单列。

## 页面

四个工作区，桌面顶部导航 / 手机底部固定入口：

| 工作区 | 页面 | 内容 |
|---|---|---|
| 监测 | 实时姿态 | 欧拉角、3D 板卡视图、加速度与角速度波形、快捷设置 |
| 配置 | 融合调参 | VQF 14 项参数、ESKF 参数、陀螺预滤波、比例因子 |
| 配置 | 传感器校准 | 加计六面、陀螺零偏历史、磁椭球校准、静置 VQF 初始化 |
| 配置 | 输出接口 | USB / UART / CAN 字段与频率、VOFA 格式 |
| 实验 | 稳定性测试 | 1–60 min 零漂统计，首尾均值、峰峰值、趋势、温度 |
| 实验 | 数据记录 | 1–500 Hz 九通道 CSV 采集 |
| 设备 | 设备设置 | 启动窗口、融合算法、量程、静置零偏、WS2812、重启 |
| 设备 | 固件更新 | 选 APP BIN 通过 USB/UART 升级（只写 APP） |

## 本地开发

Node.js ≥ 22.12：

```sh
npm ci
npm run build     # 生成 dist/public
npm run dev       # http://127.0.0.1:8833/
```

页面源码在 `src/public/`，Worker 在 `src/index.js`。`dist/` 是构建产物，改它会在下次构建被覆盖。

回归：`npm test`（Node 单测）、`BASE=http://127.0.0.1:8833 node test/app1.browser.check.mjs`（无头 Chrome + mock）。

## 部署

Cloudflare Pages 使用现有项目 `gyroa-upper`，正式地址为 <https://gyroa-upper.pages.dev/>。执行 `npm run deploy:pages` 会构建 `dist/pages`，再发布到该项目的 `master` 生产分支。单独验证构建使用 `npm run build:pages`；本地预览使用 `npx wrangler pages dev --cwd pages --port 8834`。Pages 配置独立放在 `pages/wrangler.jsonc`。

Pages 构建把同一份 API Worker 打包为 `_worker.js`，用 `_routes.json` 只处理 `/healthz` 与 `/api/releases` 及子路径，使固件列表与下载在新域名下继续使用同源接口。其余页面和资源由 Pages 静态托管。部署需要 Cloudflare Pages 写入权限。

旧界面独立发布到 `gyroa-upper-previous.pages.dev`。`npm run deploy:pages:previous` 从 Git 提交 `1ab7765c21781040d57d8f002467bc404a5fbc5d` 提取 10 月 23 日生产版源码，在 `dist/previous-source` 和 `dist/pages-previous` 构建；不会切换或覆盖当前工作区。旧版保留当时的界面与功能，单独构建使用 `npm run build:pages:previous`。

正式入口为 <https://ag.233688.xyz/>，不带 `?mock=1`，默认使用真实设备连接。执行 `npm run deploy:ag` 会先构建，再按 `wrangler.ag.jsonc` 发布到独立的 `ag-233688` Worker。

推送 `master` 上的 `upper/gyroa/**` 会触发两个工作流，都是在 Actions 里 `npm ci && npm run build` 后发布：

| 目标 | 工作流 | 地址 |
|---|---|---|
| GitHub Pages | [`deploy-github-pages.yml`](../../.github/workflows/deploy-github-pages.yml) | <https://mathonix.github.io/AT32F423_LSM6DSV/> |
| Cloudflare Worker / Pages | [`deploy-gyroa.yml`](../../.github/workflows/deploy-gyroa.yml) | <https://gyroa.233688.xyz/> |

Pages 是项目子路径，工作流会把生成产物里的绝对资源路径改写成 `/AT32F423_LSM6DSV/`，并把 `.nojekyll` 一并放进 artifact。Cloudflare 需要仓库 secret `CLOUDFLARE_API_TOKEN`。

## 固件兼容

界面按固件回复的能力位自适应，旧固件缺项会禁用对应控件并提示升级。常用最低版本：

| 功能 | 最低固件 |
|---|---|
| 配置 / 启动窗口 / 量程 | 配套 `20261003e` BL+APP |
| 融合算法切换、滤波档位 | `20261007a` |
| 静置零偏估计开关 | `20261008a` |
| 磁力计校准 | `20261006b` |
| 磁干扰拒绝 | `20261006c` |
| 惯导位移估计 | `20261005i` |
| VQF 调参 | `20261011a` |
| 陀螺比例因子 | `20261011b` |
| 运动数据采集 | `20261012b` |
| 加速度计量程 | `20261012a` |

帧格式固定为 `AA 55 | msg | len | seq | payload | CRC16`，命令与载荷定义以 [`inc/telemetry/protocol.h`](../../inc/telemetry/protocol.h) 为准。

## 许可

第三方许可见 [`src/public/THIRD-PARTY-NOTICES.txt`](src/public/THIRD-PARTY-NOTICES.txt)。
