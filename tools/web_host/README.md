# AT32 AHRS Web 上位机

这是一个可以直接由 Wrangler 部署到 Cloudflare Workers/Pages 的静态 Web 上位机。它不修改固件，也不依赖现有 Python 上位机文件；浏览器通过 Web Serial 直接连接 AT32 的 USB CDC 或 UART 转 USB 设备。

## 功能

- 中文界面，实时显示 Yaw、Pitch、Roll、温度和帧率。
- Canvas 姿态显示，不引入第三方前端依赖。
- 支持 VOFA+ JustFloat 四通道数据：`yaw, pitch, roll, temperature`。
- 支持现有 `AA 55 CMD LEN SEQ PAYLOAD CRC16` 命令协议。
- 六轴、九轴、九轴相对角三种模式选择。
- 设置模式进入/退出；只有进入设置模式后才能保存模式。
- “应用后立即重启”选项；未勾选时保存配置并提示设备重启后生效。
- Yaw 归零、重新校准、PING、查询状态。
- `/healthz` 健康检查接口。

## 本地开发

需要 Node.js 18 或更新版本，并安装 Wrangler：

```powershell
cd tools/web_host
npm install
npm run dev
```

打开 Wrangler 输出的本地地址，通常为 `http://localhost:8787`。Web Serial 在 localhost 上可用。浏览器建议使用最新版 Chrome 或 Edge，并在页面打开后点击“连接 USB / UART”。

也可以先做部署前检查：

```powershell
npm run check
```

该命令执行 `wrangler deploy --dry-run`，会检查 Worker 入口和 `public/` 静态资源。

## 部署到 Cloudflare

```powershell
cd tools/web_host
npm install
npx wrangler login
npm run deploy
```

`wrangler.toml` 使用 Workers Static Assets：

- Worker 入口：`src/index.js`
- 静态目录：`public/`
- 绑定名：`ASSETS`
- 单页路由回退：`not_found_handling = "single-page-application"`

部署后访问 Wrangler 返回的 `https://at32-ahrs-web-host.<账户子域>.workers.dev` 地址即可。页面通过 Worker 的静态资源绑定提供，`/healthz` 返回 JSON 健康状态。

## 子域和自定义域名

先在 Cloudflare 中将域名加入同一账户，并确保 DNS 托管在 Cloudflare。然后执行：

```powershell
npx wrangler domains add ahrs.example.com
```

如果使用 Cloudflare 控制台，在 Workers & Pages 中打开 `at32-ahrs-web-host`，进入 **Settings → Domains & Routes → Add Custom Domain**，填写例如 `ahrs.example.com`。Cloudflare 会创建或配置对应 DNS 路由。也可以用一个已有的 Worker 路由，例如 `imu.example.com/*`，但应确保该路由指向此 Worker。

不要把串口数据转发到服务器：本项目的设备数据只存在当前浏览器标签页中，Cloudflare Worker 只负责发送页面和健康检查响应。

## Web Serial 使用要求

1. 使用 HTTPS 部署地址或 `http://localhost`。
2. 用 Chrome/Edge 打开页面并授予串口权限。
3. USB CDC 设备通常选择设备生成的 COM 端口；UART 需要 USB-UART 转换器。
4. 固件输出为 2 Mbps 时选择 2000000；Web Serial 的波特率必须与固件/转换器一致。
5. JustFloat 模式默认按四个 float 解析，帧尾为 `00 00 80 7F`。二进制控制命令和 JustFloat 数据可共用同一连接。

## 模式命令约定

网页发送的控制帧为：

```text
AA 55 CMD LEN SEQ PAYLOAD CRC16_L CRC16_H
```

当前使用的命令：

- `0x10`：PING
- `0x11`：Yaw 归零
- `0x12`：重新校准陀螺仪
- `0x14`：查询状态
- `0x17`：进入设置模式
- `0x18`：退出设置模式
- `0x19`：设置融合模式，payload 为 `[mode, apply_now]`
  - `mode=0` 六轴
  - `mode=1` 九轴
  - `mode=2` 九轴相对角
  - `apply_now=0` 保存但等待重启
  - `apply_now=1` 保存后立即重启
- `0x15`：设备复位

若浏览器没有显示数据，先确认串口已打开、波特率正确，并在“解析”下拉框中尝试“JustFloat”或“二进制”。

## 目录结构

```text
web_host/
├─ package.json
├─ wrangler.toml
├─ README.md
├─ src/
│  └─ index.js
└─ public/
   ├─ index.html
   ├─ style.css
   └─ app.js
```
