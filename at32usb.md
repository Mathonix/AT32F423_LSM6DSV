# AT32F423 USB Crystal-less（HICK）配置要点

本文记录 AT32F423KCU7 USB Device CDC 虚拟串口的 Crystal-less 配置方法、当前工程的正确初始化流程，以及此前 USB 无法枚举的原因。

- **芯片**：AT32F423KCU7-4
- **USB 控制器**：OTGFS1，全速 USB Device
- **USB 引脚**：PA12 = USB D+，PA11 = USB D-
- **USB 时钟**：HICK + ACC 自动校准，目标 48 MHz
- **USB 类**：官方 CDC ACM Virtual COM Port
- **当前设备标识**：VID `0x2E3C`，PID `0xF401`
- **验证结果**：Windows 已枚举出 `USB 串行设备 (COM16)`
- **验证日期**：2026-09-14

---

## 1. Crystal-less USB 的基本原理

Crystal-less 的含义是 USB 不依赖外部 HSE 晶振作为 48 MHz 参考，而是使用芯片内部 HICK 时钟，并通过 AT32 的 ACC（Auto Clock Calibration）模块根据 USB SOF 对 HICK 进行自动校准。

USB Full-Speed 对时钟精度要求较高。仅仅打开 HICK、把它当作普通时钟使用，并不能保证 USB 能稳定工作；必须同时完成：

1. 选择 HICK 作为 USB 48 MHz 时钟源；
2. 开启 ACC 外设时钟；
3. 写入官方推荐的 C1/C2/C3 校准窗口；
4. 开启 `ACC_CAL_HICKTRIM` 自动校准；
5. 再初始化 USB Device Core。

当前工程使用官方例程中的校准值：

```c
crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);

crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, TRUE);
acc_write_c1(7980U);
acc_write_c2(8000U);
acc_write_c3(8020U);
acc_calibration_mode_enable(ACC_CAL_HICKTRIM, TRUE);
```

> 这三个值是官方例程的初始校准窗口，不是传感器参数。除非有明确的时钟测量依据，否则不要随意修改。

---

## 2. `usb_conf.h` 的关键配置

当前工程的 `inc/config/usb_conf.h` 至少应包含以下配置：

```c
#define USE_OTG_DEVICE_MODE
#define USB_ID                 0

#define OTG_CLOCK              CRM_OTGFS1_PERIPH_CLOCK
#define OTG_IRQ                OTGFS1_IRQn
#define OTG_IRQ_HANDLER        OTGFS1_IRQHandler

#define OTG_PIN_GPIO           GPIOA
#define OTG_PIN_GPIO_CLOCK     CRM_GPIOA_PERIPH_CLOCK
#define OTG_PIN_DP             GPIO_PINS_12
#define OTG_PIN_DP_SOURCE      GPIO_PINS_SOURCE12
#define OTG_PIN_DM             GPIO_PINS_11
#define OTG_PIN_DM_SOURCE      GPIO_PINS_SOURCE11
#define OTG_PIN_MUX            GPIO_MUX_10

#define USB_VBUS_IGNORE
#define USB_EPT_MAX_NUM        8
```

官方 USB 中间件还需要 FIFO 配置：

```c
#define USBD_RX_SIZE           128
#define USBD_EP0_TX_SIZE       24
#define USBD_EP1_TX_SIZE       20
#define USBD_EP2_TX_SIZE       80
#define USBD_EP3_TX_SIZE       20
#define USBD_EP4_TX_SIZE       20
#define USBD_EP5_TX_SIZE       20
#define USBD_EP6_TX_SIZE       20
#define USBD_EP7_TX_SIZE       20
```

### `USB_VBUS_IGNORE` 的作用

如果硬件没有把 USB VBUS 检测线接到 MCU，或者当前硬件设计不使用 VBUS 检测，就必须定义：

```c
#define USB_VBUS_IGNORE
```

官方中间件会据此：

```c
otgdev->cfg.vbusig = TRUE;
otgdev->usb_reg->gccfg_bit.vbusig = TRUE;
```

