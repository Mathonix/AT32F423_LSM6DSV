# SPI DMA 候选（仅离线，待用户随后烧录）

## master 集成补记

用户随后授权将 SPI DMA 改动合入 master。本次集成基于 `ef423a9a57af8d1cb2fa8b3a393ec47b5841299c`，版本单独标记为 **20261023E**，不是原 D 镜像。

只合入 DMA 驱动、诊断查询、perf bucket15 和相关测试。master 既有静置学习、journal保存策略、磁场回退灯效、Q/R、加速度均值/门限、`COV_DIV=4` 配置全部保留，未引入 Z 分支的其他 USB/栈/上位机修改。下文针对 D/Z 的原结论和回退说明仅作为历史证据，**不自动适用于 master E**。

E 完整 APP 构建和实际驱动 native 回归通过，`src/fusion`、`inc/fusion` 与 `eskf8k_config.h` 相对集成前无改动。ELF/map/BIN 位于 `build/spi-dma-master/`；BIN SHA256 为 `03489339500aefa4469b664742e2a1f54b488d9a6f760e1584637dd90fc2ebfc`。本次没有烧录或操作板卡。

E map：`_edata=0x20001bc0`，`_ebss=0x2000769c`，heap预留末 `0x200078a0`，栈顶 `0x2000bf00`，最小 heap/stack 预留未改变。E 的 CPU、物理 SPI DMA 时序、采集/输出连续性、Flash并发及全功能负载均仍为 **NOT VERIFIED**。特别是 master 原有 journal保存行为没有 SRAM Flash-busy采集保证，不能根据 DMA 改动宣称并发保存无丢样。

master 复现：`mingw32-make -j8 BUILD=build/spi-dma-master`，`python tests/test_spi_dma_capture.py`。

---

分支 `codex/eskf8k-spi-dma`；起点 Z 对应提交 `e914198d80b719409387978e956f0a9c6641525d`。
版本 `20261023D`（大写 D），与 Z/其他小写版本区分。本轮没有连接板卡或烧录。

选择 Z 基线是为了保留已验证 USB 修复、完整逐帧协方差传播和独立 1kHz 输出。当前主目录 master 的协方差降频改动没有混入此候选。原工作区上位机未提交修改保留，未纳入 DMA 提交。

## 采集实现

- SPI1 保持原 16bit 帧和时钟分频；不提高 SPI 频率。
- DMA1 ch2 接 SPI1 RX，ch3 接 SPI1 TX；UART 原 DMA1 ch1 保留。已检索本 APP 无 ch2/3 其他拥有者。
- DRDY EXINT0 保存 ISR 入口 DWT 时间，先使能 RX DMA，再 TX 产生时钟；不再调用阻塞 read_raw。启动前清锁存 DRDY 的一次 read_raw 仍保留。
- 传输 8 个半字：命令 0xA000 + 7 个零；与旧驱动一样发送 16 字节、保留其中 14 字节温度/gyro/acc payload，忽略首个命令回复及末尾多余字节。
- RX TC 确认 TX 完成和 SPI 状态后，停止 DMA、释放 CS，解析并发布一个带原 DRDY ISR 时间的样本。完整 payload 解码覆盖正负边界值。
- 保留原 64 帧 SPSC 环缓及发布 DMB。未增大缓冲、未改 ESKF、Q/R、静置阈值、观测频率和姿态调度。
- TX 仅开传输错误 IRQ；RX 开 TC/错误 IRQ。两个 DMA IRQ 与 EXINT0 使用相同抢占优先级，避免其间 producer 并发。前台停止/快照短 PRIMASK 临界区；不把 volatile 当作多字段原子性。
- RX 数据为专属 volatile SRAM buffer；TX 也在 SRAM。完成发布后才重新允许下一次 DMA。

## 故障行为

- DRDY 到达但上一传输未完成：增加 busy 拒绝计数，不覆盖 buffer/源时间，不伪造旧样本。
- 下一 DRDY 看到前传输年龄≥100µs时，计 timeout 并中止，当前 DRDY 仍记 busy 拒绝。**这是下一 DRDY 检查，不是独立100µs硬件定时器**；没有后续 DRDY 的永久故障须由后续实板故障恢复验收处理，不能宣称有独立看门狗。
- RX TC 后最多等待 SPI busy 尾部2µs，超过即中止；不是等待完整传输。
- DMA error、SPI overrun、TX TC 缺失、启动时 SPI 非空都计错并丢弃，不提交不完整数据。
- abort 停 DMA 请求/通道，停 SPI、抬 CS、清 RX/overrun，再恢复 SPI；这一路的真实硅片恢复行为仍待实测。
- 缓冲满：完整传输计 completed，增加原 overrun，不覆盖未消费样本。
- stop 关闭 EXINT/DMA IRQ、请求/通道和 pending IRQ；中止在途记录 stop_abort。start 开新采集纪元并重置计数。
- 保留 perf IMU 桶用于 DRDY；新增末尾桶15（PERF_IMU_DMA）计两个 DMA ISR。比较 CPU 时必须同时包含二者，不用 DRDY 一项降低冒充总成本下降。

