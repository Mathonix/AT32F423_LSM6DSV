# 上位机任务：接入第 4 档「零角速保持」

把这份说明交给负责 `upper/gyro-live` 的 agent。固件已经烧进当前这块板子，上位机只改网页和测试。不要改固件，不要刷机，不要执行 `npm run deploy` 或 `wrangler deploy`，除非用户另说。

工作区：`E:\Desktop\1_Program\AT32F423_LSM6DSV_SPI_Test`

上位机目录：`upper/gyro-live`

公开站点仍是旧页面。本地源码里已经有一版第 4 档改动，先按下面核对，缺的补上，已经符合的不要重写。

## 当前板子，先读再动

2026-10-02 经 USB CDC（VID:PID `0x2E3C:0xF401`，当时是 COM16）读回并已保存：

- 融合：六轴，运行和保存都是 0
- 量程：±125
- 滤波：运行 3，保存 3，名称「零角速保持」
- `0x0B` capabilities：4
- 回读时间常数：`tau_mag = 4.0 s`，`rest_tau = 0.5 s`

这不是出厂默认。不要为了测试把滤波改回 0、1 或 2。改之前先 `0x26` 读回，结束时恢复这次读到的运行值和保存值。

控制口是 USB CDC 或 WCH UART（VID:PID `0x1A86:0x8012`）。不要打开名称里带蓝牙的 COM 口。1000 Hz 遥测会淹没应答，要连续读并按序号匹配。`0x27` 必须先 `0x17` 进入设置，完成后 `0x18` 退出。不要用 `0x1D` 改输出频率。

## 产品行为，文案按这个写

第 4 档只在六轴锁定对外航向。

- 运动时，发布的 Yaw 正常更新。
- 停止后约 100~200 ms，UART、USB、CAN 的欧拉角和 CAN 四元数不再跟随内部漂移。判据是扣除陀螺零偏后的角速度，不是 VQF 的 1.5 s 静止标志。细节见 `docs/host-agent-zaru-threshold.md`。
- 角速度超过约 0.70 °/s 并持续约 3 ms 后解除，航向连续，不能跳变。
- Roll、Pitch 继续跟随姿态输出。
- 九轴选这一档时，滤波参数仍与均衡相同，但不锁定磁力计航向。
- 滤波参数与均衡相同：加速度 2.5 s，静止门限 1.0 °/s、0.25 m/s²，停稳后外层保持 0.8 s。
- 不改量程、带宽、零偏估计和前三档编号。

界面文案：

- 中文：`零角速保持`
- 副标题：`静止时锁定航向，检测到运动后立即恢复更新`
- 英文：`ZARU` / `Stationary Heading Hold`
- 补上两句：滤波参数与均衡相同；仅六轴锁定航向，九轴不锁磁力计航向。

编号固定为：

| 值 | 名称 | 磁力计 / 静止输出 / 运动输出 |
| --- | --- | --- |
| 0 | 响应优先 | 2.0 / 0.15 / 0.04 s |
| 1 | 均衡 | 4.0 / 0.50 / 0.10 s |
| 2 | 静态稳定 | 6.0 / 1.50 / 0.20 s |
| 3 | 零角速保持 | 4.0 / 0.50 / 0.10 s |

## 协议

`0x26` 查询，应答 `0x0B`，16 字节，小端：

| 偏移 | 内容 |
| --- | --- |
| 0 | version，必须是 1 |
| 1 | 当前 profile，0..3 |
| 2 | 已保存 profile，0..3 |
| 3 | capabilities，即档位数。新固件是 4，旧固件是 3 |
| 4 | `estimator_hz`，u16，必须是 1000 |
| 6 | reserved，必须是 0 |
| 8 | `tau_mag`，float |
| 12 | `rest_tau`，float |

`0x27` 两字节：`profile`、`persist`。`persist` 只能是 0 或 1。新固件接受 0..3，拒绝 4 及以上，ACK 状态为 2。必须在设置模式里发送。成功后同一序号还会再回一帧 `0x0B`。

诊断 `0x0C` 第 2 字节是 profile，合法范围改成 0..3。它后面的磁力计标志不是档位数，原有范围不要放宽。

## 本地已经改过的位置

核对这些文件，不要把第 4 档删掉：

- `upper/gyro-live/src/public/index.html`：`<option value="3">零角速保持</option>`，脚本缓存参数是 `app.js?v=20261002zaru1`
- `upper/gyro-live/src/public/app.js`：`FILTER_NAMES` 有第 4 项；`0x0B` 接受 profile 0..3，且 capabilities 为 3 或 4；诊断 profile 接受 0..3；`applyFilter` 拒绝大于 3
- `upper/gyro-live/test/mock-serial.js`：capabilities 写 4，时间常数数组是 `[2,4,6,4]` 和 `[.15,.5,1.5,.5]`，`0x27` 拒绝 `profile > 3`
- `upper/gyro-live/test/filter-profile.test.mjs`：旧帧 `[1,2,1,3]` 仍合法；profile 4 为 unknown；profile 3、capabilities 4、`tauMag=4`、`restTau=0.5` 能解码

## 还要补上的行为

1. 把 `0x0B` 的 capabilities 交到界面状态里，例如 `profileCount`。不要只在解码时检查后丢掉。
2. `profileCount === 3` 的旧固件继续显示前三档，并禁用或隐藏「零角速保持」。不要让用户发出会被旧固件拒绝的 `profile=3`。
3. `profileCount === 4` 时可选第 4 档。当前这块板子就是 4。
4. 当前融合不是六轴时，第 4 档的说明要写明「本档不锁定航向」。融合模式来自 `0x07`，不要靠灯的颜色判断。
5. 前三档的编号、名称和提示不要改回旧文案。
6. 浏览器模拟器在 `profileCount === 4` 时要能应用并回读第 4 档；发 4 仍要被拒绝。补一条 `filter-profile` 测试，不要只靠现有的解码测试。

在 `upper/gyro-live` 运行：

```text
node --test test/filter-profile.test.mjs
npm test
```

改了页面交互后再跑对应的 browser check。不要部署。完成后说明改了哪些文件、旧固件 capabilities=3 时第 4 档是否发不出去、以及测试结果。
