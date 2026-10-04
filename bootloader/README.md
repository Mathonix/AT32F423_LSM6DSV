# AT32F423 Bootloader

在项目根目录执行 `make -C bootloader -B all`（Windows 使用 `mingw32-make`），需 GNU Arm 工具链在 PATH。构建输出在 `build/`，与根目录 Makefile 生成的 APP 配套使用。

BL 位于 `0x08000000`，APP 位于 `0x08008000`，`0x0803C000` 及以上为配置 / 校准区。首次安装使用 SWD 更新两者；UART/USB 更新仅写 APP，不覆盖 BL 或配置区。

普通启动读取保存的 T=0～60000 ms（默认 2000 ms），在同一窗口处理升级并采集零偏。到期向 APP 交接合格结果，否则使用历史；APP 不再追加一段 T。T=0 检查合法 APP 后立即启动。空应用、非法向量或应用主动命令 `0x16` 均停留在恢复入口，主动升级不受 T 限制。

20261005a 的启动静止判定使用 100 ms 块均值、标准差和重力方向，避免单个正常噪声尖峰越过 1 dps / 0.8 m/s² 就否决整个窗口。块均值门限仍为 1 dps / 0.8 m/s²，标准差仍为 0.15 dps / 0.15 m/s²，方向变化上限 1°；大于 5 dps 或加速度模长偏差 2.4 m/s² 的大幅异常立即否决。真实运动、不稳定噪声、丢采样仍回退历史，T 不延长。0x38 reason 增加 7 角速度越限、8 加速度模长越限、9 加速度噪声、10 样本不足、11 首样本过晚、12 末样本过旧、13 窗口未完整覆盖、14 BL 未采集。这项判定在 BL 内，必须更新 BL 才生效，仅更新 APP 不会改变旧 BL 的采集规则。

BL 和 APP 保留 SRAM 顶部 256 字节，初始 SP 为 `0x2000BF00`；启动结果含 CRC，主动升级 cookie 位于 `0x2000BFF0`。须使用本仓库配套链接脚本。

PA8 的 RGB 灯在 BL 等待和启动零偏采集期间每秒快速闪蓝灯两下（每次亮80ms，间隔100ms，随后熄灭740ms）；进入 APP 后由 APP 接管灯光。上位机触发静置初始化时，准备和采集期间使用相同节奏。T=0 不额外延长蓝灯显示时间。此灯光行为需更新 BL；USB/UART 的 APP 升级不能替换 BL。

接收未完成的升级命令或固件传输已占用端口时暂停刷新灯效，避免 WS2812 波形发送占用 UART 接收时间。

## 临时 APP 更新用户 BL

`USER_BL_UPDATER=1` 为专用维护构建：内嵌指定的 BL 镜像，USB 设置模式下接收明确更新请求，检查芯片ID、镜像CRC、向量和分区，只写BL的32KiB范围，向量最后写并完整校验。正常APP不编译这些接口。`tools/serial/build_user_bl_updater.py` 生成镜像头并构建，`tools/serial/app_update_user_bl.py` 在写入前保存板上BL与保留区，写后逐字节核验。单份BL更新不具备掉电原子性，失败时不主动复位，保持供电使用临时APP诊断。

2026-10-05 已通过USB装入临时APP 20261005b、更新新BL并恢复正常APP 20261005a，整个32KiB BL读回一致、16KiB保留区不变，连续三次普通启动均采用新采集零偏。证据见本地 `artifacts/diagnostics/user-bl-usb-20261005/final-report.json`。普通BL升级协议仍只写APP，写BL操作由临时APP执行。

## 更新协议

支持 USART4、USB CDC 和 WebUSB。请求为小端 18 字节头：`42 4C | version:u8 | command:u8 | seq:u16 | address:u32 | length:u32 | crc32:u32`。只有 DATA 后接 length 字节数据，单块最大 256 字节，地址连续，从 APP 起点开始。

回复为 14 字节：`42 4C | version:u8 | command|0x80:u8 | status:u8 | reserved:u8 | value:u32 | crc32:u32`，CRC32 覆盖前 10 字节。镜像校验为 IEEE/zlib CRC32。

BEGIN 绑定更新端口；成功 END 后发送 BOOT 才进入应用。跨端口写入返回 BUSY，切换端口先由原端口 ABORT。网页上位机已包含该协议实现。
