# 项目工作进度报告 (Progress Report)

> **记录时间**：2026-09-14  
> **当前分支**：`master`  
> **远端同步基线**：`e2e9834 docs: document project path migration`  
> **工作区状态**：正在进行通信协议子系统完善与软硬件接口联调

---

## 1. 总体进度概览

本项目是基于 **AT32F423KCU7-4** 主控与 **LSM6DSV (SPI)** + **IST8310 (I2C)** 的 2 kHz 高速高精度嵌入式姿态解算系统。  
当前阶段已全面完成工程结构规范化重构、文档重写与全量构建验证；目前正在系统化**完善多通道通信协议**（包含通用二进制标准姿态协议、VOFA+ 兼容流、CAN 2.0B AHRS 广播与指令控制、USB CDC 虚拟串口全双工收发）。

---

## 2. 前序已归档交付成果（已合并至 master 并推送）

1. **工程结构规范化重构（5 层架构）**：
   - `src/`：分设 `app/`（应用主逻辑）、`bsp/`（板级支持/时钟/中断）、`drivers/`（外设与传感器驱动）、`fusion/`（VQF 算法核心与封包）、`diagnostics/`（诊断固件入口）。
   - `inc/`：按 `app/`、`bsp/`、`calibration/`、`config/`、`diagnostics/`、`drivers/`、`fusion/`、`telemetry/` 职责清晰分层。
   - `tools/`：归类为 `dap/`（DAPLink 在线调试采集）、`serial/`（串口/VOFA 工具）、`analysis/`（离线拟合分析）、`legacy/`（历史脚本）。
   - `docs/`：分类为 `calibration/`、`validation/`、`review/`、`datasheets/`。
   - `reference/`：外部参考隔离为 `st/`（官方传感器库）与 `usb_hick/`（USB 时钟参考工程）。
2. **构建系统无缝适配**：
   - `Makefile`：适配全新相对路径与 include 目录，支持默认主固件及 6 类专用诊断固件。
   - `mdk_v5/lsm6dsv_spi_test.uvprojx`：全面校验修复 24 个文件引用路径。
3. **全量构建验证**：
   - 默认固件及 6 个独立诊断固件全部编译通过，7 份日志均为 `warnings=0, errors=0`。
   - Python 工具包经 `compileall` 验证无语法异常。
4. **交付文档**：
   - `README.md`：按中文工业级规范重写，附录详细项目状态。
   - `docs/review/file_path_migration_20260914.md`：详尽记录 89 处路径重命名与 13 处新增文件清单。

---

## 3. 当前任务进展：通信协议完善 (In Progress)

针对原本仅有单向 3 通道 VOFA JustFloat 且 CAN 仅发送固定计数测试包的缺陷，正在构建多链路、双向控制、高可靠度的通用 AHRS 通信协议体系：

### 3.1 已完成落地的核心代码模块

1. **统一标准二进制协议规范与定义 (`inc/telemetry/protocol.h`)**：
   - 帧头：`0xAA 0x55`（双字节同步头）。
   - 帧结构：`[SYNC1(1B) | SYNC2(1B) | MSG_ID(1B) | LEN(1B) | SEQ(1B) | PAYLOAD(0~64B) | CRC16_L(1B) | CRC16_H(1B)]`。
   - 下行遥测帧定义：
     - `AHRS_MSG_ATTITUDE_EULER (0x01)`：欧拉角（Roll/Pitch/Yaw 单精度浮点，单位度）、状态位（静止/磁有效/标定完成/异常）、时间戳。
     - `AHRS_MSG_QUATERNION (0x02)`：四元数（Qw, Qx, Qy, Qz）、时间戳。
     - `AHRS_MSG_IMU_RAW (0x03)`：三轴角速度、三轴线加速度、传感器温度、时间戳。
     - `AHRS_MSG_COMPACT (0x04)`：12 字节紧凑低延迟帧（定点化姿态角 0.01°、Gz、状态位）。
     - `AHRS_MSG_SYSTEM_INFO (0x05)`：融合频率、输出频率、跳帧计数、温度、当前流模式、CAN 状态。
     - `AHRS_MSG_ACK (0x90)`：通用指令应答包（含命令号、执行状态、结果详情）。
   - 上行控制命令定义：
     - `AHRS_CMD_PING (0x10)`：心跳探测。
     - `AHRS_CMD_ZERO_YAW (0x11)`：当前航向角重设为参考零位。
     - `AHRS_CMD_RECALIBRATE_GYRO (0x12)`：在线触发陀螺仪静止重新校准。
     - `AHRS_CMD_SET_STREAM_MODE (0x13)`：动态在线无感切换数据流模式。
     - `AHRS_CMD_QUERY_STATUS (0x14)`：查询系统当前健康状态。
     - `AHRS_CMD_SYSTEM_RESET (0x15)`：远程复位 MCU。
   - 流模式枚举：
     - `STREAM_MODE_VOFA_3CH (0)`：经典 VOFA+ 3 通道 JustFloat（无缝兼容现有上位机）。
     - `STREAM_MODE_BIN_ATT (1)`：标准二进制欧拉角姿态包。
     - `STREAM_MODE_BIN_COMPACT (2)`：高频紧凑定点姿态包。
     - `STREAM_MODE_BIN_IMU (3)`：全传感器 9 轴融合数据包。
     - `STREAM_MODE_VOFA_6CH (4)`：VOFA+ 扩展 6 通道（Yaw/Pitch/Roll/Gz/Az/Temp）。

