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
