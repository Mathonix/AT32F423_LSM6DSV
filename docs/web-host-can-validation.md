# Web CAN 配置验证 · 2026-09-30

新增实现及限制：[CAN 配置协议](../upper/gyro-live/docs/can-config-protocol.md)。开发入口仍为 upper/gyro-live，未修改用户已有 tools/web_host 开发。

## 已验证

- 原生 C 五组回归：原有协议/Flash/Bootloader/USB，加上 CAN 编码与 CAN 驱动。检查 11 位 ID、四类报文位布局、错误请求/权限、波特率、带宽限制、四类每类 1 kHz 调度、CAN 波特率先旧速率 ACK 后切换与超时取消、V2/V3 到 V4 迁移、写失败回退及不重复擦写。
- 网页单元测试 44 项全部通过；旧浏览器回归 14 步、独立输出 11 步、CAN 模拟浏览器 16 组速率/模式、保存/失败/校验/恢复均通过。
- Release 九轴、Release 六轴、Debug 九轴编译通过。原有未使用标定变量警告保留。
- SWD 更新 Release 九轴应用；全段回读校验 71,344 字节，Flash SHA256 `89700a2c665ae896891bc5fcba2965fd743f1d393f4f7176d0033d148c54ea2d`。Bootloader 和配置/标定区与更新前逐字节相同。
- 原始备份 `artifacts/web-host/board-before-host-upgrade.bin` 未动；此次最终更新备份 `board-before-host-upgrade-20260930-223733.bin`。该 SHA 是 HEX 空洞补 FF 后的 Flash 镜像，不等于 BIN 补零文件的 SHA。
- 最终固件实板 CAN 配置测试连续两次通过，每次 UART/USB 共 512 组组合（2 接口 × 8 速率 × 16 掩码 × 2 主动状态），11 组无效请求，保存/重启恢复、启动/UART/USB 配置不被覆盖、恢复原 CAN 配置。
- 真实 COM16 字节驱动网页：16 组 CAN 速率/模式、11 位 ID、临时/保存回读、非法发送量拦截、恢复原配置通过。
- 新固件再次通过原有 56 组 UART/USB 输出与两个启动重启路径；真实 COM16 字节驱动网页 28 组自选输出通过。
- 检查桌面及 390 px 手机预览，CAN 表单布局正常。测试桥接已关闭并释放 COM16。

连续压力测试曾发现两个 UART 超时：SWD 读到硬件 RX overruns=2，而 UART/USB 控制队列 drops 和 RX ring drops 均为 0。原有 WS2812 在静态设置模式重复发送相同颜色，每次屏蔽中断约 30 μs；增加 RGB 去重，避免这些无必要的中断屏蔽，并在测试设置模式首次灯色切换后等待 50 ms。最终两次 512 组测试、原有 56 组、网页 28 组都通过，最后 SWD 读到 RX overruns=0、全部 drops=0。实际变色仍需短暂屏蔽中断，因此不宣称在任意灯色变换时 2-Mbaud UART 绝对不丢字节。

## 最终实板状态

运行/保存：九轴，快速启动关；UART/USB：JustFloat、Yaw/Pitch/Roll，1000 Hz；CAN：CAN_ID=1、MST_ID=0x6FF、1 Mbps、主动、每类 1 ms，仅欧拉角。测试临时改动已恢复。

## 证据与边界

证据位于 artifacts/web-host：swd-upgrade.json、hardware-can-test.json、hardware-can-web-test.json、hardware-output-test.json、hardware-web-test.json、gyro-can-real-board.png、gyro-can-mobile.png。

真实 CAN 总线对端、收发波形、终端电阻、400k 对端兼容和总线输出率尚未实测。软件 CAN 驱动测试不是物理收发证明。网页实板验证使用本地显式串口字节桥，不包含浏览器原生 Web Serial 权限弹窗/驱动完整端到端流程；正式网站不依赖字节桥。

新版 Web 部署包：artifacts/web-host/gyro-233688-20260930b.zip，源代码/构建产物一致，不含凭据、node_modules、设备 Flash 备份。已于 2026-10-01 通过 Grok Bot 发布，独立线上验证见下文。

## Cloudflare 发布与独立验收 · 2026-10-01

通过 Windows 文件复制/粘贴将部署包上传至 Grok Bot 的“123”会话，发送限定现有 Worker `gyro-233688` 和域名 https://gyro.233688.xyz/ 的发布说明。没有重新生成页面，没有将固件或 Flash 备份上传。

Grok Bot 返回的发布记录（版本 ID 和实际部署时间来自 Bot，不是本地 Cloudflare API 独立查询）：

- 当前版本：`50097c6f-91c0-4805-84b8-ebd60f70a004`。
- 部署时间：2026-10-01 04:31:22 北京时间，即 2026-09-30T20:31:22Z。
- 可回滚旧版：`ff25cf06-64e1-4400-b0ff-f17d796b778a`（20260930a）。本次未执行回滚。
- Bot 报告保留原 wrangler 配置、Worker 脚本不变，且没有修改其他服务。

本地独立通过 HTTPS 获取线上资源并与 `upper/gyro-live/dist/public/` 比较，全部 HTTP 200、逐份 SHA256 一致；首页引用版本均为 `20260930b`，含 CAN 面板：

- index.html：9952 字节，`c452753f329c0229c39e7c0c349b6d2ca1ad75cec5e94f059b0560b6507c7fe7`。
- app.js：63304 字节，`f27cc0a372817055fb484aaf01f1fed5247eba5e0913dd9a67e38441b715b64e`。
- style.css：4118 字节，`f3ab982b725b0c0ec43c6c4adbcbcdd0c9be99c93a61dfbad35ba1dfee4bcc1d`。
- `/healthz`：HTTP 200，`{"ok":true,"service":"at32-ahrs-web-host"}`。
- 部署 ZIP SHA256：`247c3e6eac083493bc267ae0c189b2163642c1f7e8583ec240a995988c6f1d01`。

对线上网站以 Windows 无头 Chrome、`BASE=https://gyro.233688.xyz`、模拟串口运行三套现有脚本，全部退出 0：旧浏览器 14 步、独立输出 11 步、CAN 16 组运行时组合及保存/回读、独立性、非法参数、保存失败、恢复检查。未打开实际串口，未改动实板配置。这些结果不替代真实 CAN 总线收发验证。

### 已确认的异步回读编辑问题（本次没有改代码或再次发布）

Grok Bot 的 CAN 浏览器测试在非法参数步骤出现超时；它报告点击读取后，脚本仅等待已经满足的 nodeId 条件便开始编辑，迟到的 CAN 回读把输入间隔从 1 ms 回填为合法的 30 ms，导致测试等待错误提示超时。

本地独立验证了其中的表单覆盖行为：仅在模拟设备中将 `QUERY_CAN` 回读延迟 200 ms，在回读到达前编辑 `canPeriod=1` 并发出 input 事件，随后该值被回填为 30。`configureCan()` 当前无条件重填表单，也没有查询待完成的编辑保护。因此默认时序回归通过不表示这一竞争已解决。

使用建议：读取后等配置回填完成再编辑，应用前核对输入值。后续需要修复查询完成判定及保护用户未提交的编辑，并增加延迟回读测试；当前已发布资源保持与部署包一致。相关独立验收摘要见 `artifacts/web-host/online-deployment-verification.json`。
