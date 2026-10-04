# AT32F423 Bootloader

在项目根目录执行 `make -C bootloader -B all`（Windows 使用 `mingw32-make`），需 GNU Arm 工具链在 PATH。构建输出在 `build/`，与根目录 Makefile 生成的 APP 配套使用。

BL 位于 `0x08000000`，APP 位于 `0x08008000`，`0x0803C000` 及以上为配置 / 校准区。首次安装使用 SWD 更新两者；UART/USB 更新仅写 APP，不覆盖 BL 或配置区。

普通启动读取保存的 T=0～60000 ms（默认 2000 ms），在同一窗口处理升级并采集零偏。到期向 APP 交接合格结果，否则使用历史；APP 不再追加一段 T。T=0 检查合法 APP 后立即启动。空应用、非法向量或应用主动命令 `0x16` 均停留在恢复入口，主动升级不受 T 限制。

BL 和 APP 保留 SRAM 顶部 256 字节，初始 SP 为 `0x2000BF00`；启动结果含 CRC，主动升级 cookie 位于 `0x2000BFF0`。须使用本仓库配套链接脚本。

## 更新协议

支持 USART4、USB CDC 和 WebUSB。请求为小端 18 字节头：`42 4C | version:u8 | command:u8 | seq:u16 | address:u32 | length:u32 | crc32:u32`。只有 DATA 后接 length 字节数据，单块最大 256 字节，地址连续，从 APP 起点开始。

回复为 14 字节：`42 4C | version:u8 | command|0x80:u8 | status:u8 | reserved:u8 | value:u32 | crc32:u32`，CRC32 覆盖前 10 字节。镜像校验为 IEEE/zlib CRC32。

BEGIN 绑定更新端口；成功 END 后发送 BOOT 才进入应用。跨端口写入返回 BUSY，切换端口先由原端口 ABORT。网页上位机已包含该协议实现。
