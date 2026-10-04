/**
  **************************************************************************
  * @file     cdc_desc.h
  * @brief    usb cdc descriptor header file
  **************************************************************************
  *
  * Copyright (c) 2025, Artery Technology, All rights reserved.
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */

/* define to prevent recursive inclusion -------------------------------------*/
#ifndef __CDC_DESC_H
#define __CDC_DESC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cdc_class.h"
#include "usbd_core.h"

/** @addtogroup AT32F423_middlewares_usbd_class
  * @{
  */

/** @addtogroup USB_cdc_desc
  * @{
  */

/** @defgroup USB_cdc_desc_definition
  * @{
  */
/**
  * @brief usb bcd number define
  */
#define CDC_BCD_NUM                      0x0110

/**
  * @brief usb vendor id and product id define
  */
#define USBD_CDC_VENDOR_ID               0x2E3C
#define USBD_CDC_PRODUCT_ID              0xF401

/**
  * @brief usb descriptor size define
  */
#define USB_IAD_DESC_LEN                 8U
#define USBD_COMPOSITE_NUM_INTERFACES    3U
/* config(9) + IAD(8) + CDC comm IF(9)+func(5+5+4+5)+int EP(7)
 * + CDC data IF(9)+2xEP(7) + WebUSB IF(9)+2xEP(7) = 98 */
#define USBD_CDC_CONFIG_DESC_SIZE        (9U + USB_IAD_DESC_LEN + \
                                          (9U + 5U + 5U + 4U + 5U + 7U) + \
                                          (9U + 7U + 7U) + \
                                          (9U + 7U + 7U))
#define USBD_CDC_SIZ_STRING_LANGID       4

/**
  * @brief device release / USB version
  * bcdUSB 0x0210 makes Windows read the BOS descriptor. Windows caches the
  * MS OS descriptor query result per VID+PID+bcdDevice (registry
  * HKLM\SYSTEM\CurrentControlSet\Control\usbflags), so bcdDevice must be
  * bumped whenever the BOS / MS OS 2.0 content changes.
  * The bootloader overrides bcdDevice (0x0280, bit 7 = bootloader) and the
  * product/interface strings in bootloader/inc/usb_conf.h; application
  * bcdDevice values must stay below 0x0280 (low byte < 0x80).
  */
#define USBD_CDC_BCD_USB                 0x0210U
#ifndef USBD_CDC_BCD_DEVICE
#define USBD_CDC_BCD_DEVICE              0x0201U
#endif

/**
  * @brief BOS + MS OS 2.0 descriptor set (WinUSB for interface 2 only)
  */
#define USBD_BOS_HEADER_LEN              5U
#define USBD_BOS_PLATFORM_CAP_LEN        28U
#define USBD_BOS_DESC_SIZE               (USBD_BOS_HEADER_LEN + USBD_BOS_PLATFORM_CAP_LEN)

#define USBD_MS_OS_20_VENDOR_CODE        0x01U  /* bRequest of the vendor IN request */
#define USBD_MS_OS_20_DESCRIPTOR_INDEX   0x07U  /* wIndex: MS_OS_20_DESCRIPTOR_INDEX */
#define USBD_MS_OS_20_WINDOWS_VERSION    0x06030000UL /* Windows 8.1+ */

#define USBD_MS_OS_20_SET_HEADER_LEN     10U
#define USBD_MS_OS_20_CFG_SUBSET_LEN     8U
#define USBD_MS_OS_20_FUNC_SUBSET_LEN    8U
#define USBD_MS_OS_20_COMPAT_ID_LEN      20U
/* L"DeviceInterfaceGUIDs\0" = 21 UTF-16 chars */
#define USBD_MS_OS_20_PROP_NAME_LEN      42U
/* L"{7B926486-7EEE-499C-BFFC-1CA59C7DD9ED}\0\0" (REG_MULTI_SZ) = 40 chars */
#define USBD_MS_OS_20_PROP_DATA_LEN      80U
#define USBD_MS_OS_20_REG_PROP_LEN       (10U + USBD_MS_OS_20_PROP_NAME_LEN + USBD_MS_OS_20_PROP_DATA_LEN)
#define USBD_MS_OS_20_FUNC_SUBSET_TOTAL  (USBD_MS_OS_20_FUNC_SUBSET_LEN + USBD_MS_OS_20_COMPAT_ID_LEN + \
                                          USBD_MS_OS_20_REG_PROP_LEN)
#define USBD_MS_OS_20_CFG_SUBSET_TOTAL   (USBD_MS_OS_20_CFG_SUBSET_LEN + USBD_MS_OS_20_FUNC_SUBSET_TOTAL)
/* 10 + 8 + 8 + 20 + 132 = 178 */
#define USBD_MS_OS_20_DESC_SET_SIZE      (USBD_MS_OS_20_SET_HEADER_LEN + USBD_MS_OS_20_CFG_SUBSET_TOTAL)
#define USBD_CDC_SIZ_STRING_SERIAL       0x1A

/**
  * @brief usb string define(vendor, product configuration, interface)
  */
#define USBD_CDC_DESC_MANUFACTURER_STRING    "AT32"
#ifndef USBD_CDC_DESC_PRODUCT_STRING
#define USBD_CDC_DESC_PRODUCT_STRING         "LSM6DSV USB CDC"
#endif
#define USBD_CDC_DESC_CONFIGURATION_STRING   "Virtual ComPort Config"
#ifndef USBD_CDC_DESC_INTERFACE_STRING
#define USBD_CDC_DESC_INTERFACE_STRING       "LSM6DSV WebUSB"
#endif

/**
  * @brief usb endpoint interval define
  */
#define CDC_HID_BINTERVAL_TIME                0xFF

/**
  * @brief usb mcu id address deine
  */
#define         MCU_ID1                   (0x1FFFF7E8)
#define         MCU_ID2                   (0x1FFFF7EC)
#define         MCU_ID3                   (0x1FFFF7F0)
/**
  * @}
  */

extern usbd_desc_handler cdc_desc_handler;

/* BOS (GET_DESCRIPTOR type 0x0F) and MS OS 2.0 descriptor set accessors. */
usbd_desc_t *cdc_get_bos_descriptor(void);
usbd_desc_t *cdc_get_ms_os_20_descriptor_set(void);


/**
  * @}
  */

/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif

