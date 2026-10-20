# 20261017j 基线与发布范围

## 来源

- 冻结固件源码提交：`7d7a09a61c98227bda1efd44b260ee4c0a88f9c0`。
- 历史 J BIN：172,204 B，SHA256 `a5a7a67ecc84044255573c10c8222f744310d268fd86d41b776b2f26b02aaa0c`。
- 发布分支以远程 master `819c51a7e65ba4957c8616bbd6ddd73e09062aa5` 为父提交，保留其 Bootloader、上位机及许可证。
- `src/`、`inc/`、`linker/`、`middleware/`、SDK 与 `LICENSES/` 按上述冻结提交逐文件导出；原 Makefile 原样保存在 `make/firmware.mk`。
- 新增根 Makefile 和 `make/eskf8k-j.mk` 仅选择完整 J 构建参数。未移植任何后续数值候选、ABI5 或 Observer 实验。

旧源码提交属于本地研究历史。本发布不推送整条研究分支；公开文件及其哈希可通过随附 manifest 独立校验和构建。

## 构建定义

默认配置包括 `ESKF8K=1`、`ESKF_VALIDATE=1`、`SRAM_CAPTURE=1`、USB 队列修复、UART DMA/队列、`CAN_TX_FAST=1`、`VOFA_6AXIS=1` 和 `ESKF_REST_RELAX=1`，完整列表见构建 profile。

保留 J 的静置门限：0.8 °/s、0.20 m/s²。Q/R 及完整滤波运算不变。`UART_TX_QUEUE_FIX=1` 沿用 J 的温度补偿和温度学习关闭定义。

编译目标为 Cortex-M4F：`-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`，ESKF 配置有效优化级别为 `-O3`。J 使用的 GCC 版本为 7.2.1；manifest 记录本次实际工具链、源码哈希、BIN 哈希与构建结果。

## 本次验证

在隔离目录从公开源文件重新构建默认 ESKF 和 legacy 配置；默认 BIN 必须与上述历史 J 哈希完全一致才允许推送。构建不包含任何设备连接、Flash 操作或运行时控制。

源码一致性和编译通过不代替真实 MCU DWT、运行栈、采样及物理通信验证。板上当前固件未新鲜读取；不能由本发布推断现场状态。

## 尚未完成的范围

- J 的默认运行时 rest-bias 行为保留，setter 的控制路径缺陷未在本发布中修复。
- 完整双套 8 kHz 与独立 1 kHz 路径的历史验证不等于全功能生产验收。
- UART 桥接器物理接收、全组合 CAN CPU、Flash 并发、温补学习、动态与宽温精度以及长期验收仍需独立工作。
- 后续 ABI5 的严格 SOURCE 边界证明仍属于独立离线研究，不以其 FAIL 否定 J 已实现的基本输出功能，也不以 J 历史数据给新诊断镜像 PASS。
