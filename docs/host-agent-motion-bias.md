# 上位机交接：打开 VQF 运动零偏估计

这次只把 `motionBiasEstEnabled` 从关改成开。静止零偏估计仍然开着。`biasSigmaMotion`、`biasVerticalForgettingFactor`、`biasForgettingTime`、`biasClip`、`biasSigmaRest`、`tauAcc`、静止门限和零角速保持阈值都没有改。

协议里原来的帧没有改长度。`0x0C` 融合诊断仍是 60 字节。网页不用改也能继续收姿态。运动零偏那版应用区 SHA256 `e7ecb0c8319195553ab84404556efd70914843fb714eec441fd4bf0539e0159f` 已被静置初始化镜像替换，见 `docs/host-agent-vqf-static-init.md`。不要部署。

6 轴没有绝对航向。运动零偏不能拿来消除航向漂移。竖直方向的陀螺零偏在运动中仍然很难观测。

## 怎么确认已经生效

空载荷查询 `0x30`。成功时只回 `0x09`，没有 ACK。载荷不是 0 时回 ACK，状态 `0x02`。旧固件不认识 `0x30`，回 ACK 状态 `0x01`，这时不要显示这项。

`0x09` 共 48 字节，小端：

| 偏移 | 类型 | 内容 |
|---|---|---|
| 0 | u8 | version，固定 1 |
| 1 | u8 | motion_bias_enabled，这版应为 1 |
| 2 | u8 | rest_bias_enabled，应为 1 |
| 3 | u8 | rest_detected |
| 4 | float | biasSigmaMotion，0.10 °/s |
| 8 | float | biasVerticalForgettingFactor，0.0001 |
| 12 | float | biasForgettingTime，100 s |
| 16 | float | biasClip，2.0 °/s |
| 20 | float | biasSigmaRest，0.035 °/s |
| 24 | float | tauAcc，当前档。均衡档是 2.5 s |
| 28 | float[3] | 当前 VQF 零偏，°/s |
| 40 | float | 陀螺残差模长，°/s |
| 44 | u8 | zaru_hold，零角速保持正在锁定时为 1 |
| 45 | u8 | zaru_enabled，仅 6 轴且滤波档 3 时为 1 |
| 46 | u16 | reserved，0 |

零偏 XYZ、三轴残差、姿态和 rest 仍用原来的 `0x28` / `0x0C`。不要改那一帧的长度。

第一版没有运动零偏开关。固件固定打开。不要在网页上做可写开关。

## 网页要改的地方

`upper/gyro-live/src/public/app.js` 里，未知消息号会被 `decodePayload` 丢成 `unknown`。要显示这项，必须同时加上：

- `MSG.MOTION_BIAS = 0x09`
- `PAYLOAD_LEN[0x09] = 48`，长度必须正好 48，否则整帧丢弃
- `CMD.QUERY_MOTION_BIAS = 0x30`

查询发空载荷。不要把单独的 ACK 当成成功。成功是收到 48 字节的 `0x09`，且 `version === 1`、`motion_bias_enabled === 1`、`rest_bias_enabled === 1`。`0x30` 回 ACK 状态 `0x01` 时隐藏这项，不要报成通信故障。

只做只读显示，放在现有诊断附近即可。建议写出：运动零偏已开、`biasSigmaMotion`、`biasVerticalForgettingFactor`、`biasForgettingTime`、`biasClip`、当前零偏三轴、残差模长。`tauAcc` 仍随滤波档变化，不要写成全局常数。

不要改 `0x0B`、`0x0C`、`0x0F`、`0x26`、`0x27`、`0x28`、`0x2E`、`0x2F` 的长度和字段。不要新增保存项，不要改融合模式、滤波档、陀螺量程和快速启动。旧固件查询 `0x30` 会得到未知命令；当前这块板已经是新固件。

## 零偏路径

原始陀螺只做单位换算，再经过 30 Hz 低通后送进 VQF。温度系数目前是 0。启动零偏是写进 VQF 的初值，不是在输入上先减一次。VQF 内部用自己的零偏估计减陀螺。零角速保持用的是同一估计减过一次之后的角速度模长，单位是 °/s。没有第二次相减。