同时，官方 GPIO 初始化流程不会再配置 VBUS 引脚。这样 USB Device 不会因为读不到 VBUS 而一直处于未连接状态。

如果没有使用 `USB_VBUS_IGNORE`，但硬件又没有正确提供 VBUS 检测，常见现象就是：D+/D- 电气连接正常，但 Windows 完全看不到新的 USB 设备。

---

## 3. USB D+/D- GPIO 配置

当前硬件连接为：

```text
PA12 -> USB D+
PA11 -> USB D-
```

必须配置为 OTGFS1 的 MUX10：

```c
gpio_init_type gpio_init_struct;

crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
gpio_default_para_init(&gpio_init_struct);

gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
gpio_init_struct.gpio_pins = GPIO_PINS_11 | GPIO_PINS_12;
gpio_init(GPIOA, &gpio_init_struct);

gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE11, GPIO_MUX_10);
gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE12, GPIO_MUX_10);
```

注意：

- 不要把 D+、D- 配成普通 GPIO 输出后再启动 USB；
- 不要在 USB 正常初始化后强行拉高或拉低 D+/D-；
- 不要同时打开 D+、D- 内部上拉来模拟 USB 连接；
- USB 的连接状态和 FS 上拉应交给 USB 外设控制器处理；
- 如果使用 `USB_VBUS_IGNORE`，不要额外把 PA9 当作 VBUS 配置。

---

## 4. 官方初始化顺序

推荐顺序必须接近 AT32 官方 `usb_device/virtual_comport` 例程：

```c
system_clock_config();
bsp_init();

/* 1. USB D+/D- GPIO */
usb_gpio_config();

/* 2. OTGFS1 外设时钟 */
crm_periph_clock_enable(CRM_OTGFS1_PERIPH_CLOCK, TRUE);

/* 3. HICK -> USB 48 MHz，并开启 ACC 校准 */
crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);
crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, TRUE);
acc_write_c1(7980U);
acc_write_c2(8000U);
acc_write_c3(8020U);
acc_calibration_mode_enable(ACC_CAL_HICKTRIM, TRUE);

/* 4. USB 中断 */
nvic_irq_enable(OTGFS1_IRQn, 3, 0);

/* 5. 官方 USB Device + CDC 类状态机 */
usbd_init(&otg_core,
          USB_FULL_SPEED_CORE_ID,
          USB_ID,
          &cdc_class_handler,
          &cdc_desc_handler);
```

中断入口：

```c
void OTGFS1_IRQHandler(void)
{
  usbd_irq_handler(&otg_core);
}
```

主循环中必须持续运行 CDC 发送和接收逻辑：

```c
usb_cdc_task();
```

其中：

- `usbd_irq_handler()` 负责 USB reset、枚举、EP0 控制传输和端点中断；
- `cdc_class_handler` 负责 CDC 类请求和端点状态机；
- `cdc_desc_handler` 提供设备、配置、接口和字符串描述符；
- `usb_vcp_send_data()` 负责 CDC Bulk IN 发送；
- `usb_vcp_get_rxdata()` 负责 CDC Bulk OUT 接收并重新挂接接收缓冲区。

---

## 5. 官方 CDC 端点配置

当前官方 CDC 类使用：

```c
#define USBD_CDC_INT_EPT       0x82
#define USBD_CDC_BULK_IN_EPT   0x81
#define USBD_CDC_BULK_OUT_EPT  0x01
```

全速 USB 最大包长：

```c
#define USBD_CDC_IN_MAXPACKET_SIZE   0x40
#define USBD_CDC_OUT_MAXPACKET_SIZE  0x40
#define USBD_CDC_CMD_MAXPACKET_SIZE  0x08
```

设备描述符和配置描述符中的端点地址必须与 CDC 类代码一致。端点地址、端点方向或接口数量不一致时，Windows 可能在枚举过程中复位设备，最终不生成 COM 口。

当前工程没有继续使用原先手写的 EP0 控制状态机，而是使用官方 CDC 类状态机。

