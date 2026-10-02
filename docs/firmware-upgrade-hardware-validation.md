# USB / UART 固件升级实板验证

日期：2026-10-01；板卡 AT32F423KCU7-4 / LSM6DSV；MCU ID `0x700A3253`。

用户要求先停止六面校准并测试固件升级。已从真实网页上位机取消校准，设备回复取消成功及 `0x0608`，随后退出设置模式。没有继续采集六面或保存新的加速度参数。

## 实测结论

| 项目 | 结果 |
| --- | --- |
| 原板上 Bootloader 的 USB 进入升级流程 | 失败：应用收到 `0x16` 并回复成功，但没有 Boot HELLO，MCU 随后运行应用。没有发送 BEGIN 或擦除应用。 |
| 当前 Bootloader 首次安装 | SWD 只更新 `0x08000000..0x08007FFF`，完整回读 32 KiB；应用和整个配置预留区逐字节保持一致。 |
| 当前 Bootloader 的 USB 进入 / HELLO / BOOT | 通过，应用回复 PING。 |
| USB 全量应用上传 | 通过，78,352 字节，307 个 DATA 块；设备逐块确认偏移，END 整镜像 CRC 通过。 |
| 正式上传脚本 USB 实测 | 通过，`--enter` 关闭旧句柄并重新打开同一 USB 身份，上传后收到 BOOT ACK。 |
| 升级后应用 | PING 正常；实际收到 1,292 / 1,583 帧校验合法遥测。 |
| Flash 独立回读 | 应用全内容与上传 bin 完全一致；应用分区剩余区域为 FF；Boot 区与安装构建一致。 |
| 配置和校准 | 设备配置、加速度校准、其他非陀螺历史预留区域保持一致。启动零偏采样更新了陀螺历史，两个历史槽均通过 CRC。 |
| UART COM6 | 后续通信恢复；修复 Boot 应答阻塞后，2,000,000 baud 完整上传、END CRC、启动、PING/遥测及全镜像回读通过。USB 线全程保留连接。 |
| 修复版 USB 回归 | 正式脚本全量上传通过；应用 PING/遥测恢复，独立回读确认 Boot/配置/加速度校准保留及应用精确匹配。 |

上表记录 **v1 Boot 协议的 Python 工具测试**。随后已实现正式网页v1升级控制器，并于同日完成COM6 UART实板网页上传、END校验、应用PING/重连和Flash回读，见 [网页升级验证](web-firmware-upgrade-validation.md)。设计文档中的v2持久PENDING/VALID、`.atfw`仍未实现，也没有真实断电注入或旧镜像自动回滚验证。

## 固定测试镜像

测试使用九轴应用评审构建，在上传前检查分区、大小和 SP/Reset 向量。UART 后续测试固定使用 `artifacts/firmware-upgrade-hardware/test-application-20261001.bin`，避免其他构建改变待测文件。

- 长度：78,352 字节。
- SHA-256：`45ddd3c02d324079717772a05ec1ba981a4d7736b2072e10d696344ecf833834`。
- IEEE CRC32：`0xDDA59240`。
- 应用地址：`0x08008000`；初始 MSP `0x2000BFF0`；Reset 向量 `0x080167A9`。

本轮上传文件与先前六面校准 SWD 烧录的 78,136 字节产物不同，因此分别保留证据，不能用早先的 SHA 判断本轮失败。实测启动后的融合模式、量程、输出频率、独立通道配置保持一致。

Boot 区安装后 32 KiB SHA-256：`8263b24f15fc3082af2ca010bc6d85eba5a00a93df5fbe670a6bb6f0ab646dba`。初始 MSP `0x2000BFF0`；Reset 向量 `0x08003215`。

后续 UART 修复版为当前板上 Boot：完整 32 KiB SHA-256 `908c1c30a6c969e469bdc6062c39b4b0202667e6051b8f7b380fc13e6f1e2348`；Reset 向量 `0x08003301`。Boot bin 14,516 字节，SHA-256 `1d848a5a28e0301c86e13faa9345ed0d87a84d952a43e63d68a9797833279934`。默认 `bootloader/build/` 已重新构建，bin 与实测的 `bootloader/build/uart_reply_fix/` 逐字节相同。

## UART 失败定位与修复

第一次 UART 全量测试收到 BEGIN 和首个 DATA（确认偏移 256）的合法 ACK，但第二个 DATA 超时，未发送 END/BOOT。设备保持在 Boot，随后经 USB 从头恢复完整镜像，END CRC、PING 和遥测正常；没有复位或尝试运行半写入应用。

旧 `bl_io_write` 先发送 UART ACK，再广播到 USB，并可能在 `usb_wait_tx` 阻塞 100 ms。主机收到 UART ACK 后马上发送下一块，Boot 此时可能仍在等没有打开的 USB 通道，轮询 UART 接收无法及时服务。这与实测在首块应答后停住的现象一致。

修复内容：

- 接收字节携带 USB/UART 来源；两个接口各有独立帧缓冲和解析状态，禁止拼接不同接口的字节。
- 应答只发往请求来源；UART 应答路径立即返回，不等待 USB 端点；清除 UART 接收溢出标志以恢复后续接收。
- BEGIN 绑定物理接口；其他接口只允许 HELLO，所有写入、结束、取消和启动命令返回 BUSY。原接口 ABORT 后可改用另一接口重传；这是 RAM 中的接口独占，仍不是 v2 持久升级状态。

