# AT32F423 + LSM6DSV 工程审查与改进建议

审查日期：2026-09-23。范围：本仓库的应用固件（`src/`、`inc/`、`Makefile`、`linker/`）；第三方 AT32 外设库、上游 VQF 和上位机源码未做逐行审计。以下是**静态代码审查**，未连接硬件验证时序/姿态精度；“风险”不等同于已在板上复现。下文保留初次审查的原始问题与建议；已完成的修复请优先查看“修复进度”。

## 修复进度（2026-09-23）

> 本节记录审查后的代码变更。下文问题描述与行号为修复前快照，不代表当前代码状态。未进行上板验证，不要把“已修复”理解为量产验收。

| 项目 | 状态 | 实施内容 / 待验证 |
| --- | --- | --- |
| P0-0 BIN_IMU 越界 | 已修复，待上板 | 双缓冲使用最大帧容量，协议打包校验容量；新增 `tests/test_protocol.c` 校验长度、哨兵、CRC、帧解析及输出模式。 |
| P0-1 ACK 栈缓冲交给 DMA | 已修复，待压测 | UART 持久化 8 槽控制队列，控制回复优先；UART/USB 入队失败计数 `protocol_reply_drops_uart`、`protocol_reply_drops_usb`。队列满时仍会丢回复，主机需重试。 |
| P0-2 普通复位进入 Bootloader | 已修复，待上板 | 普通复位不写启动魔数，仅升级命令写入；回复成功排队后再复位，等待 UART/USB 发送完毕，最多 100 ms。旧版四字节复位仍可用。 |
| P0-3 主循环同步 Flash 写 | 部分缓解 | 记录写入耗时/次数、重置采样时间基准并计入估计丢样；**擦写依然同步**，不能保证 2 kHz 无间断。需设计维护窗口或非阻塞写入并实测延迟。 |
| P0-4 `fusion_n` 每秒归零 | 已修复，待上板 | 使用独立秒窗口统计输出频率，单调采样计数不再清零。 |
| P1 UART RX 积压 | 部分修复 | UART 中断 + 1024 字节环形缓冲，协议处理 UART 每轮 128 字节 / USB 每轮 64 字节，增加溢出与丢字节计数；需通信压力测试。 |
| P2-10 构建隔离 | 已修复 | 默认 `DEBUG_BUILD=0`，目标、debug/release、6/9 轴隔离产物目录。 |
| P2-11 参数存储掉电 | 双槽实现，待断电测试 | 保留旧槽 `0x0803D800`，新增槽 `0x0803D000`，交替写、CRC 校验、序号恢复，并编译检查地址。依据仓库内《AT32F423 Reference Manual Rev 2.03》第 2.2 节表 2-1，256 KB 型号每扇区 2 KiB；链接应用区结束于 `0x0803C000`。须确认实板芯片容量，完成断电/旧版数据升级测试后再刷量产设备。 |
| Bootloader 漏擦扇区（追加发现） | 已修复，待上板 | 原 `BL_SECTOR_SIZE=0x1000` 用作逐扇区擦除的步幅，但 `flash_sector_erase` 一次只擦 2 KiB，导致隔一扇区漏擦；已改为 `0x800`。须实测整片升级及旧固件覆盖。 |
| P1 软件 I²C、WS2812 时延，P2-12 模块拆分 | 未实施 | 待排期与实测。 |

**验证：** `make DEBUG_BUILD={0,1} SIX_AXIS={0,1} all` 四种组合、`make -C bootloader -B all` 编译链接通过；`gcc -std=c11 -Wall -Wextra -Werror -Iinc/telemetry tests/test_protocol.c src/drivers/protocol.c -o test_protocol` 并运行通过；`git diff --check` 通过。固件仍有 `main.c` 未使用变量/函数告警；Bootloader 有 newlib `_close/_lseek/_read/_write` 未实现的链接器提示。**未进行上板、并发通信或断电恢复测试。**
## 现状概览

