# VOFA 文本切换协议验收

验收日期：2026-10-01。配套固件已写入实板并读回验证。

## 使用方式与行为

USB CDC 或 UART 发送四字节 ASCII `vofa`，HEX 为 `76 6F 66 61`。VOFA 发送栏选择“无追加”，不添加空格或换行。收到第四字节时立即把两路当前输出协议都改为 JustFloat，保留各自选择的通道；mask=0 仍关闭该路遥测。

支持大小写及分段接收，各接口分别维护匹配状态。仅在 AA55 二进制报文外识别，报文中的相同字节即使 CRC 错误也不触发。命令不输出文字确认，不自动写入 Flash；重启使用原已保存配置。

## 验证结果

- 本地 6 组 C 回归通过；协议用例覆盖无结束符、分段、两路隔离、大小写、错配、二进制载荷及 CRC 错误隔离。
- 九轴及六轴固件编译通过。
- 实际 UART COM6 发送 `766f6661`、逐字节发送以及大写变体均通过，两路格式读回为 JustFloat，通道保持 UART `0x01FF`、USB `0x0007`；UART 收到有效浮点帧。
- 实板二进制载荷隔离、启动及 CAN 配置保持、重启后恢复原保存配置均通过。脚本先重启以建立保存配置基线，避免把此前的临时切换误作保存值。
- 在 VOFA 原生界面选择“无追加”发送 `vofa` 后，通过 SWD 确认两路格式均为 JustFloat，通道和保存配置未变；VOFA 已恢复连接。
- AT32 USB CDC 本次未连接，USB 输入解析由本地回归覆盖；实际输入来自 UART，USB 输出配置已通过设备读回及 SWD 核验。

实板脚本：`python tools/serial/test_vofa_hardware.py --run-hardware`。完整结果：`artifacts/web-host/vofa-serial-hardware-20261001.json`、`artifacts/web-host/vofa-native-after-20261001.json`。

## 固件升级

应用区写入并逐字节核验 72768 字节，Bootloader、设置及校准区域保持不变。固件 SHA256：`a68cbe0f920f1c47759f54dcb5425c259250fe964e9666424470ad0ffa60b171`。

升级结果：`artifacts/web-host/swd-vofa-upgrade-20261001.json`。本次没有网页代码改动。