新增原生回归检查交错的半帧、应答去向、外来 BEGIN/DATA/END/ABORT/BOOT 不修改镜像、END 后仍保持接口独占，以及 ABORT 后换口。8 组回归通过。修复后只更新并完整回读 Boot 区，安装时应用和整个配置预留区逐字节保留。

修复后的 UART 实测：307 个 DATA 块连续确认到 78,352 字节；整镜像 CRC `0xDDA59240` 通过；BOOT 后 COM6 回复 PING 并收到 852 帧合法遥测；完整测试约 6.56 秒（含进入 Boot、上传和应用恢复观察）。回读应用与固定镜像完全一致，剩余应用分区为空白，设备配置和加速度校准区一致；两个陀螺历史记录 CRC 正常。USB 回归使用正式上传脚本，随后收到 1,584 帧合法遥测，完整回读也通过。

## 使用已实测的正式工具

先断开上位机，让对应串口可独占打开；保持板卡供电。首次 Boot 安装本板已完成，后续应用上传使用 USB / UART。

```powershell
python bootloader/tools/bl_upload.py artifacts/firmware-upgrade-hardware/test-application-20261001.bin --port COM16 --baud 2000000 --enter
```

`--enter` 要求应用支持 `0x16` 且 Boot 能维持升级模式。重新打开时按 USB VID/PID/序列号匹配，COM 编号变化也可以识别；重复身份拒绝选择。HELLO 不匹配应用起始地址则停止，不发 BEGIN。没有自动重试 BEGIN 或 DATA，因为 v1 不提供重复包幂等保证。

UART 使用 USART4：板上 PA0 TX 接适配器 RX，PA1 RX 接适配器 TX，并共地；2,000,000 baud，8N1。本轮 COM6 链路恢复后已完成 UART 上传验证，后续可用正式工具将上述命令的端口替换为 COM6。

正式工具返回“启动已请求”表示已通过 END 并收到 BOOT 应答；完整验收还要检查应用回复和 Flash 内容。本轮已执行这两项独立检查。

## 证据

完整备份：

- `artifacts/firmware-upgrade-hardware/board-before-upgrade-test-20261001.bin`
- `artifacts/firmware-upgrade-hardware/board-before-boot-install-20261001.bin`

真实串口测试：

- `usb-old-boot-handshake-20261001.json`：原 Boot 入口失败，应用 ACK 已记录。
- `boot-install-20261001.json`：SWD Boot 限定范围安装与完整回读。
- `usb-current-boot-handshake-20261001.json`：当前 Boot 握手和启动；此次观察只记录 PING，不用它证明遥测恢复。
- `usb-upload-20261001.json`：全部 BEGIN / DATA / END 应答、镜像 CRC、恢复遥测。
- `usb-readback-20261001.json`：第一次 USB 上传后完整 Flash 回读。它与安装前备份比较，因此 `boot_preserved=false` 是此次主动安装 Boot 造成；安装后的 Boot SHA 已另外核对一致。
- `usb-canonical-uploader-20261001.log`：修正后正式上传脚本的实际上传日志。
- `usb-canonical-application-20261001.json`、`usb-canonical-readback-20261001.json`：正式脚本后 PING/遥测、Boot/应用、设置和校准的独立验收。
- `uart-application-link-20261001.json`：UART 应用链路未收到回复，尚未擦写。
- `uart-link-retest-20261001.json`：后续 UART PING/遥测恢复。
- `uart-preflight-20261001.json`、`board-before-uart-20261001.bin`：UART 上传前完整备份和镜像/Boot 核对。
- `uart-upload-20261001.json`：修复前第二个 DATA 超时；保留实际失败记录。
- `usb-recovery-after-uart-20261001.json`：失败后 USB 从头恢复完整镜像、启动和遥测。
- `uart-fix-native-20261001.log`、`uart-fix-build-20261001.log`：接口解析/应答/独占回归及 ARM 构建。
- `uart-boot-fix-install-20261001.json`、`board-before-uart-boot-fix-20261001.bin`：修复版 Boot 限定范围安装与安装前全 Flash 备份。
- `uart-upload-fixed-20261001.json`、`uart-readback-fixed-20261001.json`：UART 全量上传、启动及完整回读。回读比较的是修复前 Boot 备份，故 `boot_preserved=false` 表示已安装修复版；`boot_matches_expected_build=true` 独立确认实际 Boot 正确。
- `usb-after-uart-fix-canonical-20261001.log`、`usb-after-uart-fix-application-20261001.json`、`usb-after-uart-fix-readback-20261001.json`：修复版 Boot 上的 USB 全量回归与完整回读。
- `boot-canonical-fixed-build-20261001.log`：默认 Boot 构建更新，bin 与实测构建相同。

上述短文件名均在 `artifacts/firmware-upgrade-hardware/`。实测程序 `tools/serial/test_firmware_upgrade_hardware.py` 不含 SWD 编程；只读回核验/限定 Boot 安装由 `tools/dap/firmware_upgrade_swd.py` 独立完成。

软件回归：`python tests/run_native.py` 的 8 组通过；`python -B -m unittest discover -s tests -p test_upload.py -v` 的 12 项通过，包括 USB 端口重编号、错误/重复设备拒绝、关闭旧句柄、END 失败不 BOOT。
