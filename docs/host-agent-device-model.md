# 上位机接入：型号查询

2026-10-03 新增，首次包含于 `20261003d`。已通过 MicroLink 4 MHz SWD 烧录应用，并通过板载 USB CDC（COM29，2000000 波特率）实测型号和版本查询。未改网页或部署。

发送空载荷命令 `0x35`，成功时仅回 `0x36`，载荷恰好 4 字节 ASCII：`AT32`（`41 54 33 32`）。没有格式前缀、NUL、空格或换行。回复 SEQ 与查询相同，CRC 和框架沿用 AA55 协议。

| 字段 | 查询 | 回复 |
| --- | --- | --- |
| SYNC | `AA 55` | `AA 55` |
| CMD / MSG | `35` | `36` |
| LEN | `00` | `04` |
| SEQ | 请求序号 | 相同序号 |
| PAYLOAD | 空 | `41 54 33 32` |
| CRC16 | MSG、LEN、SEQ 和 PAYLOAD 的 CRC16-CCITT-FALSE，低字节在前 | 同左 |

SEQ=1 的完整示例：

```text
发送：AA 55 35 00 01 E8 F2
回复：AA 55 36 04 01 41 54 33 32 67 79
文本：AT32
```

非空载荷回 ACK `0x90`，载荷 `cmd=0x35,status=0x02,detail=0`，不回型号。无需进入设置模式，静置 VQF 初始化或启动零偏采集中也允许查询。USB CDC、UART、WebUSB 使用同一命令处理器，只在请求来源链路回复。

上位机可在连接后查询一次，使用合法 CRC 的 `0x36` 帧及匹配 SEQ 解码载荷。旧固件可能回 ACK `status=0x01`，或没有回应：显示“型号未知”，不将普通遥测或版本查询成功当作型号查询成功。此文只定义接入方式，没有修改当前线上网页。

版本查询仍为 `0x23 → 0x32`，其载荷是 **16 字节结构**：偏移 0 为 format=1，偏移 1 为 text_len=9，偏移 2 起 char[14] 包含 `20261003d` 和零填充；不要把整个 16 字节直接作为型号或纯版本字符串。

本地固件输出：`../build/device_model_query/release-9axis/device_model_query.bin`，96476 字节，SHA256 `50ee691a3871ed819bb0818a9e4be5f3ffac80f818ff41deb08ec68536557f97`。

构建命令：`mingw32-make TARGET=device_model_query DEBUG_BUILD=0 SIX_AXIS=0 -j4`。原生回归 `python tests/run_native.py` 已通过，包括独立完整帧示例、全部 256 个 SEQ、CRC 错误拒绝和缓冲区边界检查。

实板验证于 2026-10-03 22:23（北京时间）完成：空查询回 `AT32`，版本回 `20261003d`，非空型号查询回参数错误 ACK，再次空查询正常；所有回复 CRC 和 SEQ 正确。证据：`../artifacts/diagnostics/model-query-hardware-20261003d.json`。

烧录证据：`../artifacts/diagnostics/model-query-flash-20261003d.json`。应用回读校验 96476 字节通过，Bootloader 和配置/校准区烧录前后逐字节一致。SWD 镜像 SHA256 为 `2d1ccaaad494a33cc6da041b50fb07696fcda05140cd8935b0cf3b7c0dadab6b`；与 bin 的哈希差异仅来自 `0x0801E89C`–`0x0801E89F` 四个对齐字节：bin 为 `00`，HEX/SWD 为 `FF`。