- 主链路：LSM6DSV 2 kHz 采样 → 可选温补/低通 → Full VQF → UART DMA / USB CDC / CAN 输出；IST8310 以约 50 Hz 读取并按配置约 10 Hz 更新磁融合。
- 已有的可取设计：UART 姿态数据双缓冲、USB 收发环形缓存、传感器连续错误后的重初始化、VQF 实时状态及丢样计数、磁力计测量与读取分步调度、陀螺零偏历史双槽交替写入。
- 仓库目前**有未提交变更**，包括 `.gitignore` 改动、多个大文档/资料与 `upper/Motion_Studio/` 文件的删除。此次审查未更动这些内容；后续整理仓库前应先确认这些删除是否有意为之。

## P0：优先修正的正确性/可靠性问题

### 0. 切换至 BIN_IMU 模式会越界写入发送缓冲区

**证据：** `src/app/main.c:109-112` 定义 `VOFA_MAX_BYTES = 6*4+4 = 28`，并分配 `vofa_dma[2][28]`；`src/app/main.c:917-923` 的 BIN_IMU 模式却向该缓冲区写入 `protocol_pack_imu()` 帧。`inc/telemetry/protocol.h:88-98` 的 IMU 载荷为 6 个 float、1 个 int16 温度和 1 个 uint16 时间戳，共 **28 字节**；再加 `inc/telemetry/protocol.h:15-18` 的 **7 字节**帧开销，实际写入 **35 字节**。`src/drivers/protocol.c:158-177` 的打包函数只有最大载荷检查，完全不知道调用方的缓冲区容量。

**影响：** 只要收到 `SET_STREAM_MODE=BIN_IMU` 并开始输出，就可能每帧越过缓冲区尾部写入 7 字节，破坏相邻缓冲区/全局状态，导致帧损坏、随机异常甚至固件失效。属于无需高负载即可触发的内存安全问题。

**建议：** 立即将发送双缓冲容量提高至至少 `AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_imu_t)`，更稳妥是按所有模式最大帧长度定义；给 `protocol_pack_frame` 增加显式 `buf_capacity` 参数并在写入前拒绝溢出，所有调用点更新；添加 `_Static_assert`/C++ `static_assert` 及各流模式打包长度的主机测试。验收包括全部模式切换、帧 CRC/长度和相邻缓冲哨兵检查。
### 1. UART 协议回复把栈缓冲交给异步 DMA

**证据：** `src/app/main.c:170-183` 的 `protocol_reply_ack()` 在栈上定义 `frame[]`，并调用 `protocol_send_frame()`；`src/app/main.c:272-277` 的状态查询也使用局部 `frame[]`。UART 路径直接调用 `uart_dma_send(frame, len)`；`src/bsp/bsp.c:304-324` 把指针设置为 DMA 内存地址，并未复制数据。

**影响：** 函数返回后栈空间可被复用，DMA 尚未传输的回复帧可能被覆盖，出现间歇性 CRC 错误/错包；DMA 正忙时返回值也被忽略，ACK 可直接丢失。

**建议：** 将 UART 控制回复放入持久化 TX 队列/固定缓冲区，发送完成再释放；为控制帧预留优先级，区分“排队成功”和“真正发完”；为 UART/USB 发送失败分别记录计数。不要简单地把 `frame` 改成一个共享 `static` 数组——前一笔 DMA 未结束时仍可能被下一条命令覆盖。测试用连续 Ping、状态查询和 1 kHz 流并发，校验每条回复 CRC 和序号。

### 2. 系统复位命令与进入 Bootloader 命令未区分

**证据：** `src/app/main.c:366-376` 将 `AHRS_CMD_SYSTEM_RESET` 与 `AHRS_CMD_ENTER_BOOTLOADER` 统一设置 `protocol_reset_pending=1`；`src/app/main.c:1034-1036,1562-1566` 对所有待复位情况调用 `app_request_bootloader()`，先写启动请求魔数再复位。旧版四字节复位命令在 `src/app/main.c:186-205` 也走同一路径。

**影响：** 普通复位（包括设置后的立即应用）可能实际进入 Bootloader，而不是重新启动应用。另一个问题是 ACK 排队后立即重启，并未等待 UART DMA/USB 发送完成，主机可能收不到成功回复。

