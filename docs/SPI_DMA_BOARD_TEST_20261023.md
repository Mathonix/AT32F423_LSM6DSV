# master E：SPI DMA 实板短测

## 镜像与操作

- 源提交 `357bcebbeaa37e8b1477024e8eba4511af04890b`；APP `20261023E`。
- BIN SHA256 `03489339500aefa4469b664742e2a1f54b488d9a6f760e1584637dd90fc2ebfc`，181244B。
- 板卡序列号已脱敏（本机板卡身份不随仓库发布），VID/PID `2E3C:F401`。
- 刷前实际版本 **20261023w**，不是先前 Z。配置及模型已新鲜读取、重复校验并私有归档。
- WinUSB 最初被占用，故经 COM29 发送一次 Bootloader 进入请求，完成 APP-only 上传、每块ACK、整镜像END CRC及启动后PING/遥测验证。没有整片擦除，没有使用SWD，没有写Bootloader。
- 烧录后 COM29 被占用，重新检查 WinUSB 已可用，正式短测改用 WinUSB。没有要求必须同时释放两接口，没有停止用户程序。
- 当前仍留 E，未自动回退，未推送远程。新脚本/报告提交不会改变板端镜像。

## 实际负载边界

运行及保存算法均为 **单套 ESKF**。刷前/刷后 active_mode、saved_mode 均为 **1（九轴请求）**，本次保留用户现场设置，没有自动切到六轴。是否磁参考已锁定及实际降级路径没有专门验收，不能将这些窗口改称纯六轴对照或九轴精度通过。

master 仍有 `COV_DIV=4`，本轮没有将其改回逐帧完整协方差。因此当前 CPU 不能冒称完整逐帧协方差8kHz的成本，也不能直接与Z整体CPU作A/B。

## 实板结果

| 窗口 | DWT实测秒 | CPU busy indicated | DMA成功增量 | busy/错误/超时/溢出 | USB原始验证 |
|---|---:|---:|---:|---|---|
| 5s重测冒烟 | 5.012024 | 75.5334% | 40080 | 全0 | collector CRC0；无完整deadline原始证明，不给完整PASS |
| 20s | 20.011836 | 76.2043% | 160060 | 全0 | **FAIL：11处可观察seq8缺口，缺帧模256合计31** |
| 60s | 60.019171 | **76.1012%** | **480096** | **全0** | **PASS_LIMITED_RAW_WINDOW：60012流帧，CRC/内部字节缺口/可观察seq8缺口均0** |

DMA完成、入环、消费/预测账本在各导出快照闭合；无静默覆盖。缓冲自启动峰值11/64，成功传输 DRDY ISR入口到完成处理的自启动最大值55.2067µs，不是每窗传输最大值或纯总线时间。

20s和60s窗口内 due=generated，没有新增 age>1ms 事件。**正式测量开始前已经有1次超龄**，自启动姿态年龄最大1063.66µs；发生时没有记录足够细的事件，不能断言其来自启动/USB交接/Flash或DMA，不能宣称自启动零失约。窗口之间的未测时间不拼为连续验收。

60s原始profile分桶：

| 项目 | cycles/次均值 | µs/次均值 | 总时间比例 |
|---|---:|---:|---:|
| DRDY ISR | 436.01 | 2.907 | 2.3251% |
| DMA完成/错误ISR | 1004.19 | 6.695 | 5.3550% |
| 两类采集中断合计参考 | 1440.20 | 9.601 | 7.6801% |
| gyro包装+tracker+预测 | 2931.72 | 19.545 | 15.6339% |
| accel全部调用 | 2394.76 | 15.965 | 3.1926% |
| 姿态输出处理 | 10740.79 | 71.605 | 7.1596% |

两个采集IRQ为同抢占级、各自分桶首尾分别读取。合计是该负载参考值，不是全机互斥账本。DMA完成ISR约6.70µs，**尚未达到此前建议的5µs目标**。没有用DRDY单桶下降代替DMA总成本。

