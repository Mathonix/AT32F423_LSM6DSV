# 真实 PCB 模型、Pitch/Roll 方向与重启重连验证

日期：2026-10-01。网页工程：`upper/gyro-live`。线上地址：https://gyro.233688.xyz/ 。最终页面资源版本：`20261001e`。

## 修改

采用用户提供的 `E:/Downlload/3D_PCB1_2026-10-01.step` 转换的 GLB，保留源文件不变。210个元件网格、142832个三角形、5种材质；PCB约24.97×19.96×1.59mm。源文件SHA256为 `45e42efcdf91d0b2acac6e9da7fd98c993d364612016424bc087079ef340aad0`。

模型安装变换为绕Z轴+90度：CAD +X对应传感器+Y，CAD +Y对应传感器−X，CAD +Z对应传感器+Z。真实模型世界姿态为设备姿态矩阵乘安装矩阵（R×A），欧拉角和四元数共用该路径，修正Pitch/Roll与PCB几何轴的对应关系。设备角度读数保持原含义。

保存启动配置并立即重启后，自动重开本页此前授权并打开的同一个SerialPort对象；保留实际波特率和解析设置，不重新弹出选口窗口，也不按VID/PID猜测设备。设备拔插/致命读错误也尝试恢复。等待设备初始化期间重复查询，最多90秒；保存失败不主动重启连接。手动断开可取消正在打开的端口或重试计时器。

## 检查结果

- 本地构建、45项逻辑测试、14步原浏览器回归和启动设置回归通过。
- 线上真实模型、六组欧拉角、四元数、实际模型安装矩阵、视角拖动/缩放/复位、1440/820/390/360屏宽、GLB加载失败回退通过。
- 线上8种串口恢复场景通过：UART端口持续存在、USB移除后延迟回来、重启ACK丢失、端口打开后初始化尚未结束、保存失败、正在打开时取消、重试间隙取消、先保存后物理重启。恢复场景确认原端口对象、原115200波特率、选口仅一次，且不会误开相同VID/PID的另一模拟设备。
- 线上7份静态资源与发布manifest的字节数和SHA256完全一致；`/healthz`返回HTTP200及ok:true。独立核对时间为2026-10-01 09:52:15（北京时间）。

串口回归使用模拟串口；本轮未执行真实USB重新枚举测试或固件烧录。线上截图使用新浏览器页面且未打开串口；它展示了实际加载的模型和修正后的零姿态。

## 交付与证据

- 发布包：`artifacts/web-host/gyro-233688-20261001e.zip`，3468905字节，SHA256 `70e6ec478a49b6dd8eb41612b8d7b5bda585aca3faf4b2877298dad2cc7bdf01`。
- 回滚点：20261001d，Cloudflare版本 `84d69a58-6ca1-460c-b694-af830d3c74a8`。
- 静态资源比对：`artifacts/web-host/online-deployment-verification-20261001e.json`。
- 模型回归：`artifacts/web-host/pcb-model-browser-20261001.json`。
- 重连回归：`artifacts/web-host/reconnect-browser-20261001.json`。
- 无串口连接的线上截图及模型状态：`artifacts/web-host/pcb-model-corrected-20261001-online.png`、`pcb-reconnect-20261001-online.png`、`pcb-online-capture-20261001e.json`。
- 本地回归快照：`pcb-model-browser-local-20261001e.json`、`reconnect-browser-local-20261001e.json`（位于上述artifacts目录）。