---

## 6. 描述符配置

当前项目保留自定义产品 ID：

```c
#define USBD_CDC_VENDOR_ID    0x2E3C
#define USBD_CDC_PRODUCT_ID   0xF401
```

字符串为：

```c
#define USBD_CDC_DESC_MANUFACTURER_STRING  "AT32"
#define USBD_CDC_DESC_PRODUCT_STRING       "LSM6DSV USB CDC"
```

Windows 识别成功时，可以通过 PowerShell 查看：

```powershell
Get-PnpDevice -PresentOnly |
  Where-Object {
    $_.InstanceId -match 'VID_2E3C|PID_F401'
  } |
  Format-Table Status,Class,FriendlyName,InstanceId -AutoSize
```

当前实测结果：

```text
USB VID_2E3C PID_F401
USB 串行设备 (COM16)
```

COM8 是 DAPLink 的调试串口，不能用来判断 MCU USB CDC 是否枚举成功。

---

## 7. 为什么之前 USB 没有成功枚举

之前的实现是完全手写的 USB CDC 驱动，主要问题有以下几类。

### 7.1 没有完整使用官方 VBUS 忽略配置

旧代码虽然配置了 USB D+/D-，但没有完整接入官方 `USB_VBUS_IGNORE` 配置和 `gccfg.vbusig` 设置流程。对于没有连接 MCU VBUS 检测的硬件，USB 核心可能判断不到有效 VBUS，从而不进入正常 Device 状态。

这也是本次修复中最关键的一项：由官方 `usb_core.c` 根据 `USB_VBUS_IGNORE` 自动设置 VBUS ignore，而不是只在应用层配置 GPIO。

### 7.2 手写 EP0 枚举状态机不完整且容易出错

旧驱动自行处理：

- SETUP 包接收；
- GET_DESCRIPTOR；
- SET_ADDRESS；
- SET_CONFIGURATION；
- SET_INTERFACE；
- CDC 类请求；
- EP0 IN/OUT 状态切换；
- USB reset 和端点重新初始化。

USB 枚举阶段对时序和状态切换非常敏感。任何一个零长度状态包、地址生效时机、接收缓冲区重新挂接或端点中断清除不准确，都可能导致 Windows 放弃枚举。

此前还出现过 `SET_INTERFACE` 控制请求状态包处理不符合主机预期的问题。虽然修复了某一处请求，但继续维护手写状态机仍然存在较大风险。

### 7.3 描述符和软件端点状态机存在维护风险

旧版描述符中使用了自定义的接口和端点组合，而软件代码又使用另一套端点控制逻辑。即使端点地址表面上部分对应，EP0、CDC 通知端点、Bulk IN/OUT 端点的初始化、忙状态和中断处理并没有完全由同一套官方状态机统一管理。

官方实现把以下内容统一起来：

```text
描述符
端点打开/关闭
EP0 控制传输
CDC 类请求
Bulk IN 忙状态
Bulk OUT 接收重挂接
USB Reset
USB Suspend/Wakeup
```

### 7.4 旧版没有严格复用官方初始化流程

旧代码中自行操作了 USB 全局寄存器、FIFO、连接状态和中断掩码。手动配置这些寄存器容易遗漏官方初始化中的默认值，例如：

- USB 核心模式设置；
- PHY 和全局寄存器配置；
- FIFO 分配；
- Device Core 默认端点状态；
- USB reset 后的重新初始化；
- VBUS ignore；
- 枚举完成后的端点启用。

本次改为官方 `usbd_init()` 后，USB 核心和 CDC 状态机由官方中间件统一管理。

### 7.5 HICK 48 MHz 和 ACC 校准没有与官方流程完全绑定

旧代码虽然加入了 HICK 和 ACC 校准寄存器配置，但 USB 其他部分仍是手写初始化，不能保证时钟、USB Core 初始化、连接时序完全按照官方例程执行。

Crystal-less USB 的正确条件不是“写入几个 ACC 寄存器”这么简单，而是：

