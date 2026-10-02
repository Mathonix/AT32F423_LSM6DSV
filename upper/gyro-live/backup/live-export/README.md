# gyro.233688.xyz 线上版本源码（从 Cloudflare 直接导出）

导出时间：2026-09-28。Worker `gyro-233688`，当前线上版本 33276162（部署于 2026-09-18 20:52 北京时间）。

- `src/index.js`：线上 Worker 脚本（从 Cloudflare 导出，只去掉了末尾的 sourcemap 注释）。`/healthz` 返回健康检查，其余请求交给静态资源。
- `public/index.html`、`public/app.js`、`public/style.css`：线上静态资源，从 https://gyro.233688.xyz 原样下载。
- `wrangler.toml`：部署配置（静态资源目录 ./public，自定义域 gyro.233688.xyz）。

部署：`npx wrangler deploy`。
