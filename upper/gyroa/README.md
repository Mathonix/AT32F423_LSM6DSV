# gyroa 网页上位机

目标域名 `gyroa.233688.xyz`。保留可构建源码、真实 PCB 模型和 Worker，兼容设备配置 v1～v4；共享启动窗口需 `20261003e` 的配套 BL/APP。

需要 Node.js ≥22.12.0，在本目录运行：

```sh
npm ci
npm run build
npm run dev
```

静态源码在 `src/public/`，Worker 在 `src/index.js`，构建复制到 `dist/public/` 并生成 SHA256 manifest。本地默认 `http://127.0.0.1:8833/`，已保留模型 bundle，无需 STEP 转换依赖。页面开发后重新构建即可。

登录目标 Cloudflare 账户后执行 `npm run deploy` 发布。本仓库只提供部署配置，不含账号凭证。

## 使用

桌面 Chrome/Edge 连接设备后，设置页可设启动窗口 0～60 秒（默认 2 秒）、融合模式与陀螺仪量程。T=0 使用历史启动；T>0 在 BL 升级等待期间采集，到期不合格立即使用历史。保存后可重启并自动重连原端口。旧固件仍显示原快速启动选项。

状态页显示真实板卡姿态；输出页配置 USB/UART/CAN；校准页提供六面加速度计校准、零偏历史及静置 VQF 初始化；设置页提供四档滤波和阈值。

升级页读取型号 AT32 和应用版本，选择 APP BIN 后通过 USB/UART 更新。网页只更新 APP；首次新版 BL 安装需 SWD 配套烧录。`?mock=1` 为演示设备，不能视为实板结果。