```text
HICK 作为 USB 48 MHz 源
+ ACC 自动校准
+ 正确 USB GPIO MUX
+ 正确 VBUS 策略
+ 官方 USB Device Core 初始化
+ 官方 CDC 类状态机
+ 正确 USB 中断入口
```

---

## 8. 当前工程的文件职责

```text
inc/config/usb_conf.h
    USB Device、OTGFS1、PA11/PA12、VBUS ignore、FIFO 宏配置

src/drivers/usb_cdc.c
    HICK/ACC/USB GPIO 初始化
    官方 CDC 的应用层封装
    USB TX/RX 环形缓冲区

middleware/usb_drivers/src/usb_core.c
    USB Core 初始化
    USB_VBUS_IGNORE 处理

middleware/usb_drivers/src/usbd_core.c
    USB Device Core、端点和 EP0 状态机

middleware/usb_drivers/src/usbd_int.c
    USB Device 中断分发

middleware/usbd_class/cdc/cdc_class.c
    官方 CDC 类请求和 Bulk 端点状态机

middleware/usbd_class/cdc/cdc_desc.c
    官方 CDC 描述符

src/bsp/at32f423_int.c
    OTGFS1_IRQHandler -> usbd_irq_handler()
```

---

## 9. 编译、烧录和枚举检查

编译：

```powershell
make -B -j4
```

烧录：

```powershell
python tools/dap/dap_flash_and_log.py `
  --hex build/lsm6dsv_spi_test.hex `
  --port COM8 `
  --baud 2000000 `
  --seconds 5
```

说明：

- COM8 是 DAPLink SWD/调试串口；
- 烧录脚本监听 COM8 没有输出，不等于 USB 失败；
- MCU USB CDC 应单独检查新出现的 COM 口；
- 本次成功枚举的 USB CDC 端口是 COM16。

检查 USB：

```powershell
Get-PnpDevice -PresentOnly |
  Where-Object {
    $_.InstanceId -match 'VID_2E3C|PID_F401' -or
    $_.FriendlyName -match 'AT32|USB.*Serial|串行设备'
  } |
  Format-Table Status,Class,FriendlyName,InstanceId -AutoSize
```

检查串口列表：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

使用 VOFA+ 或串口工具打开 COM16 后，USB 默认输出仍为 VOFA+ JustFloat：

```text
float yaw
float pitch
float roll
float temp
00 00 80 7F
```

---

## 10. 排查顺序

如果以后 USB 再次无法枚举，按以下顺序检查：

1. 确认 PA12 是 D+、PA11 是 D-；
2. 确认 D+/D- 没有被其他代码改成普通 GPIO；
3. 确认 USB 使用 MUX10；
4. 确认 `USB_VBUS_IGNORE` 与硬件 VBUS 连接方式一致；
5. 确认 HICK 已选择为 USB 48 MHz 源；
6. 确认 ACC 时钟已打开并启用 HICKTRIM；
7. 确认 OTGFS1 时钟已打开；
8. 确认 `OTGFS1_IRQHandler()` 调用了 `usbd_irq_handler()`；
9. 确认 `usbd_init()` 使用官方 CDC handler 和 descriptor handler；
10. 确认 CDC 的 Bulk IN/OUT 端点和描述符一致；
11. 确认主循环持续调用 `usb_cdc_task()`；
12. 最后再检查 Type-C 方向、电阻、D+/D- 连通性和 VBUS 电压。

---

## 11. 当前结论

本项目 USB 枚举失败的主要原因不是单一的 D+/D- 引脚问题，而是旧版手写 USB 驱动没有完整复用 AT32 官方的：

```text
VBUS ignore 配置
+ Crystal-less HICK/ACC 配置流程
+ USB Device Core 初始化
+ CDC 类状态机
```

改用官方 USB 中间件和官方 CDC 状态机后，2026 年 9 月 14 日已经在 Windows 成功识别：

```text
VID_2E3C PID_F401
USB 串行设备 (COM16)
```

## USB Composite + WebUSB（固件 20261003a 起）

