# Cloudflare 上位机同步：20261001acc3

后续网页升级版 `20261001fw1` 已发布，当前部署 `f6a77f67-1a0e-4f1e-bb58-ca71aee7842b`，8份资源独立HTTPS核验通过。最新记录见 [网页固件升级验证](web-firmware-upgrade-validation.md)；下文保留acc3历史证据。

线上地址：https://gyro.233688.xyz/。用户指定沿用 Grok Bot 发送发布包及部署指令，使用 Windows computer-use 技能操作已有“123”会话。

## 发布内容

`upper/gyro-live` 为正式上位机工程。同步六面校准面板及原始 XYZ g 值、方向参考、旧固件兼容和失败反馈；合并线上 `20261001f-c1` 紧凑 CSS，保留真实 PCB 模型、轴向修正、重启重连、量程、串口及 CAN 频率功能。只部署静态资源及原 Worker；不包含固件升级网页功能。

发布包 `artifacts/web-host/gyro-233688-20261001acc3.zip`：1,628,361 字节，SHA256 `2ae0719ee25ac6791226fc97277396dca953a93cb528f427234a7326989584ab`。通过显式文件清单打包，校验源文件与产物一致；无凭据、node_modules、固件 BIN/HEX 或设备 Flash 备份。7 份静态资源的哈希见 `artifacts/web-host/deployment-manifest-20261001acc3.json`。Worker 脚本 468 字节，与原线上备份逐字节相同。

## 发布元数据

以下版本及实际发布时间由 Grok Bot 返回，未使用本机 Cloudflare API 独立查询：

- 新版本：`3b531072-4740-48ad-b386-ee7074838897`。
- 发布时间：2026-10-01 22:29:31，北京时间。
- 发布前版本及回滚点：`34383ec1-ebf4-4a79-882a-b60edfdc4878`（20261001f-c1）；Bot 报告发布前已查询确认其占 100% 流量。
- 回滚命令：`npx wrangler rollback 34383ec1-ebf4-4a79-882a-b60edfdc4878 --name gyro-233688`。

## 本机独立核验

北京时间 22:31:15，通过公开 HTTPS 下载并保存全部 7 份资源，前端缓存版本为 `20261001acc3`。`/healthz` HTTP 200，返回 `ok:true`、服务 `at32-ahrs-web-host`。

6 份非 HTML 资源的原始响应字节数及 SHA256 完全匹配 manifest。HTML 原始响应为 14,343 字节，SHA256 `fc49437a79534d1e158bff5b00ba58b0769be34335effe2311b3c8c1aec797ea`，比产物多 367 字节。差异仅为 Cloudflare 在边缘添加的既有 `static.cloudflareinsights.com` 统计模块（发布前 HTML 也包含该模块）。严格移除这一项后，HTML 为 13,976 字节、SHA256 `a35063cb7213b9302bf3b0b7e85afc4f2988a6ca56e48a858d148d726c978d1b`，完全匹配产物；未调整站点统计或隐私设置。

线上 Chrome 模型回归通过，确认加载真实 GLB（210 个源元件网格、142832 个三角面、5 种材质），验证欧拉角/四元数和 Pitch/Roll 安装方向、查看角度、缩放、复位、加载失败回退及四种屏宽。模拟串口不会修改实板。

本地构建、51 项逻辑测试、11 组六面校准浏览器场景、重启重连、8 项量程/频率场景均通过；合并 CSS 后重跑六面校准及模型回归，通过。更新了旧浏览器测试中的拟合失败提示断言，并补充失败保留旧参数及末次读数状态检查。

证据：

- `artifacts/web-host/online-after-sync-20261001acc3.json` 及对应目录：独立 HTTPS 响应及哈希。
- `artifacts/web-host/cloudflare-*-20261001acc3.log`：构建与测试日志。
- `artifacts/web-host/cloudflare-online-20261001acc3.jpg`：实际线上界面，含真实模型及六面面板。
- `artifacts/web-host/cloudflare-model-online-20261001acc3.log`：线上模型回归。

`online-before-sync-20261001acc3.json`/同名目录是发布期间第一次获取结果，抓取时 Bot 已完成部署，因此不作为旧版本备份。旧版首页及样式为独立的 `online-before-sync-20261001acc3.html`/`.css`；回滚依赖上述已确认的 Cloudflare 版本。

六面校准实板验收此前由用户暂停，仍未完成；本次网页发布和模拟测试不替代该验收。