旧Z阻塞采集测得19.13µs/次、15.30%，相较此次采集中断合计有减少的迹象；两次固件/滤波/配置及接收负载不同，**未完成W→E→W受控A/B/A**，不把整机CPU差值都归因DMA。不使用PC或Unicorn时间估算CPU。

## 失败与归档修正

- 首次计划COM29 5s窗口：端口打开被拒绝，没有开始测量，保留 `smoke-5s/NOT_STARTED.json`。
- 首次WinUSB 5s：采集器收尾访问不存在的tx字段，未成功归档，保留 `winusb-5s/ARCHIVE_FAIL.json`，结论PARTIAL，不将其计为PASS；修复后另目录重测。
- 20s独立复核明确FAIL；collector旧 `success=true` 只代表当时的局部检查完成，**不等于验收**。后来脚本改为 `capture_finished`＋`acceptance=NOT_VERIFIED`，没有覆盖旧原始文件。
- 20s最初整段seq检查还包含接收交接阶段缺口。独立验证器随后明确使用首末0x83诊断回复之间的原始数据；窗口内仍有上述11处缺口，FAIL不变。未改宽验收标准掩盖缺失。
- 60s原始数据独立复算通过，不能覆盖20s失败。E没有Z版USB入队账本；缺失究竟发生在入队、端点调度、主机暂停或其他业务，当前 **NOT VERIFIED**，不能仅凭CRC0排除丢帧。
- 60s窗口在复核20s期间已经启动，现已结束并保留证据；发现USB缺口后未再延长或升级到更长测试。

## 数据保留

上传后立即CDC回复的config/加速度校准与刷前相同。最终WinUSB读回：加速度/磁校准、运行/保存算法及所有配置字段保持；Flash温度模型sequence仍326。

最初直接比较config字节得到false，原因仅为索引1是回复接口source（CDC=1，WinUSB=2）。保留原比较 `preservation.json`，独立按ABI排除source后得到 `preservation-corrected.json`。没有把来源字节差异当作用户配置变化。

保护位、完整Bootloader及私有Flash内容没有新做SWD逐字节哈希；保留结论来自APP-only地址/CRC流程和协议读回范围，不夸大为全Flash认证。

## 独立结论

| 子项 | 判定 |
|---|---|
| APP-only烧录、整镜像CRC、E启动 | PASS |
| DMA短窗传输/计数/环缓/消费 | PASS（本次静置导出账本范围） |
| CPU<85% | PASS（本次负载短窗） |
| DMA完成ISR≤5µs目标 | FAIL（均值约6.70µs） |
| 1kHz窗口内新增超龄/生成不闭合 | PASS（20s/60s无新增） |
| 自启动零失约 | FAIL（已发生1次，原因未定位） |
| USB持续零缺失 | FAIL（20s失败；60s仅独立短窗通过） |
| 校准/设置/温度模型sequence保留 | PASS（协议读回范围） |
| 全路径运行栈、独立UART/CAN、动态精度、Flash并发 | NOT VERIFIED |

总体 **CONDITIONAL PASS：DMA采集及当前CPU短测可用，完整通信/实时性验收未完成**，不宣称生产通过。

## 证据和复现

所有原始窗口位于 `build/spi-dma-board-test/`，含 private RAM快照，不提交私有文件。新manifest索引全部原始证据和镜像哈希。

```powershell
python tools/serial/test_spi_dma_hardware.py --transport winusb --seconds 20 --out build/NEW-window
python tools/serial/verify_spi_dma_hardware.py build/NEW-window
```

Collector只读，不切模式、不保存、不复位。正式Verifier从原始TX确认命令白名单，自己复算CRC、字节连续性、seq8、DMA恒等式、runtime、DWT和deadline增量，不信collector的PASS字段。seq8仍不能证明绝对无整256帧丢失；没有端点入队账本和严格SOURCE时界，本轮保留这些限制。
