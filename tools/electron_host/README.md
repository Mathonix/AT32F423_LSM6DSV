# AT32 AHRS Electron 上位机

## 功能

- Electron 原生串口连接，使用 `serialport`，不依赖浏览器 Web Serial 权限。
- 自动解析 MCU 的 VOFA+ JustFloat 3/4 通道姿态数据。
- 解析二进制 `AA 55 MSG_ID LEN SEQ PAYLOAD CRC16` 帧和 `0x90 ACK`。
- 支持进入/退出设置模式、六轴/九轴/九轴相对角、CAN ID、Yaw 归零、PING、状态查询和重启选项。
- MCU 当前对运行时校准命令返回 `EXEC_FAILED` 时，界面会显示失败，不会误报成功。

## 安装与启动

在项目根目录执行：

```powershell
cd tools/electron_host
npm install
npm start
```

开发检查：

```powershell
npm run check
```

首次安装如果 `serialport` 原生模块需要重建，Electron 会自动执行安装脚本；若使用特殊 Node/Electron 镜像，可执行：

```powershell
npx electron-rebuild
```

## 串口协议

命令帧：

```text
AA 55 MSG_ID LEN SEQ PAYLOAD CRC16_L CRC16_H
```

CRC 是 CRC-16/CCITT，初值 `0xFFFF`，多项式 `0x1021`，覆盖 `MSG_ID + LEN + SEQ + PAYLOAD`。

ACK：

```text
MSG_ID = 0x90
payload = cmd_id, status, detail(uint16 little-endian)
```

融合模式：

```text
0 六轴
1 九轴
2 九轴相对角
```

CAN ID 使用 2 字节小端，范围 `0x000~0x7FF`。模式设置必须先进入设置模式；勾选立即重启时，Electron 等待模式 ACK 后由设备重启，未勾选时在模式 ACK 后自动发送退出设置命令。

## 说明

Electron 主进程只负责串口枚举、打开、写入和接收；协议解析与 UI 状态在渲染进程完成。没有修改 MCU C 文件。实际姿态和 ACK 行为仍需连接目标板验证。
