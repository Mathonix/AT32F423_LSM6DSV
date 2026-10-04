# AT32 AHRS 网页上位机 · 20261004startup1

目标地址：**https://gyroa.233688.xyz/**，沿用用户提供的 `gyroa-src-20261003.zip` 配置。此目录与旧 `upper/gyro-live` 独立，当前包未部署到线上。

## 本次更新

- 支持固件 `20261003e` 的配置 v4（28 字节），保留 v1/v2/v3 兼容。
- 设置 → 设备 → 启动窗口：0～60 秒，默认 2 秒。升级等待和零偏采集共用这段时间；到期采集不合格使用历史零偏，不再追加等待。0 秒检查 APP 后直接用历史启动。
- v4 隐藏旧“快速启动”复选框，标志由 T=0 自动派生；旧固件仍按原复选框和 0.1～60 秒规则操作。
- 保存需 ACK 和配置回读确认；非法输入不进入设置模式。失败保留草稿，可“放弃修改”。“保存后重启”与首页融合快捷切换复用原端口重连，重连期限 90 秒覆盖 T=60 秒。
- 升级页新增设备型号。连接时发送空命令 `0x35`，读取 `0x36` 的四字节 ASCII `AT32`（无 NUL）；旧固件不支持则显示 `--`。版本查询仍为 `0x23 → 0x32`。
- 保留原包的 USB/UART 升级、输出/CAN、四档滤波、零角速阈值、运动零偏、历史零偏、VQF 初始化、真实 PCB 模型与移动端界面。

## 使用

桌面 Chrome/Edge 打开网页，连接设备，进入“设置”填写启动窗口，点击底部“保存”。勾选“保存后重启”可立即生效，否则下次重启生效。普通上电 T>0 时保持静止；T=0 没有普通升级等待，但仍能通过应用的主动升级命令进入 BL。

共享窗口必须配套新版 **BL 和 APP**。网页升级只更新 APP，首次更新 BL 需要 SWD；单独升级 APP 不能获得旧 BL 的并行采样功能。

## 源码与构建

需要 Node.js ≥22.12.0。静态页面的可编辑源码在 `src/public/`，Worker 在 `src/index.js`；`dist/public/` 为构建产物，修改源码后需重新构建。

```sh
npm ci
npm run build
npm test
npm run dev
```

开发地址为 `http://127.0.0.1:8833/`。`?mock=1` 是不连接硬件的展示模式。浏览器回归需安装 Chrome，可通过环境变量 `CHROME` 指定路径，`BASE` 指定测试服务地址。

```sh
npm run test:startup
npm run test:quick
npm run test:version
npm run test:app4
```

构建生成 `dist/asset-manifest.json`，记录各静态文件 SHA256。部署包保留原 Wrangler 路由；发布操作在登录目标 Cloudflare 账户后执行：

```sh
npm run deploy
```

部署前确认当前账户管理 `gyroa.233688.xyz`。本次仅完成本地修改、验证与打包，没有执行发布。

## 验证边界

本包的界面和协议回归使用模拟 Web Serial，未再次烧录或改变板卡配置。T=0/2/60 的真实计时、运动到期回退及 USB 完整升级，见工程 `docs/host-agent-shared-startup.md` 的前一轮实板记录；不能把模拟重启时序等同于实板 60 秒测试。

原始 ZIP SHA256：`b0a3b83cf0fd69284ea5778be02f4a80a48ef5aed6d5e6e732f229cd140c25fb`。
