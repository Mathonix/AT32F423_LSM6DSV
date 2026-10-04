# AT32 AHRS 使用指南（供 AI 阅读）

更新日期：2026-10-04。当前实板固件：`20261003e`；说明书原基准：`20261002c`。本次上位机目标：<https://gyroa.233688.xyz/>（用户确认沿用压缩包配置），本地已更新，未部署。此前 <https://gyro.233688.xyz/> 的检查结论为历史记录。

本次网页源码入口为 `../upper/gyroa/src/public/`，Worker 为 `../upper/gyroa/src/index.js`。用户提供 `E:/Downlload/gyroa-src-20261003.zip`，保留其新版界面与移动端，新增配置 v4 共享启动窗口 T=0～60 秒、非法输入先校验、型号 `0x35 → 0x36` 显示 `AT32`。设置页 v4 隐藏快速启动复选框，T=0 使用历史，T>0 共用升级等待与零偏采集；旧 v1/v2/v3 保留原行为。自动重连期限 90 秒、融合快捷切换期限 95 秒覆盖最大窗口。量程与输出频率 v4 保存/回读仍支持。修改 `src/public` 后执行 `npm run build`，不要直接修改 `dist/public`。完整使用、构建与验证边界见 `../upper/gyroa/README.md`。

更新包在 `E:/Downlload/gyroa-src-20261004-startup1.zip` 和 `E:/Downlload/gyroa-deploy-20261004-startup1.zip`。本轮网页测试为模拟串口，不能宣称浏览器已在实板测试或网站已上线；板卡仍沿用前一轮实测配置。本次没有重新烧录或设置硬件，没有增加温度补偿。

同日新增并已烧录 `20261003d`：型号查询空命令 `0x35`，回复 `0x36` 的 4 字节 ASCII `AT32`；命令示例与校验见 `host-agent-device-model.md`。全部原生回归和构建通过，随后通过 MicroLink 烧录并完整回读校验，Bootloader 与配置/校准区保持一致。COM29 USB 实测型号 `AT32`、版本 `20261003d`，还验证了非空查询拒绝及恢复。未改网页、未部署。构建位于 `../build/device_model_query/release-9axis/device_model_query.bin`，实板证据位于 `../artifacts/diagnostics/model-query-hardware-20261003d.json`。下文 `20261002c` 的发布及板卡表格属于历史快照。

2026-10-04 已配套烧录 BL 和 `20261003e` APP：T=0/2/60 秒实板测试、6 秒运动到期回退、T=0 主动维护与同镜像 USB 升级通过，完整固件回读通过。最终六轴模式0、T=2秒、±2000 dps、2000 Hz，校准及旧历史保留。当前历史43条（sequence136）。协议、文件及测试证据见 [共享启动指南](host-agent-shared-startup.md)。线上 gyro.233688.xyz 尚未发布本次配置 v4 上位机变更；不得把本地已测试页面视作线上已更新。下文其他板卡/版本表格是历史快照。