应用固件从单一 CDC 改为 **IAD 复合设备**：保留原 CDC 虚拟串口，新增一个厂商自定义 WebUSB 数据接口。板上现有 Bootloader 仍是纯 CDC；源码版 Bootloader（2026-10-03，尚未 SWD 安装）已改为同样的复合设备 + WebUSB，bcdDevice `0x0280`，见 `docs/bootloader-webusb.md`。

### 设备描述符

| 字段 | 值 |
|---|---|
| VID:PID | `2E3C:F401`（不变） |
| bDeviceClass / SubClass / Protocol | `0xEF / 0x02 / 0x01`（Miscellaneous + IAD） |
| bcdUSB | `0x0210`（20261003b 起；Windows 会请求 BOS，见下文 MS OS 2.0） |
| bcdDevice | `0x0201`（20261003b 起，原 `0x0200`；用于让 Windows 重新查询 MS OS 描述符） |
| 序列号 | MCU UID（不变） |
| bmAttributes / MaxPower | `0xC0` / `0x32`（未改，硬件确认前不改） |
| 配置描述符 wTotalLength | **98 B**，由 `USBD_CDC_CONFIG_DESC_SIZE` 宏计算，编译期 `sizeof` 校验 + 本机测试逐描述符累加校验 |

### 接口与端点（最终版）

| 接口 | 类 | 端点 | 类型 | 包长 | 用途 |
|---|---|---|---|---|---|
| IAD | `0x02/0x02/0x01`，FirstInterface 0，Count 2 | — | — | — | 把接口 0+1 绑成一个 CDC ACM 功能 |
| 0 | CDC Comm `0x02/0x02/0x01` | `0x82` IN | Interrupt | 8 B | CDC 通知（不发数据） |
| 1 | CDC Data `0x0A/0x00/0x00` | `0x81` IN / `0x01` OUT | Bulk | 64 B | 串口数据（Web Serial / 上位机） |
| **2** | **Vendor `0xFF/0x00/0x00`**，iInterface = "LSM6DSV WebUSB" | **`0x83` IN / `0x03` OUT** | **Bulk** | **64 B** | WebUSB 数据（AA55 协议） |

所有接口只有 alt setting 0；`SET_INTERFACE` 对接口 2 只接受 alt 0，接口号 >2 或 alt≠0 会 STALL。CDC 类请求仍只接受 `wIndex == 0`。

FIFO（`usb_conf.h`，单位 word）未改：RX 128，EP0 24，EP1 20，EP2 80，EP3 20（=80 B ≥ 64 B），EP0–3 合计 272 ≤ 320。

### 数据通路

- `usb_cdc.c` 中 WebUSB 有**独立**的 TX 环 2048 B、RX 环 512 B；`webusb_task()` 由 `usb_cdc_task()` 一起调度，同一个 `OTGFS1_IRQHandler`、同一次 `usbd_init`。
- 每次 IN 传输最多 64 B，**不发 ZLP**；网页应循环 `transferIn(3, 64)`，按字节流解析：一帧可跨多个包，一包可含多帧。
- OUT 端点只在包被拷入 RX 环后才重新 arm；RX 环不足 64 B 空间时不 arm，主机被 NAK 反压，不丢字节。
- `main.c` 新增 `PROTOCOL_SOURCE_WEBUSB = 2` 和独立的 `webusb_protocol_parser`，与 UART/CDC 共用 `protocol_frame_received`。**命令回复只回到命令来源的那一路**。该值只在 RAM 中使用，不写 Flash。
- WebUSB 与 CDC **共用 USB 输出配置**（端口索引 1）。USB 输出打开时，同一帧遥测**同时写入 CDC 和 WebUSB 两个环**；没人读的那一路在环满后丢弃，不影响另一路。
- 未枚举/断开时两路环都清空，重新连接不会回放旧数据。复位等待条件包含 `webusb_tx_idle()`（仍有 100 ms 超时兜底）。
- 回复丢包计数：`protocol_reply_drops_webusb`。
- 协议格式、消息 ID、Flash 格式均未改。