## 只读诊断

空请求 `0x82` → `0x83`，64B，16个LE u32。错误长度拒绝。无保存/控制副作用。

| 索引 | 内容 |
|---:|---|
|0|ABI=1|
|1/2|active / DMA busy|
|3/4/5/6|DRDY ISR / DMA started / 成功completed / ring published|
|7/8/9|busy拒绝 / DMA或结束状态错误 / timeout|
|10/11|ring overflow / ring最大深度|
|12|成功传输 DRDY入口→完成处理的最大cycles|
|13/14/15|stop_abort / SPI启动非空拒绝 / consumed|

同一纪元按u32模运算核对：

```
DRDY = started + busy_reject + spi_not_idle
started = completed + dma_error + timeout + stop_abort + in_flight
completed = published + overflow
published - consumed = ring_depth
```

旧 runtime SPI error 包括 DMA/timeout/SPI-not-idle，busy与ring-full另外记录。源时间是 ISR 读数，不能称硬件物理边沿；未增加传感器重复样本识别能力，也未解决严格 SOURCE 时界证明。

## 离线结果

- 完整 APP ARM hard-float 链接通过，产物 `build/spi-dma-candidate/lsm6dsv_spi_test.{bin,elf,map,hex}`。
- map：`.data` 6864B，`.bss` 23304B，`_ebss=0x200075d8`，最小 heap末 `0x200077d8`；相对 Z 静态 RAM增加96B。原 heap/stack预留不变。ELF size 工具将 SRAM 热代码单独计入 text，不能直接用其 data/bss相加替代 map分区。
- `tests/test_spi_dma_capture.py` 编译**实际驱动**加寄存器假对象，覆盖解码、完成前不可见、时间戳保持、busy、无TC、错误、超时、满环、u32回绕、旧IRQ、stop/restart、PRIMASK恢复。PASS。假对象不验证 DMA/SPI 电气时序、SDK硬件效果或 ISR 周期。
- 真实候选 ARM ELF 与 Z ELF 的4×8192样本 Unicorn 比较：six完整状态、P/L、残余bias与rest tracker逐样本逐位一致，首差异null。PASS（限定模拟浮点定义域，非实机精度或CPU）。
- 未改变 `src/fusion`/`inc/fusion`/`eskf8k_config.h`。
- 编译仍有基线的 dt_cy/未使用label/旧quat_mul警告；不能声称零警告。

## 后续上板入口（本轮不执行）

用户随后提出烧录测试后：先新鲜读取固件/模式/pending及RAM模型；如需复位，保留模型快照，APP-only更新，保护Bootloader、设置、校准和温度模型。保留精确 Z 回退 BIN，SHA256 `8f5728daa88733a469d46c882f58cf95d34635f5290724a5a9cee4dec3208788`。

先2–5秒验证诊断恒等式、8k消费/预测、CS和DMA是否退出，再20秒、60秒（每组≤5分钟）。核对 DRDY+DMA 总CPU、完整gyro/acc/pose负载、缓冲/错误、1kHz deadline和年龄、双USB同源账本及栈水位。任何新增采样错误、busy、溢出、持续积压、USB拒绝或deadline异常立即停止升级，保留失败原始数据。CPU收益、物理采集连续性及电气正确性均为 **NOT VERIFIED**。

本候选没有 SRAM 向量表/完整 SRAM中断调用链证明，不能用于宣称 Flash busy期间连续采集。不要直接把旧 Flash/SRAM实验结果移植为本版PASS。

## 复现

```
mingw32-make -j8 BUILD=build/spi-dma-candidate
python tests/test_spi_dma_capture.py
python tests/compare_arm_hot_placement.py build/closure-z/lsm6dsv_spi_test.elf build/spi-dma-candidate/lsm6dsv_spi_test.elf --out build/spi-dma-candidate/arm-parity.json
```

总体：**PASS OFFLINE / NOT VERIFIED ON SILICON**。仅准备待烧录候选，不标生产可用。