最新正式 Release 为 [20261003e](https://github.com/Mathonix/AT32F423_LSM6DSV/releases/tag/20261003e)，标为 Latest，来源提交 `8db84a8`；附件含已实测 APP、配套 BL、两个 gyroa 上位机包及 SHA256 校验。首次共享窗口安装需 SWD 配套更新 BL/APP，网页只能更新 APP；网站仍未部署。下文 `20261002c` Release 是历史记录。

本指南用于理解设备、指导使用、定位问题和继续项目工作。硬件现状以运行时回读为准；不要把历史测试设置或本文快照当作默认配置。

## 1. 来源与版本优先级

1. 用户本次明确要求决定工作范围。
2. 实际设备回读决定当前配置、版本与能力；本次文档工作未连接或修改实板。
3. `../progress.md` 是最新工程交接入口；同一事项的旧交接记录不能覆盖它。
4. 当前网站资源决定已上线的入口和操作名称。本次通过 HTTPS 只读检查页面与 JS，没有修改、构建或部署网页。
5. 各 `host-agent-*.md` 用于字段布局与兼容规则；冲突处按本文列出的修正处理。

2026-10-03 网站检查发现，线上已接入版本查询 `0x23 → 0x32`、启动零偏历史、静置 VQF 初始化、运动零偏和零角速保持阈值。这比 `progress.md` 中“线上不会发 0x23”更新。只能据此确认线上代码具备功能，不能推断当前电脑已连接设备或实板操作已成功。

线上布局为“状态 / 输出 / 校准 / 升级 / 设置”。资源参数包括 `app.js?v=20261002biashist1`、`ui-shell.js?v=20261002fwver1`。本地 `upper/gyro-live` 与线上可能不同，不能用本地旧 UI 代替线上说明。

## 2. 固件与板卡快照

以下是 `progress.md` 的交接快照，不是本次重新采集的数据：

| 项目 | 值 |
| --- | --- |
| 应用版本 | `20261002c` |
| 融合 | 六轴，模式 0 |
| 滤波 | 档 0，响应优先 |
| 量程 | ±1000 dps |
| 快速启动 | 开 |
| 普通启动采集时长 | 2000 ms |
| UART / USB 公共输出频率 | 1000 Hz |
| 启动历史 | 36 条，记录版本 3，sequence 129，corrupt 0 |
| Release | <https://github.com/Mathonix/AT32F423_LSM6DSV/releases/tag/20261002c> |
| 发布文件 | `20261002c.bin` |
| 发布文件 SHA256 | `8724340ab1ab18801636ffc9989a1470c56bf88ea74641cc7b1646ecb53d5bd9` |
| SWD 回读 SHA256 | `9b613764bfff58c6a87f34baece5b5e095b0674c16825da275167974e75a19b7` |

发布 bin 与 SWD 镜像差 4 个对齐字节：文件为 `00`，板上 `0x0801E3F4` 为 `FF`。不能把这两个哈希不相等直接判定为升级失败。网页及串口升级使用发布 bin。

同日版本用字母递增，下一版从 `20261003a` 起；不要自行改用 `v1.0.0`。

## 3. 连接与识别

| 接口 | 设备身份 | 历史端口号 | 配置 |
| --- | --- | --- | --- |
| 板载 USB CDC | VID:PID `2E3C:F401`，序列号 `22EC987C8068` | COM16 | 2000000、8N1；CDC 忽略物理波特率 |
| WCH UART | VID:PID `1A86:8012`，序列号 `349B8F06B96E` | COM6 | 2000000、8N1 |
| CAN | CAN2，经外部收发器 | 无 COM 口 | 通过 USB / UART 配置 |

端口号会变，先枚举身份，不打开无身份的蓝牙串口。UART TX/RX 交叉且共地。MCU USART4 TX=PA0、RX=PA1；CAN2 RX=PA2、TX=PA3 是数字脚，必须经收发器后接 CANH/CANL；USB D+=PA12、D-=PA11。

浏览器连接需要用户完成串口授权。不要宣称 AI 已代替用户完成浏览器选口。串口不能同时被网页和 Python / VOFA 占用。

按当前交接，打开串口后先持续接收并丢弃约 16 秒启动数据，再查询。不要拉 DTR/RTS 复位，不要用 `0x15` 或 `0x16` 探活，不要用 `0x1D` 关闭输出。只读文档任务不应打开串口或刷机。

USB 复位会重新枚举，应关闭旧句柄、识别原设备并重新 PING。Web Serial `getInfo()` 不提供序列号；不能凭相同 VID/PID 选另一块板。UART 转接器一般保留原端口。网页刷新后需要重新连接。

## 4. 推荐只读查询顺序

1. 识别接口，按 2000000、8N1 打开并持续读取。
2. PING `0x10`，匹配 ACK 序号与命令。
3. 查询设备 `0x1F`、CAN `0x21`、滤波 `0x26`、版本 `0x23`。
4. 按任务需要查静置状态 `0x29` / 参数 `0x2C`、保持阈值 `0x2E`、运动零偏 `0x30`、历史 `0x33`。
5. 只有任务要求修改时才进入设置模式 `0x17`；修改后核对 ACK 及回读，完成后退出 `0x18`。

高频遥测中需持续解析，按序号和消息类型匹配，不能假设读到的下一帧就是回复。六面进行中不能退出设置，`0x18` 会取消六面校准。

## 5. AA55 协议

```text
AA 55 MSG_ID LEN SEQ PAYLOAD CRC16_L CRC16_H
```

CRC16-CCITT-FALSE，初值 `0xFFFF`，多项式 `0x1021`，覆盖 `MSG_ID` 到 `PAYLOAD`，小端附加；载荷最大 64 字节。复用 `../tools/usb_host/protocol.py` 中的 `pack_command` / `unpack_binary`，不要另写不兼容 CRC。

ACK 消息 `0x90`，4 字节：`cmd u8, status u8, detail u16 LE`。status：0 成功，1 未知命令，2 参数非法，3 执行失败。查询多数只回数据帧，没有 ACK；未知命令应显示不支持，不应当成写入成功。

| 操作 | 命令 | 回复 / 载荷 | 设置模式 |
| --- | --- | --- | --- |
| PING | `0x10` | ACK | 否 |
| 系统信息 | `0x14` | `0x05` | 否 |
| 设备配置 | `0x1F` | `0x07`，v3，28 B | 否 |
| CAN 查询 | `0x21` | `0x08`，24 B | 否 |
| 滤波查询 | `0x26` | `0x0B`，16 B | 否 |
| 融合诊断 | `0x28` | `0x0C`，60 B | 否 |
| 应用版本 | `0x23` | `0x32`，16 B | 否 |
| 启动零偏历史 | `0x33` | `0x34`，60 B | 否 |
| 静置状态 / 参数 | `0x29` / `0x2C` | `0x0D` 28 B / `0x0E` 52 B | 否 |
| 静置开始 / 取消 / 恢复 | `0x2A` / `0x2B` / `0x2D` | ACK + 对应状态 / 参数帧 | 开始、恢复需要；取消不需要 |
| 零角速阈值查询 / 写入 / 恢复 | `0x2E` / `0x2F` / `0x31` | `0x0F` 40 B；写入 20 B，恢复 2 B | 写入、恢复需要 |
| 运动零偏查询 | `0x30` | `0x09`，48 B | 否 |
| 启动设置 | `0x1E` | 3 / 5 / 7 B | 是 |
| 输出频率 | `0x1D` | u16 Hz | 否；会持久化，不用于静音 |
| 独立输出 | `0x20` | 5 B | 否 |
| CAN 设置 | `0x22` | 11 B | 是 |
| 滤波设置 | `0x27` | profile、persist，共 2 B | 是 |
| 六面开始 / 取消 / 查询 | `0x1C` / `0x25` / `0x24` | 空载荷，状态 `0x0A` | 开始需要 |
| Yaw 置零 | `0x11` | ACK | 否 |
| 进入 Bootloader | `0x16` | 仅升级流程使用 | 不用于探活 |

`0x09` 是运动零偏，不能当版本或历史。`0x12` 陀螺重标定和 `0x1B` 旧运行时 60 秒标定仍失败，后者 detail `0x0601`；新静置流程用 `0x2A`。

## 6. 配置与四档滤波

### 启动设置

`0x1E`：`mode, fast_start, apply_now`，可追加 `init_ms u16`，再追加 `range_dps u16`。mode 0 六轴、1 九轴、2 九轴相对角。init 100..60000 ms，默认 2000；量程 125/250/500/1000/2000/4000 dps。短格式保留已有时长和量程。保存后重启生效，`apply_now=1` 只发一次并等待重连。

九轴相对角只要求 Yaw 从启动参考 0 开始，Pitch / Roll 保留重力倾角。Yaw 置零只是参考变化，不是零偏校准。

快速启动开启时，按当前温度 ±5 °C 匹配历史取平均。普通采集成功追加历史；失败也优先回退温度匹配历史，无匹配则默认零偏。温度匹配不是温度补偿；当前没有完成温度补偿模型。

### 四档编号

| profile | 名称 | tauAcc | 磁航向 / 静止输出 / 运动输出时间常数 |
| --- | --- | --- | --- |
| 0 | 响应优先 | 1.0 s | 2.0 / 0.15 / 0.04 s |
| 1 | 均衡 | 2.5 s | 4.0 / 0.50 / 0.10 s |
| 2 | 静态稳定 | 4.0 s | 6.0 / 1.50 / 0.20 s |
| 3 | 零角速保持 | 2.5 s | 4.0 / 0.50 / 0.10 s |

`0x0B` v1：active/saved 在偏移 1/2，capabilities 在 3，含义是档数（旧 3、新 4）；estimator_hz u16 在 4，为 1000；tauMag/restTau float 在 8/12。不能把档数当成静置功能能力位。`0x27 [profile,persist]` 立即生效，persist 0 临时、1 保存。capabilities=3 时不能发送 profile=3。

四档 VQF 静止门限基准统一为 gyro 0.60 dps、acc 0.15 m/s²，连续 1.0 s；biasSigmaRest 0.035 dps。静置结果可覆盖允许的门限和 sigma。旧页面提示或旧交接里的 1.0/0.25、1.5/0.40 已过时。`0x0B` 两个 float 是输出时间常数，不是静止门限。

### 零角速保持

仅六轴 + profile 3 锁定发布 Yaw，Pitch/Roll 持续更新；九轴不锁磁航向。停稳约 100..200 ms 进入，明显转动后约 3..10 ms 解除，解除连续、不跳回内部角。不是等待 VQF 的 rest 标志后才锁定。很慢的转动可能被抑制，不能宣称完全保持任意慢转动。

`0x2F` 20 B：enter_dps f32@0、exit_dps f32@4、acc_dev_ms2 f32@8、enter_filter_ms u16@12、enter_confirm_ms u16@14、exit_confirm_ms u16@16、persist u8@18、reserved=0@19。

| 参数 | 编译默认 | 范围 |
| --- | --- | --- |
| enter_dps | 0.30 | 0.05..2.00 |
| exit_dps | 0.70 | 大于 enter，且 ≤5.00 |
| acc_dev_ms2 | 0.15 | 0.02..2.00 |
| enter_filter_ms | 10 | 0..200 |
| enter_confirm_ms | 50 | 0..2000 |
| exit_confirm_ms | 3 | 0..500 |

`0x0F`：version=1@0、supported@1、reserved u16@2，当前参数块 18 B@4，保存参数块 18 B@22。恢复命令 `0x31 [persist,0]`，网页恢复默认使用 persist=1，并从设备回读默认结果，不另写默认生成逻辑。写入成功须 ACK 与当前值匹配，持久化时保存值也匹配。

## 7. 静置初始化与运动零偏

静置初始化：等待连续静止 5 s，再按墙上时间采集 60 s；预稳定移动重计，120 s 无法进入采集判失败。正式采集移动立即失败，旧参数不变。通过后写 Flash 并立即应用三轴 bias、biasSigmaInit、biasSigmaRest、restThGyr、restThAcc。

`0x0D` 状态：version@0、state@1、error@2、source@3、elapsed_ms u32@4、remaining_ms u32@8、sample_count u32@12、gyro_rate_dps f32@16、acc_deviation_ms2 f32@20、temperature_c f32@24。state 0 空闲、1 等待、2 预稳定、3 采集、4 检查、5 成功、6 失败。主动状态约每 200 ms 发给发起端口，SEQ=0；不能因一次 ACK 就报告完成。

error：1 移动、2 陀螺噪声、3 加速度噪声、4 零偏超限、5 温变 ≥2 °C、6 零偏漂移、7 样本不足、8 非法数、9 Flash 写失败。

`0x0E`：version/source/cal_valid/reserved@0..3；bias_dps[3]@4；当前 sigmaInit/sigmaRest/restGyr/restAcc@16/20/24/28；default 四项@32/36/40/44；calibration_temp_c@48。source=0 时 bias 是当前 VQF 值；source=1 时是静置保存值，不能当实时值。默认值从帧读取，当前基准 0.50/0.035 dps、0.60 dps、0.15 m/s²。

静置不会使安静环境门限无限降低；当前 sigmaRest/门限下限就是编译默认值。典型安静成功结果 sigmaInit 0.10 dps，其余保持下限。不要承诺静置一定把这些值变小。

恢复 VQF 用 `0x2D`，清除静置记录、恢复 default 四项，不直接把 bias 清零。与 ZARU 恢复 `0x31` 不同。Flash 写失败保留原参数。取消 `0x2B` 不保存。

静置进行中，模式、六面、输出频率、启动配置、滤波、恢复 VQF、设置/恢复 ZARU 等写命令被拒绝，status=3、detail=`0x0703`。查询可以继续；不并行开展六面校准或升级。

运动零偏固定开启，只读。`0x30` 返回 `0x09` 48 B：version@0，motion/rest/restDetected@1/2/3；sigmaMotion@4、verticalForgetting@8、forgettingTime@12、clip@16、sigmaRest@20、tauAcc@24；实时 bias[3]@28（dps），残差模长@40（dps），zaru_hold@44，zaru_enabled@45，reserved u16@46。六轴没有绝对航向，运动估计不能完全消除 Yaw 漂移。陀螺输入不重复扣除启动零偏。

字段详见 [静置协议](host-agent-vqf-static-init.md)、[运动零偏](host-agent-motion-bias.md)、[保持阈值](host-agent-zaru-limits.md)、[保持恢复](host-agent-zaru-restore.md)。这些文档中的旧实板快照不得覆盖第 2 节。

## 8. 启动零偏历史：单位修正优先

`0x33` 空载荷表示 offset=0，或 2 B 小端 offset；成功返回 `0x34` 60 B。头部：version@0、record_version@1、corrupt@2、count@3、offset u16@4、entry_count@6、reserved@7、sequence u32@8；@12 起每条 16 B，三轴 float + 温度 float，最多 3 条。

count≤50，offset=0 是最旧。只读 entry_count 条；下一页 offset += entry_count；达到 count 或 entry_count=0 停止。翻页中 sequence 变则整表重读，最多 17 帧。网页按最新在上展示。

**`20261002c` 的历史存储/`0x34` 三轴实际单位为 rad/s，尽管字段名 `bias_dps` 和页面标题写 °/s。** 线上 `bias-history.js` 直接显示 float，尚未换算。转为 dps 应乘 `180/π`，约 57.2957795。这是 `progress.md` 明确指出的已知问题，优先于旧 `host-agent-bias-history-list.md` 的“单位 °/s”。保留原始数值与单位来源，不能对已明确是 dps 的 `0x09`、`0x0C`、`0x0E` 同样乘 57.3，也不能在固件与网页双重换算。

历史槽 `0x0803E800` / `0x0803F800`，支持旧版本 2 的 15 条记录，当前版本 3 最多 50 条。静置记录另放 `0x0803E000` / `0x0803F000`，不进入启动历史。

## 9. 六面与输出使用

六面：设置模式下空 `0x1C` 开始，`0x24` 查询，`0x25` 取消。依次摆 ±X/±Y/±Z，每面稳定约 1.5 s，以原始 g 值与已完成面判断。翻动暂停、重复面忽略。六面完成后等拟合与保存成功；失败或取消保留旧参数。不能在校准期间退出设置模式。

独立输出 `0x20`：port（0 UART、1 USB）、format（0 JustFloat、1 AA55）、mask u16、persist，共 5 B。mask bit0..8：Yaw/Pitch/Roll/Ax/Ay/Az/Gx/Gy/Gz。只传选中项，顺序固定；单位度、m/s²、dps。format=2 仅在回读表示旧预设，不可写。mask=0 只关遥测。

JustFloat 页面预设解析是 3 通道 Y/P/R，或 6 通道 Y/P/R/Gz/Az/Temp；历史六通道 Az 是 g。其他组合需接收端匹配，网页可用 AA55 自动识别。文本 `vofa`，字节 `76 6F 66 61`，不需要空格/回车，将两路当前协议改 JustFloat、保留字段、不写 Flash。

输出频率 `0x1D` 会立即应用并保存，必须整除 2000：1、2、4、5、8、10、16、20、25、40、50、80、100、125、200、250、400、500、1000、2000 Hz。只控制 USB/UART，不能用作 CAN 频率。

CAN 由串口配置，标准 11 位 ID、8 B 数据帧。`0x22` 11 B：CAN_ID u16@0、MST_ID u16@2、period_ms u16@4、baud index@6、active@7、mask@8、reserved=0@9、persist@10。ID≤0x7FF，period 1..10000 ms，active 0 请求/1 主动，mask bit0 acc/temp、bit1 gyro、bit2 Euler、bit3 quat。

CAN 每组频率=1000/period_ms，多组帧率累加；负载上限标称带宽 80%。波特率索引 0..7 为 1M/500k/400k/250k/200k/100k/50k/25k；400k 实际约 401069 bit/s。CAN 欧拉顺序 Pitch/Yaw/Roll，度；角速度 rad/s；加速度 m/s²，不能套用 UART 角速度单位。

## 10. 网页固件升级

使用“升级”页面，板上需已安装 Bootloader。仅本板应用 `.bin`，链接地址 `0x08008000`，最大 208 KiB；不能选 `.hex`、Bootloader、整片备份或压缩包。文件由浏览器本地读取。

升级顺序：连接应用、停校准、处理未应用草稿 → 选择 bin → 本地文件/CRC/SHA 检查 → `0x16` 进入 Bootloader → HELLO 核对应用分区 → 擦应用、分块传、全镜像 CRC → BOOT → 应用 PING → 成功、重连、查版本 `0x23`。

UART 用同一转接器；USB 等新枚举的原设备。保持电源，Bootloader v1 没有断电自动回滚。不能把“已发送所有字节”当成功；完成至少应有全包校验和应用响应。升级失败用页面“恢复模式”，选择当前 Bootloader 端口，完整重传。

应用区从 `0x08008000` 到 `0x0803C000`（上界不含）；后面是配置/校准区域，不能整片擦除。文档任务未刷机。将来执行 SWD 时按最新 progress：`tools/dap/dap_host_upgrade.py --flash`，4 MHz，仅应用区；不用旧 `firmware_upgrade_swd.py`。操作前依任务授权和设备身份执行备份、校验。

应用版本 `0x32`：format=1@0，length@1，char[14]@2，文本按长度且 NUL 结尾读取；当前 `20261002c`。旧固件 ACK status=1；未收到版本不意味着设备没有应用。

## 11. 工作文件与交付

| 文件 | 用途 |
| --- | --- |
| `../progress.md` | 最新进度，优先于旧快照 |
| `../upper/gyro-live/src/public/` | 本地维护网页源码；当前不修改、不部署 |
| `../upper/gyro-live/dist/public/` | 本地构建产物，不等于线上现状 |
| `../build/lsm6dsv_spi_test/release-9axis/` | 当前九轴应用构建目录；不要沿用旧 noise 路径 |
| `../output/pdf/AT32_AHRS_使用说明书_新版.tex` | 用户说明书 LaTeX 源文件 |
| `../output/pdf/AT32_AHRS_使用说明书_图示协议版.pdf` | 当前用户说明书 PDF，含截图与协议；旧新版 PDF 保留 |
| `../output/pdf/AT32_AHRS_使用说明书_预览兼容版.pdf` | 同内容的 ChatGPT/Codex 预览兼容版；可见文字为矢量轮廓，保留可搜索复制的隐藏文本、目录与链接 |
| `../tests/run_native.py` | 固件 native 回归 |

只读指南或说明书工作不修改固件、网页、配置和 progress，不触发发布。不要向用户宣称温度补偿、加密、旧陀螺重标定、断电自动回滚已实现。公开 `20261002c.bin` 是明文。

实测性能结论必须说明条件、时长、样本与统计；平滑输出、保持航向和减少传感器真实噪声是不同结果。Yaw 漂移统计先展开跨 ±180° 的角度，再估算趋势；诊断主机时间、设备时间与姿态帧时间不得混用。

## 12. 图示与协议补充（2026-10-03）

用户说明书已参考达妙 DM-IMU-L1 V1.2 的“操作截图、逐字节帧格式、寄存器与映射表”组织方式，增加线上状态、设备设置、输出、升级、六面与静置初始化共 6 张截图。截图来自未连接状态，不是实测数据。图片在 `../output/pdf/manual-assets/`；LaTeX 需连同图片编译，从工程根目录执行 XeLaTeX。本次只修改文档，没有连接实板或部署页面。

ChatGPT/Codex 内预览曾出现英文正常、中文全部消失。原文已嵌入 Fandol CFF 字体，问题疑似预览器兼容性。分发预览兼容版可绕过可见文字的字体绘制依赖，不改变排版或将页面栅格化。更新源文件并编译后，可用 `../tools/make_pdf_preview_compatible.py` 转换，参数为原 PDF、兼容 PDF 和 `--ghostscript` 可执行文件路径；依赖 pypdf 和已安装的 Ghostscript。保留原始 LaTeX 与图示协议版，勿将兼容版当作编辑源文件。

不能复制参考手册的 USB `55 AA` 帧作为本工程格式；本工程仍为 `AA 55 MSG LEN SEQ PAYLOAD CRC16_LE`，总长 `7+LEN`。SEQ=1 的版本查询完整示例：`AA 55 23 00 01 2B 03`，CRC 已由现有协议工具计算核验。

JustFloat：N 个 float32 小端 + `00 00 80 7F`，总长 `4N+4`；无通道掩码、时间戳或 CRC。自选 AA55 `0x06`：mask u16@0、timestamp u16@2、按 mask bit0..8 顺序发送 float32@4，载荷 `4+4N`，整帧 `11+4N`。旧 `0x01` 为 Roll/Pitch/Yaw 顺序，与自选 Yaw/Pitch/Roll 不同。当前旧 IMU `0x03` 预设加速度仍为 g；自选 `0x06` 才是 m/s²，以 `src/app/main.c` 的 legacy_fields 路径为准。

CAN 映射：acc ±235.2 m/s²，gyro ±34.88 rad/s，Pitch ±90°，Yaw/Roll ±180°，均 16 位；四元数 W/X/Y/Z 各 ±1、14 位。解码 `x=min+u*(max-min)/(2^bits-1)`。CAN 的映射范围不是传感器的当前硬件量程。

四元数 D0=04，D1..D7 打包连续的 56 位。先还原无符号值：

```text
W = (D1 << 6) | (D2 >> 2)
X = ((D2 & 0x03) << 12) | (D3 << 4) | (D4 >> 4)
Y = ((D4 & 0x0F) << 10) | (D5 << 2) | (D6 >> 6)
Z = ((D6 & 0x3F) << 8) | D7
```

参考手册示例 W 使用 `0xF8` 掩码会丢掉一位，应使用 `0xFC` 再右移 2 位，或直接用上述 D2 右移。不得把四元数当四个 uint16 小端。对应实现 `../src/drivers/can_protocol.c`。

例：CAN_ID=001，MST_ID=6FF，标准 DLC8 请求到 ID=001，数据 `CC 03 00 DD 00 00 00 00`；欧拉回复到 ID=6FF，数据 `03 00` + Pitch/Yaw/Roll 三组 uint16。配置寄存器 uint32、周期 ms、输出掩码可写是本工程扩展，不保证原厂配置完全兼容。
