# 上位机排版与 PCAN 验证 · 2026-10-01

开发入口：`upper/gyro-live/src/public/`。最终资源版本：`20261001b`。

## 页面与交互

- 开始时整理了另一套分区布局并打包 `20261001a`；该方案未发布。
- 发布前发现 Grok Bot 已于北京时间 05:59:55 将网站更新为 `20260930b-ui3`。独立获取三份线上资源，确认 HTML/CSS 已变化，app.js 仍为原 `20260930b`。
- 最终保留最新紧凑布局和配色，只合并 CAN 编辑保护、撤销按钮及通道中文提示。360、390、820、1440 px 无横向溢出，通道标签不裁切。原线上资源快照见 `artifacts/web-host/online-ui3-20261001/`。
- 修复 CAN 回读覆盖草稿：输入事件标记修改，后续回读仍更新实际配置但保留表单；提供撤销修改。
- CAN 应用等待匹配的当前/保存配置回读才确认成功；失败或 3 秒未确认时保留草稿并释放控件。

## 软件验证

本地构建通过；Worker 脚本与原备份逐字节相同。44 项单元测试通过，原浏览器控制回归 14 步、独立输出回归 11 步、CAN 网页模拟 16 组运行组合及保存/回读/非法参数/恢复检查通过。

新增 CAN 草稿回归：250 ms 延迟回读、全局刷新时编辑所有 CAN 字段及数据组、保存失败保留草稿、成功后匹配回读、仅 ACK 但回读不同的超时处理、断开清理。截图：`artifacts/web-host/layout-20261001-desktop.png`、`layout-20261001-mobile.png`。

浏览器回归使用模拟串口。COM6 当前被占用，COM16 未枚举，本次没有通过串口重刷固件或运行网页实板字节桥。

## 真实 PCAN 收发

使用本机现有 python-can 4.6.1 和 PCANBasic.dll，通过 PCAN_USBBUS1 连接 PCAN-USB。API 初始化/读写/释放顺序参照 [PEAK 官方文档](https://www.peak-system.com/documentation/API/PCAN-Basic.Net/html/1a16483a-6d6d-42b3-bc65-093401a55494.htm)。测试时间约北京时间 05:47。

执行 `python tools/serial/test_pcan_hardware.py --run-hardware` 成功，证据保存于 `artifacts/web-host/pcan-hardware-20261001.json`：

- 1 Mbps、标准帧、DLC 8，默认欧拉角主动输出约 1000 帧/秒。
- 加速度/温度、角速度、欧拉角、四元数四组数据的普通及快速寻址请求，共 8 次请求；四元数解码模长检查通过。
- 主动输出全部 16 种掩码组合，每组间隔 20 ms；实际数据类别与选中组匹配，空掩码停止输出。
- 请求模式停止主动输出；7 个非法寄存器写入被拒绝，读取确认配置未改变。
- 临时切换 CAN_ID=0x123、MST_ID=0x345，寄存器及快速请求应答通过。
- 测试后回读所有 6 个运行参数与测试前一致：CAN_ID=1、MST_ID=0x6FF、baud=0（1 Mbps）、主动、间隔 1 ms、mask=4（欧拉角）。PCAN 总线状态为 0。

测试没有调用保存、重启或标定命令，没有修改设备 Flash。当前只实测 1 Mbps；其他波特率、400 Kbps 对端兼容、物理波形和终端阻值未测。软件模拟的 8 档配置通过不能替代其他波特率的实板结果。

## 发布

最终新版包 `artifacts/web-host/gyro-233688-20261001b.zip`，SHA256 为 `c24aea455b63b8b0adae4b22fd0b63005ca225ac402112d913244a45468d622d`。通过 Grok Bot 上传并发布到现有 Worker `gyro-233688` 和 <https://gyro.233688.xyz/>。

北京时间 2026-10-01 06:14:47 独立下载线上资源验收，三份文件与最终构建逐字节一致：

| 文件 | 字节数 | SHA256 |
| --- | ---: | --- |
| index.html | 10952 | `55609755495ffc8cdd11506f464f5fbace60060fd581263cac81fcb060556a3a` |
| app.js | 65162 | `48e0a2849a63bf7e1d347600b56c3b0cedf6cd3b06037d4a15daa64a0d4e4230` |
| style.css | 13187 | `c3653a7c23db54d7fb9eb35ee8dd80acf97fa4fbdcaf095d476d3a2f255b9244` |

`/healthz` 返回 HTTP 200、`ok=true`。证据见 `artifacts/web-host/online-deployment-verification-20261001b.json`。CSS 与原 ui3 的哈希相同，确认紧凑版配色和布局样式已保留。

在生产网址再次运行浏览器回归：原控制 14 步、独立输出 11 步、CAN 16 组及保存/回读/失败处理、新增 CAN 草稿及 1440/820/390/360 px 布局检查全部通过。浏览器检查使用模拟串口，真实 PCAN 的覆盖范围见上文。

Grok Bot 返回的部署元数据：版本 ID `bc7a7bbb-c006-4eca-8b72-3a2dae9b8566`，北京时间 `2026-10-01 06:13:32`。回滚点 `8109dbd7-2a2e-493e-81d0-57d6e7cbdb19`（ui3）；回滚命令 `npx wrangler rollback 8109dbd7-2a2e-493e-81d0-57d6e7cbdb19 --name gyro-233688`。版本 ID/部署时间来自 Grok Bot 对话，资源哈希、健康检查和网页回归由本机独立验证。

Grok Bot 在其环境报告旧 CAN 回归第 55 行仍有时序波动。检查发现该脚本直接设置表单值而不发出输入事件，刷新回读可能在点击前恢复表单。已在仓库测试脚本为所有模拟编辑补齐 input/change 事件，并对生产网址再次完整通过 CAN 回归。此后只调整测试脚本，生产三份资源未改变；发布 ZIP 保留为当时上传的快照。