**建议：** 将请求类型显式区分为 `RESET_APP` / `ENTER_BOOTLOADER`；普通复位只执行复位，只有升级命令写启动魔数。复位前等待回复发完或设有限超时；在三种命令入口分别做上板回归。

### 3. 在 2 kHz 主循环中同步擦写 Flash

**证据：** `src/app/main.c:298-321` 的设置命令直接保存 Flash；快速启动后台修正路径在 `src/app/main.c:1332-1344` 直接调用 `gyro_bias_history_save_at_temp()`。后者在 `src/calibration/gyro_bias_history.c:192-204` 擦除扇区并逐字写入；设置保存也在 `src/calibration/fusion_settings.c:57-84` 同步擦写。

**影响：** Flash 操作跨越传感器采样周期时会产生丢样/输出中断；需要实测中断及代码从 Flash 执行期间的行为。快速启动路径受 `DEBUG_BUILD` 控制，但当前 `Makefile:94` 默认 `DEBUG_BUILD=1`。

**建议：** 设置命令先进入暂停采样/维护状态，向主机明确提示采样暂停，然后写入并重新初始化定时基准；后台零偏写入可先缓存在 RAM，待退出运行或显式维护窗口再提交。至少记录擦写最大耗时、前后 `skip_n`、USB/CAN 服务中断及供电中断恢复行为。

### 4. `fusion_n` 同时充当统计值与周期调度时基

**证据：** `src/app/main.c:1365-1371` 用 `fusion_n % MAG_PERIOD_N` 触发磁力计并记录 `mag_start_n`；`src/app/main.c:1418-1421` 用两个计数差判断超时；`src/app/main.c:1427` 用它判断输出分频；但 `src/app/main.c:1623-1635` 每约 1 秒将 `fusion_n` 清零。

**影响：** 磁力计事务跨越秒边界时，`fusion_n - mag_start_n` 发生无符号下溢，可能立即判为超时。非 2000 次/秒或更改输出分频时，清零还可能造成调度相位跳变；`fusion_hz` 实际上是自上次清零以来的样本数，在延迟超过 1 秒后不能直接代表 Hz。

**建议：** 拆成永不主动清零的 `sample_seq`（允许自然回绕；超时使用无符号差）与每秒统计窗口的 `sample_count_window`；磁力计超时可直接用 DWT 或毫秒时戳。以人为注入的秒边界跨越、磁传感器超时、运行时输出分频改变进行验证。

## P1：实时性、故障恢复与通信质量

### 5. 单次异常/超长间隔后的服务任务可能得不到执行

`src/app/main.c:1234-1260` 对等待超时、SPI 失败进入 `recover_or_continue`，对采样间隔超过 `DROP_DT_CYCLES` 则直接 `continue`。两条路径都跳过本轮的 USB 命令处理、CAN、LED、每秒统计；连续超长间隔还不会累计 `err_streak`。可将“每轮都需执行”的非采样服务抽为统一尾部，并针对 `dt` 异常设置单独计数/有限恢复策略。注意异常时不能把陈旧数据送进 VQF。

### 6. UART 接收仅轮询单字节硬件缓冲，压力下容易丢命令

`src/bsp/bsp.c:332-344` 通过 `USART_RDBF_FLAG` 逐字读取；`src/app/main.c:1551-1560` 只在每次成功采样后的尾部清空缓存。Flash 写入、磁力计软件 I²C、USB/LED 占用主循环时存在接收溢出风险；没有看到对应的 UART RX DMA 环形缓冲或显式溢出计数。建议改为 RX DMA circular + 帧解析限额，采集 ORE/丢字节/CRC 计数，用持续姿态流与突发命令交织压测。

### 7. 输入解析和输出缺少预算/背压指标

`src/app/main.c:1551-1560` 两个 `while` 没有本轮字节数/耗时上限，主机持续发数据可占用采样预算。`src/app/main.c:926-932` 忽略 USB 写入失败，UART 仅增加 `vofa_late`；`src/drivers/usb_cdc.c:145-155` 在 TX 环满时直接拒绝新帧。建议控制每轮最大解析字节数，完整帧优先/过期姿态帧丢弃，增加 UART/USB 独立的排队/丢帧/峰值耗时计数。UART 控制回复应优先于高频遥测。

