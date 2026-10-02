# 启动及独立输出协议 v2（兼容 v1）

所有控制和反馈使用现有 AA55 外框：`AA 55 MSG LEN SEQ PAYLOAD CRC16_LE`；CRC16 CCITT-FALSE 覆盖 MSG..PAYLOAD。数值为小端。

## 控制

USB CDC 或 UART 发送 ASCII 字符串 `vofa`，即把 **USB 和 UART 两路当前输出协议** 都切换为 JustFloat，保留每路选中的字段（mask=0 仍关闭遥测）。无需进入设置模式或重启；支持 `VOFA` 等大小写和分段接收，末尾可带 CR/LF，也可不换行。仅在 AA55 报文外识别文本，二进制 payload 中相同字符不会触发。命令不发送文本确认，不写 Flash；断电重启仍使用已保存的输出配置。

HEX 发送：`76 6F 66 61`，对应四字节 `vofa`，不追加空格或换行。

| CMD | payload | 行为 |
| --- | --- | --- |
| 0x1E | mode:u8, fast:u8, apply_now:u8, gyro_init_ms:u16 | 设置模式中保存启动配置及零偏初始化时长，保留 CAN ID 和各接口输出；apply_now=1 应答后重启。兼容旧 3 字节请求，保留已保存时长 |
| 0x1F | 空 | 返回 0x07 配置帧，不要求设置模式 |
| 0x20 | port:u8, format:u8, mask:u16, persist:u8 | port 0 UART / 1 USB；format 0 JustFloat / 1 自定义；persist 0 临时 / 1 Flash 保存；立即应用 |

mode 0 六轴 / 1 九轴 / 2 九轴相对角；fast/apply_now/persist 均为 0 或 1。六轴专用构建拒绝非零 mode。失败返回现有 ACK，写入失败不应用新配置。mask 仅低 9 位有效；0 禁用遥测但仍回复命令。输出设置无需暂停采集。速率命令 0x1D 仍全局、仅本次运行有效。

mask 从 bit0 到 bit8 依次为 **Yaw, Pitch, Roll, Ax, Ay, Az, Gx, Gy, Gz**。浮点值只发送选中项，始终按该顺序。角度 deg、加速度 m/s²、角速度 deg/s。

## 数据

- JustFloat：所选 N 个 float32 + `00 00 80 7F`，N=1..9。流本身不包含字段名，接收端必须先查询配置；切换时重新同步帧尾。
- 自定义 MSG 0x06：`mask:u16, timestamp_ms:u16, float32[N]`，payload 长度 4+4N；外框带 CRC、SEQ；接收端严格检查长度和有限数。
- 所有旧预设及命令 0x13 继续兼容；0x13 同时切换两个接口为旧预设，仅改变当前运行配置。

## 配置反馈 MSG 0x07

v1 固定 18 字节：version:u8=1，source:u8（当前命令来源），active_mode:u8，saved_mode:u8，active_fast:u8，saved_fast:u8，capabilities:u8，reserved:u8，output_hz:u16，然后 UART、USB 各四字节 `format:u8, legacy_mode:u8, mask:u16`。

v2 固定 22 字节，version=2，前 18 字节布局与 v1 相同，末尾追加 `active_gyro_init_ms:u16, saved_gyro_init_ms:u16`。capabilities bit4 表示启动零偏时长可配置，范围 100～60000 ms，默认 2000 ms。fast=0 时在启动后完成完整连续静止窗口；运动、无效样本或采样间隙会重置该窗口，超出时长加 10 秒仍未完成时回退默认零偏并置失败状态。fast=1 时复用温度匹配历史零偏，跳过此窗口。启动设置保存与当前运行分开展示，需重启生效。上位机同时解码 v1/v2，旧固件不支持时明确提示升级。

capabilities bit0 九轴可用、bit1 快速启动配置、bit2 自选输出、bit3 CAN 配置扩展（另见 can-config-protocol.md）。反馈 format=2 表示旧预设，legacy_mode 使用旧枚举 0..4；此值仅用于兼容反馈，不接受通过 0x20 设置。输出条目反馈当前运行状态，不代表临时配置已经写入 Flash。SYSINFO 的 stream_mode 在自选输出时为 0xFF；旧客户端应忽略，不自行假定三通道。

配置查询无输出数据也能使用；上位机据 source 选择当前连接的解析配置，不假定 USB 和 UART 相同。
