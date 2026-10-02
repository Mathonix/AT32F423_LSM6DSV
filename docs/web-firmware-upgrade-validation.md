# 网页 USB/UART 固件升级验证

日期2026-10-01，资源版本 `20261001fw1`，代码入口 `upper/gyro-live`。网页已实现实际Web Serial升级，复用本板验证的Bootloader v1。本次不修改板上固件代码。

## 实现

应用串口发送 `0x16` 进入升级模式，释放普通读写流，以2,000,000 baud重开同一已授权SerialPort。HELLO必须确认 `0x08008000`，BEGIN擦除应用分区，DATA连续256字节分块（尾块可不足256），检查每块确认偏移，END校验完整镜像。只有END成功才BOOT，应用成功PING后才显示完成。上传独占串口并阻止配置、校准和普通重连。停止在当前步骤后ABORT，不启动残缺应用。

输入是本板应用 `.bin`，8～212,992字节；检查SRAM栈顶、Thumb Reset位及入口是否在应用镜像内，显示CRC32和SHA256。此检查不提供签名认证或证明板型兼容，用户须选择本板固件。文件只在本地浏览器读取，不传到服务器。

BEGIN/DATA失败或超时不自动重试，避免v1非幂等写入；可在恢复模式完整重传。USB重枚举只重开同一对象，对象失效时用户手动选择恢复端口，不按VID/PID猜选同型号设备。请求和Boot流清理都有超时，并释放流锁；恢复选口期间阻止重复上传。

v2持久状态、`.atfw`、签名、断电自动回滚均未实现，升级必须保持供电。首次配套Bootloader安装仍需SWD，本板已安装；设计样稿不代表v2完成。

## 自动测试

- 构建通过，Worker468字节与原线上备份相同；59项逻辑测试通过，包含8组升级协议/传输/超时测试。
- 升级浏览器模拟8组：错误向量不擦除和UART全流程；USB延迟重枚举及不猜选设备；END CRC失败不BOOT及恢复重传；错误偏移不重试；停止ABORT；已在Boot的恢复；1440/820/390/360 px布局；错误Boot分区不BEGIN。
- 六面校准11组、量程/频率8组、原串口重启恢复和真实PCB模型回归通过。模拟场景不打开真实串口。

## UART 网页实板测试

AT32F423KCU7-4 / LSM6DSV，MCU ID `0x700A3253`，COM6 WCH UART，2,000,000 baud。用户选择端口并保持供电。旧页面占用COM6时断开旧连接，由用户在新版页面重新选择，不终止其他进程，不继续六面校准。

固定文件 `artifacts/firmware-upgrade-hardware/test-application-20261001.bin`，78,352字节，CRC32 `DDA59240`，SHA256 `45ddd3c02d324079717772a05ec1ba981a4d7736b2072e10d696344ecf833834`。

第一轮23:11:47开始，23:11:51校验后成功PING并重连，23:12:03姿态数据恢复。检查界面时误触发同版本第二次上传：23:13:01开始，23:13:06确认完成，23:13:16数据恢复，随后约995帧/秒。校验完成与启动数据恢复分别记录，不把PING误称为已完成全部初始化。

SWD只作独立Flash回读，无安装、擦写或复位。最终应用全内容与上传文件相同，尾部为FF；Boot、设备设置、加速度校准和其他非陀螺历史预留区完全保留。陀螺启动历史正常新增记录，两个槽CRC有效。证据：`artifacts/firmware-upgrade-hardware/board-{before,after,final}-web-uart-20261001fw1.*` 和 `web-uart-20261001fw1.{json,jpg}`。

本轮未执行网页USB实板上传；USB重枚举/恢复为模拟验证，之前Python工具USB实板完整上传通过，不能等同于本轮网页USB实测。

## 发布

发布包仅含8份静态资源、未改动的Worker及部署文件，不含固件、设备Flash备份或凭据。沿用用户指定的Grok Bot渠道发布到 `https://gyro.233688.xyz/`。

Grok Bot报告部署版本 `f6a77f67-1a0e-4f1e-bb58-ca71aee7842b`，发布时间2026-10-01 23:24:47北京时间；发布前确认回滚点为 `3b531072-4740-48ad-b386-ee7074838897`（acc3），100%流量，无其他变化。这些元数据由Bot返回，未在本机通过Cloudflare API独立查询。

发布包 `artifacts/web-host/gyro-233688-20261001fw1.zip`，1,635,363字节，SHA256 `6cb07bd75ce88fc822d01d4d8c6c35c5f5016a65a11289fc4e2cd58932c4769d`。23:19:10已独立备份当时公开acc3资源，再发送发布包；未将新版本误当旧版备份。

23:28:37本机独立HTTPS核对8份资源全部通过，`/healthz`200且`ok:true`，HTML缓存标识为fw1。7份非HTML响应原始字节和SHA256精确匹配manifest；HTML边缘原有统计模块多367字节，严格移除唯一的Cloudflare统计标签后15293字节、源SHA256精确匹配，其他差异仍判失败。未修改统计或隐私设置。

线上浏览器实际加载 `firmware-upgrade.js?v=20261001fw1`，升级面板可展开，未连设备时开始按钮禁用；真实板卡GLB加载及模型回归通过。发布不执行实板操作，UART网页实测在本地完成。最后断开本地测试连接并释放COM6，避免在线使用时占口。

部署证据见 `artifacts/web-host/deployment-manifest-20261001fw1.json`、`online-{before,after}-sync-20261001fw1.json`及响应目录、`grok-deployment-receipt-20261001fw1.txt`、`firmware-model-online-20261001fw1.log`、`firmware-upgrade-online-20261001fw1.jpg`。UART完成截图另有 `artifacts/firmware-upgrade-hardware/web-uart-panel-20261001fw1.jpg`。