### 平台说明 / 风险

- **Windows**：COM 口由 `usbser` 绑定到 `MI_00`，正常可用；但复合设备的设备实例路径变了，**COM 口号可能会变一次**。20261003a 中接口 2 显示为“无驱动”；**20261003b 起由 MS OS 2.0 描述符自动绑定 WinUSB**（见下节），COM 口不受影响。
- **Android Chrome**：可直接 `claimInterface(2)` 使用 `0x83/0x03`。
- **P1（已完成，固件 20261003b）**：MS OS 2.0 描述符集 + WinUSB 兼容 ID，只作用于接口 2。
- **P2（未做）**：WebUSB BOS 平台能力 + Landing Page URL（不影响 Chrome 使用 WebUSB，只是插入时不弹落地页）。

### 验证

- 本机测试 `tests/test_usb_cdc.c`：设备类 EF/02/01、VID/PID；wTotalLength = 98 = 各描述符 bLength 之和；3 个接口、1 个 IAD、5 个端点地址唯一、包长 ≤64；接口 2 类型 FF/00/00、两个 64 B bulk 端点；EP3 OUT 接收后重新 arm、与 EP1 状态互不影响；EP3 IN 忙状态、EP2 完成被忽略、>64 B 拒绝；SET_INTERFACE 边界；clear/init 关闭 5 个端点并恢复空闲状态。
- 构建：`make -B DEBUG_BUILD=0 SIX_AXIS=0 all`。

## MS OS 2.0 / WinUSB（固件 20261003b 起）

目的：Windows 免装驱动、自动给 **接口 2（MI_02，"LSM6DSV WebUSB"）** 绑定 WinUSB，Chrome WebUSB / libusb 可直接打开；CDC 功能（MI_00/MI_01）仍由 `usbser` 驱动，COM 口不变。

### 描述符

| 项 | 值 |
|---|---|
| bcdUSB | `0x0210`（≥ 0x0201 时 Windows 才会读 BOS） |
| bcdDevice | `0x0200` → **`0x0201`** |
| BOS | 33 B = 头 5 B + MS OS 2.0 平台能力 28 B（`bNumDeviceCaps = 1`） |
| 平台能力 UUID | `{D8DD60DF-4589-4CC7-9CD2-659D9E648A9F}` |
| dwWindowsVersion | `0x06030000`（Windows 8.1+） |
| wMSOSDescriptorSetTotalLength | **178 B** |
| **bMS_VendorCode** | **`0x01`**；bAltEnumCode = 0 |
| 描述符集结构 | 集合头 10 B → 配置子集头 8 B（配置索引 0）→ **功能子集头 8 B，bFirstInterface = 2** → 兼容 ID `WINUSB` 20 B → 注册表属性 132 B |
| 注册表属性 | `DeviceInterfaceGUIDs`（REG_MULTI_SZ，UTF-16LE） |
| **DeviceInterfaceGUID** | **`{7B926486-7EEE-499C-BFFC-1CA59C7DD9ED}`**（固定值，主机程序可用它 `SetupDiGetClassDevs` 枚举） |

因为 WinUSB 兼容 ID 放在**功能子集（接口 2）**里，而不是整机，CDC 功能仍走 usbser。各长度由 `cdc_desc.h` 中的宏计算，`cdc_desc.c` 里有 `sizeof` 编译期校验（BOS 与描述符集），并校验两者都不是 64 B 的整数倍（避免需要 ZLP）。

### 请求处理

- 中间件 `usbd_sdr.c`：`GET_DESCRIPTOR` 类型 `0x0F`（新增 `USB_DESCIPTOR_TYPE_BOS`）转给类层 `class_setup_handler`；其他类型行为不变。
- 类层 `cdc_class.c`：
  - 标准 `GET_DESCRIPTOR(BOS)`：只接受 `bmRequestType = 0x80`、`wValue = 0x0F00`、`wLength ≠ 0`，回复 `min(33, wLength)`；其它 STALL。
  - 厂商请求：只接受 `bmRequestType = 0xC0`（设备、IN）、`bRequest = 0x01`、`wIndex = 7`（MS_OS_20_DESCRIPTOR_INDEX）、`wValue = 0`、`wLength ≠ 0`，回复 `min(178, wLength)`；其它厂商请求（含接口接收者、OUT、其它 bRequest/wIndex）一律 STALL。
  - CDC 类请求（Line Coding / DTR/RTS）路径不变。