### 8. 软件 I²C 需测算对 500 µs 周期的最坏占用

`src/drivers/ist8310.c:21-32` 的 I²C 基于逐位 GPIO + `delay_us()` 忙等待，触发测量和读取虽拆分调度，但每个事务仍同步阻塞主循环；`src/app/main.c:1363-1415` 随采样周期触发。建议用 DWT 抽样统计磁力计触发/读取耗时和全循环 p99/p99.9、`skip_n` 增量；若超过预算，再考虑硬件 I²C/DMA 或降低轮询频率。**不建议未经测量就改掉已工作的总线时序。**

### 9. LED 输出短时关闭所有中断

`src/bsp/ws2812.c:40-65` 写 24 bit 前关闭中断，约 30 µs 的线速发送期间 SysTick/USB/CAN ISR 均不能运行；本身短于 500 µs 采样周期，但可能影响通信时延。先测 ISR 最大延迟；若压测确有问题，再考虑定时器 + DMA 或限制刷新频率。

## P2：配置、维护与验证体系

### 10. 默认构建模式与“常规量产固件”的说明不一致

`Makefile:92-98` 说明量产应关闭快启，但 `DEBUG_BUILD ?= 1` 默认打开；而 `inc/app/app_config.h` 未经编译覆盖时默认关闭。建议让默认构建指向预期发布行为（例如默认 `DEBUG_BUILD=0`），把 `debug`/`release` 明确分目标或构建目录，构建产物嵌入模式/版本标识。要注意切换 `DEBUG_BUILD`/`SIX_AXIS` 时，同一 `build/` 中的 `.o` 不会仅因编译参数改变而自动重编译；暂时必须 `make -B ...` 或 clean，长期宜以配置隔离输出目录。

### 11. 设置存储为单扇区覆盖写入

`src/calibration/fusion_settings.c:57-84` 擦除唯一的设置扇区后再写入；写入中途断电会丢失上次设置，`sequence=1` 没有发挥版本轮换用途。建议参照陀螺零偏的双槽方案，先写备用槽、验证 CRC/序号后切换，并定义断电后恢复策略。调整地址前必须核对链接脚本、应用镜像尺寸、Bootloader 擦除范围和已有设备升级兼容性。

### 12. 模块边界与自动化回归

`src/app/main.c` 约 1648 行，协议命令、采样调度、校准、VQF 输出滤波与遥测耦合。可按 `sensor_task` / `fusion_task` / `command_task` / `output_task` / `storage_service` 拆分，同时将 `millis()`、DWT 和 Flash 操作抽成可模拟接口。建议先覆盖：CRC/分包/错误帧、重启与 Bootloader 指令区别、UART DMA 缓冲生命周期、跨秒磁超时、Flash 断电模拟、2 kHz 调度延迟。现有 `src/diagnostics/` 可用于上板冒烟测试。

## 推荐实施顺序与验收

1. **先修 P0-0/P0-1/P0-2：** 全部输出模式均不越界、打包帧长及 CRC 正确；所有命令 ACK 在连续高频姿态流下 CRC 正确、无丢回复；普通重启仍进入应用，升级命令才进入 Bootloader；重启之前主机收到 ACK。
2. **再修 P0-4/P0-3：** 跨秒磁力计不出现误超时；保存参数/零偏时有明确维护状态、可测丢样和断电回退行为。
3. **随后处理 P1：** 输出 `skip_n`、UART/USB 丢包、磁力计事务与主循环最大耗时；目标是对连续运行的 2 kHz 有实测证据，而非只凭编译通过。
4. **最后处理 P2：** 独立的 debug/release 构建目录、协议/时间调度主机单元测试、量产烧录及升级流程回归。

> 验证说明：以上为源代码审查，未进行硬件测试或重新刷写设备；文中的性能影响需在目标板上用 DWT/逻辑分析仪和通信压力测试定量确认。
