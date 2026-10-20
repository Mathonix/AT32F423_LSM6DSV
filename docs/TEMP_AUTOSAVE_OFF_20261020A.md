# 20261020a：关闭启动零偏进入温度模型及自动保存

## 改动范围

基于公开 J 提交 `2f9ca81118b3a1fec85a354426c019358d2e6c06`，新增独立构建策略：

- `STARTUP_BIAS_SAVE=0` → `APP_GYR_STARTUP_BIAS_SAVE_ENABLE=0`。
- `TEMP_FLASH_AUTO_SAVE=0` → `APP_TEMP_FLASH_AUTO_SAVE_ENABLE=0`。
- J 原有温度补偿及温度学习关闭定义继续生效。

成功的启动静置校准仍沿原路径传入 `vqf_prime_rest(acc_avg, gyr_bias)`，用于本次运行。保存 helper 在新 profile 中直接返回，不调用 `gyro_bias_history_save_at_temp()`，因此不会把新启动零偏放入 V4 的 RAM fallback，也不会为其产生 dirty/pending。

温度表自动保存条件独立关闭。显式温度控制协议保持原定义，已有 Flash 记录可读取；本改动不发送保存、清空或维护命令。不修改加速度/磁校准、Q/R、静置门限、双套 ESKF 或姿态生成。

手动温度表保存/清除仍属于原协议的显式持久化操作，不应被误当作无 Flash 的运行时控制。此版本没有清除这些接口，也未在板上调用它们。

## 离线验证

- 完整 APP 编译 PASS，BIN 171,780 B，SHA256 `09962f1e265b7d67ae0c597f29bb3394e4a8446f73d18f43d3f70c2b7af7c951`。
- 从真实源码抽取 helper 与环境服务，执行新策略、legacy 默认及 protected updater 三组副作用测试，全部 PASS。新策略即使模拟运行两小时，也不调用启动保存或自动保存资格检查。
- 兼容 `PROFILE=eskf8k-j` 复建 BIN 为 `a5a7a67ecc84044255573c10c8222f744310d268fd86d41b776b2f26b02aaa0c`，与历史 J 完全一致。
- `eskf8k.o`、`vqf_wrapper.o`、`imu8k_capture.o`、`sram_capture.o`、`attitude_output.o` 的已分配代码/常量段均与兼容 J 构建逐字节相同。
- SRAM 布局不增加：`_ebss=0x2000B0E4`，`__heap_limit=0x2000B2E8`，`_estack=0x2000BF00`，保留 3,096 B 静态主栈空间与 3,072 B 最小栈预留。main 静态帧 688 B。实际栈高水位和新镜像 CPU 仍为 NOT VERIFIED。

这不是新镜像的实机 PASS，也不代替 USB、CAN、UART 或长期稳定性验收。

## 精确镜像上板审查

本轮已通过 SWD 只读确认现场仍为精确 J，4 秒启动窗口、pending settings=0、temperature pending=1。待存模型及保留区已导出至本地私有目录；私有字节不在仓库内。待存内容与 Flash 模型相比只有 fallback 温度/零偏字段变化，原温度节点未变。

按照用户此前要求，新的精确镜像须获得单独明确授权后才可烧录。本报告不构成烧录授权。

未来授权方案：仅通过 SWD 更新本 APP，地址 `0x08008000`，保护 `0x08000000..0x08007FFF` 与 `0x0803C000..0x0803FFFF`，不执行整片擦除、不改保护位、不进入 MAINTENANCE、不保存当前 RAM 模型。旧 RAM 快照保留在电脑。APP 更新后，沿用已保存的 4 秒窗口做一次静置启动校准。

先重新核对镜像、pending、备份及保护区，再更新 APP；完成后检查新 BIN 读回、Bootloader/保留区/保护位哈希、启动结果必须为 FRESH、采样数及静止拒绝原因、加速度/磁校准一致，并观察最多 60 秒的采样、双套 ESKF、1 kHz 生成与温度 dirty/pending 状态。任何异常停止，不重试 Flash 或控制操作。精确 J BIN 和备份始终保留，回退需满足当时授权及安全条件。

普通静置校准是当前温度下的三轴偏置估计，不是比例因子、宽温标定或六轴绝对航向校准。