### Windows 缓存

Windows 按 **VID+PID+bcdDevice** 把“是否有 MS OS 描述符”缓存在 `HKLM\SYSTEM\CurrentControlSet\Control\usbflags\2E3CF401xxxx`。旧固件（bcdDevice 0x0200）已被记为“没有”，所以本次把 bcdDevice 改为 `0x0201` 来强制重新查询（无需管理员删注册表）。**以后再改 BOS / MS OS 2.0 内容时，必须再递增 bcdDevice**（宏 `USBD_CDC_BCD_DEVICE`）。

### 测试方法

1. 本机：`python tests/run_native.py`（`test_usb_cdc.c` 的 `test_ms_os_20`：bcdUSB/bcdDevice；经 `usbd_device_request` 读 BOS（5 B 与全长）、UUID、Windows 版本、集合总长 = BOS 字段 = 178、厂商码；厂商请求返回描述符集并逐项解析：子集长度、bFirstInterface = 2、`WINUSB`、属性名/GUID UTF-16 与 MULTI_SZ 结尾、短读；错误厂商请求和非 BOS 描述符 STALL 且不发送数据）。
2. 刷机后 PowerShell：`Get-PnpDevice -PresentOnly | ? { $_.InstanceId -match 'VID_2E3C' }`，并用 `Get-PnpDeviceProperty -InstanceId <id> DEVPKEY_Device_Service` 确认：MI_00 = `usbser`（USB 串行设备 COMxx），MI_02 = `WINUSB`，均为 Status OK。
3. pyusb：`pip install pyusb libusb-package`，用 `libusb_package.get_libusb1_backend()` 找 `2E3C:F401`，只 `claim_interface(2)`（不要动 CDC 接口），向 EP `0x03` 发 `0x23` 查询帧，从 EP `0x83` 读并按 AA55 解析，应收到 `0x32` 固件信息与持续的 `0x06` 遥测（约 1000 帧/s）。脚本参考 `C:\Users\Mathonix\at32_webusb_test.py`。
4. 实测（20261003b）：MI_00 = USB 串行设备 (COM29) / usbser / OK；MI_02 = LSM6DSV WebUSB / WINUSB / OK，`DeviceInterfaceGUIDs` 已写入；pyusb 读到 BOS 33 B、描述符集 178 B，未知厂商请求 STALL；WebUSB 遥测 ≈1021 帧/s；WebUSB 查询只在 WebUSB 收到回复，COM29 查询只在 COM29 收到回复。

## UART RX ROERR 修复（固件 20261003c）

- 现象：App UART（USART4，2 Mbps）在突发接收后 RX 失效：遥测照常发送，但 0x10/0x15/0x23 均无 ACK/回复。
- 原因：`uart_rx_isr()` 在 ROERR=1、RDBF=0 时直接 return，不读 DT；AT32 的 ROERR 只能靠“读 STS 再读 DT”清除，且会保持 RDBF 中断请求，于是中断反复进入、RX 永久卡死。
- 修复：ISR 只读一次 STS；RDBF/ROERR 都为 0 才返回；任一置位都读 DT（清 ROERR）；ROERR 计入 `uart_rx_overruns`；仅当 RDBF 置位时把 DT（溢出时为最后一个有效字节）存入环形缓冲。
- Bootloader（`bootloader/src/bl_io.c`，轮询方式）用 `usart_flag_clear(ROERR)`（内部读 STS+DT）清除溢出，不会卡死，但会丢弃 DT 中的那个字节；bootloader 不能经 App 升级更新，暂不修改。