2. **通信协议解析与封包引擎 (`src/drivers/protocol.c`)**：
   - 算法：标准 CRC16-CCITT（多项式 `0x1021`，初始值 `0xFFFF`），经位运算优化，耗时极低。
   - 封包函数：提供各类姿态、四元数、传感器、系统状态与 ACK 应答封包接口。
   - 解析器：实现容错状态机 `protocol_parser_feed_byte`，支持坏字节快速重同步与帧回调触发。

3. **板载串口驱动完善 (`inc/bsp/bsp.h`, `src/bsp/bsp.c`)**：
   - 增加非阻塞接收接口 `uart_read_byte(uint8_t *ch)`，直接查询 `USART_RDBF_FLAG` 并读取硬件数据寄存器，无缝接入协议状态机。

4. **USB CDC 虚拟串口双向驱动完善 (`inc/drivers/usb_cdc.h`, `src/drivers/usb_cdc.c`)**：
   - 在 EP2 OUT 端点增加 256 字节单写单读无锁环形缓冲区 (`rx_ring`)。
   - 提供 `usb_cdc_read_byte(uint8_t *ch)` 与 `usb_cdc_available()` 接口，实现主机到下位机的高速双向数据透传。

5. **CAN 2.0B 工业总线协议重构 (`inc/drivers/can_test.h`, `src/drivers/can_test.c`)**：
   - 将原单一测试计数包升级为完整的工业 AHRS 节点协议：
     - **姿态广播帧 (ID `0x123`)**：Roll、Pitch、Yaw 转换 `0.01 deg/LSB` 的 `int16_t`，携带 8-bit 系统状态标志（静止、磁阻抗、标定状态）与滚动序列号。
     - **双帧交替模式 (ID `0x123` + ID `0x124`)**：交替广播姿态与传感器高频特征值（Gz、Az、芯片温度与毫秒时间戳）。
     - **CAN 命令接收与应答 (ID `0x110` / `0x111`)**：支持总线节点发送归零指令、重标定指令与 Ping 探测包，MCU 自动回复包含运行时间与状态的 ACK 帧。
   - 接口扩展：提供 `can_test_update_attitude`、`can_test_get_cmd_flag` 与 `can_test_clear_cmd_flag`。
   - 兼容性：`can_test_live_t` SRAM 内存布局 100% 保持原有定义，现有 DAP 工具完全兼容。

6. **工程构建与宏配置同步 (`inc/app/app_config.h`, `Makefile`, `mdk_v5/lsm6dsv_spi_test.uvprojx`)**：
   - `app_config.h` 中新增数据流默认模式宏 `APP_STREAM_DEFAULT_MODE` 及 CAN 负载模式配置宏 `APP_CAN_PAYLOAD_MODE` 等。
   - `Makefile` 与 Keil 工程已将 `protocol.c` 纳入编译链路，当前已通过 GCC 编译测试（0 警告、0 错误）。

---

## 4. 当前工作区变动文件清单 (Git Status)

```text
Changes not staged for commit:
	modified:   Makefile
	modified:   inc/app/app_config.h
	modified:   inc/bsp/bsp.h
	modified:   inc/drivers/can_test.h
	modified:   inc/drivers/usb_cdc.h
	modified:   mdk_v5/lsm6dsv_spi_test.uvprojx
	modified:   src/bsp/bsp.c
	modified:   src/drivers/can_test.c
	modified:   src/drivers/usb_cdc.c

Untracked files:
	inc/telemetry/protocol.h
	src/drivers/protocol.c
	progress.md
```

---

## 5. 后续紧接的技术工作计划 (Next Steps)

1. **`src/app/main.c` 调度器业务打通**：
   - 实例化 UART 和 USB CDC 两个独立的 `protocol_parser_t`。
   - 在主循环末尾轮询 UART 和 USB CDC 接收字节，接入状态机解析。
   - 绑定命令处理回调函数：响应 Ping、动态零点校正（Zero Yaw 偏航角偏置）、触发静止陀螺标定、动态切换流模式。
   - 在姿态输出函数中，根据 `current_stream_mode` 分支输出 VOFA JustFloat 或 二进制标准姿态帧，并调用 `can_test_update_attitude` 更新 CAN 总线数据。
2. **上位机测试与监控工具配套**：
   - 编写 `tools/serial/ahrs_protocol_reader.py`：支持打开 COM 口（UART/USB CDC），自适应解包二进制协议或 VOFA 数据，支持命令行发送控制指令（如 `--zero-yaw`、`--ping`、`--mode binary/vofa`）。
   - 升级 `tools/dap/dap_can_read.py`：显示 CAN 接收到的真实 Roll/Pitch/Yaw 角度数值与状态位。
3. **协议规范技术文档交付**：
   - 编写 `docs/protocol_specification.md`（详细记录数据帧定义、校验规则、CAN ID 映射与示例代码）。
4. **全量编译与工程归档提交**：
   - 执行 `make -B -j4` 确保所有固件目标编译无误，提交代码并推送到远端仓库。