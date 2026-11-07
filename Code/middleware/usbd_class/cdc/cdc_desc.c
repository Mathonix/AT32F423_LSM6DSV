/**
  **************************************************************************
  * @file     cdc_desc.c
  * @brief    usb cdc device descriptor
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
#include "stdio.h"
#include "usb_std.h"
#include "usbd_sdr.h"
#include "usbd_core.h"
#include "cdc_desc.h"

/** @addtogroup AT32F423_middlewares_usbd_class
  * @{
  */

/** @defgroup USB_cdc_desc
  * @brief usb device cdc descriptor
  * @{
  */

/** @defgroup USB_cdc_desc_private_functions
  * @{
  */

static usbd_desc_t *get_device_descriptor(void);
static usbd_desc_t *get_device_qualifier(void);
static usbd_desc_t *get_device_configuration(void);
static usbd_desc_t *get_device_other_speed(void);
static usbd_desc_t *get_device_lang_id(void);
static usbd_desc_t *get_device_manufacturer_string(void);
static usbd_desc_t *get_device_product_string(void);
static usbd_desc_t *get_device_serial_string(void);
static usbd_desc_t *get_device_interface_string(void);
static usbd_desc_t *get_device_config_string(void);

static uint16_t usbd_unicode_convert(uint8_t *string, uint8_t *unicode_buf);
static void usbd_int_to_unicode (uint32_t value , uint8_t *pbuf , uint8_t len);
static void get_serial_num(void);
#if defined ( __ICCARM__ ) /* iar compiler */
  #pragma data_alignment=4
#endif
ALIGNED_HEAD static uint8_t g_usbd_desc_buffer[256] ALIGNED_TAIL;

/**
  * @brief device descriptor handler structure
  */
usbd_desc_handler cdc_desc_handler =
{
  get_device_descriptor,
  get_device_qualifier,
  get_device_configuration,
  get_device_other_speed,
  get_device_lang_id,
  get_device_manufacturer_string,
  get_device_product_string,
  get_device_serial_string,
  get_device_interface_string,
  get_device_config_string,
};

/**
  * @brief usb device standard descriptor
  */
#if defined ( __ICCARM__ ) /* iar compiler */
  #pragma data_alignment=4
#endif
ALIGNED_HEAD static uint8_t g_usbd_descriptor[USB_DEVICE_DESC_LEN] ALIGNED_TAIL =
{
  USB_DEVICE_DESC_LEN,                   /* bLength */
  USB_DESCIPTOR_TYPE_DEVICE,             /* bDescriptorType */
  LBYTE(USBD_CDC_BCD_USB),               /* bcdUSB 2.10: host reads BOS */
  HBYTE(USBD_CDC_BCD_USB),
  0xEF,                                  /* bDeviceClass: miscellaneous (IAD composite) */
  0x02,                                  /* bDeviceSubClass: common class */
  0x01,                                  /* bDeviceProtocol: interface association */
  USB_MAX_EP0_SIZE,                      /* bMaxPacketSize */
  LBYTE(USBD_CDC_VENDOR_ID),             /* idVendor */
  HBYTE(USBD_CDC_VENDOR_ID),             /* idVendor */
  LBYTE(USBD_CDC_PRODUCT_ID),            /* idProduct */
  HBYTE(USBD_CDC_PRODUCT_ID),            /* idProduct */
  LBYTE(USBD_CDC_BCD_DEVICE),            /* bcdDevice rel. 2.01 (bumped for MS OS 2.0) */
  HBYTE(USBD_CDC_BCD_DEVICE),
  USB_MFC_STRING,                        /* Index of manufacturer string */
  USB_PRODUCT_STRING,                    /* Index of product string */
  USB_SERIAL_STRING,                     /* Index of serial number string */
  1                                      /* bNumConfigurations */
};

/**
  * @brief usb configuration standard descriptor
  */
#if defined ( __ICCARM__ ) /* iar compiler */
  #pragma data_alignment=4
#endif
ALIGNED_HEAD static uint8_t g_usbd_configuration[USBD_CDC_CONFIG_DESC_SIZE] ALIGNED_TAIL =
{
  USB_DEVICE_CFG_DESC_LEN,               /* bLength: configuration descriptor size */
  USB_DESCIPTOR_TYPE_CONFIGURATION,      /* bDescriptorType: configuration */
  LBYTE(USBD_CDC_CONFIG_DESC_SIZE),          /* wTotalLength: bytes returned */
  HBYTE(USBD_CDC_CONFIG_DESC_SIZE),          /* wTotalLength: bytes returned */
  USBD_COMPOSITE_NUM_INTERFACES,         /* bNumInterfaces: CDC comm + CDC data + WebUSB */
  0x01,                                  /* bConfigurationValue: configuration value */
  0x00,                                  /* iConfiguration: index of string descriptor describing
                                            the configuration */
  0xC0,                                  /* bmAttributes: self powered */
  0x32,                                  /* MaxPower 100 mA: this current is used for detecting vbus */

  /* IAD: interfaces 0..1 form one CDC ACM function */
  USB_IAD_DESC_LEN,                      /* bLength */
  0x0B,                                  /* bDescriptorType: interface association */
  0x00,                                  /* bFirstInterface */
  0x02,                                  /* bInterfaceCount */
  USB_CLASS_CODE_CDC,                    /* bFunctionClass */
  0x02,                                  /* bFunctionSubClass: ACM */
  0x01,                                  /* bFunctionProtocol */
  0x00,                                  /* iFunction */

  USB_DEVICE_IF_DESC_LEN,                /* bLength: interface descriptor size */
  USB_DESCIPTOR_TYPE_INTERFACE,          /* bDescriptorType: interface descriptor type */
  0x00,                                  /* bInterfaceNumber: number of interface */
  0x00,                                  /* bAlternateSetting: alternate set */
  0x01,                                  /* bNumEndpoints: number of endpoints */
  USB_CLASS_CODE_CDC,                    /* bInterfaceClass: CDC class code */
  0x02,                                  /* bInterfaceSubClass: subclass code, Abstract Control Model*/
  0x01,                                  /* bInterfaceProtocol: protocol code, AT Command */
  0x00,                                  /* iInterface: index of string descriptor */

  0x05,                                  /* bFunctionLength: size of this descriptor in bytes */
  USBD_CDC_CS_INTERFACE,                 /* bDescriptorType: CDC interface descriptor type */
  USBD_CDC_SUBTYPE_HEADER,               /* bDescriptorSubtype: Header function Descriptor 0x00*/
  LBYTE(CDC_BCD_NUM),
  HBYTE(CDC_BCD_NUM),                    /* bcdCDC: USB class definitions for communications */

  0x05,                                  /* bFunctionLength: size of this descriptor in bytes */
  USBD_CDC_CS_INTERFACE,                 /* bDescriptorType: CDC interface descriptor type */
  USBD_CDC_SUBTYPE_CMF,                  /* bDescriptorSubtype: Call Management function descriptor subtype 0x01 */
  0x00,                                  /* bmCapabilities: 0x00*/
  0x01,                                  /* bDataInterface: interface number of data class interface optionally used for call management */

  0x04,                                  /* bFunctionLength: size of this descriptor in bytes */
  USBD_CDC_CS_INTERFACE,                 /* bDescriptorType: CDC interface descriptor type */
  USBD_CDC_SUBTYPE_ACM,                  /* bDescriptorSubtype: Abstract Control Management functional descriptor subtype 0x02 */
  0x02,                                  /* bmCapabilities: Support Set_Line_Coding and Get_Line_Coding 0x02 */

  0x05,                                  /* bFunctionLength: size of this descriptor in bytes */
  USBD_CDC_CS_INTERFACE,                 /* bDescriptorType: CDC interface descriptor type */
  USBD_CDC_SUBTYPE_UFD,                  /* bDescriptorSubtype: Union Function Descriptor subtype 0x06 */
  0x00,                                  /* bControlInterface: The interface number of the communications or data class interface 0x00 */
  0x01,                                  /* bSubordinateInterface0: interface number of first subordinate interface in the union */

  USB_DEVICE_EPT_LEN,                    /* bLength: size of endpoint descriptor in bytes */
  USB_DESCIPTOR_TYPE_ENDPOINT,           /* bDescriptorType: endpoint descriptor type */
  USBD_CDC_INT_EPT,                       /* bEndpointAddress: the address of endpoint on usb device described by this descriptor */
  USB_EPT_DESC_INTERRUPT,                /* bmAttributes: endpoint attributes */
  LBYTE(USBD_CDC_CMD_MAXPACKET_SIZE),
  HBYTE(USBD_CDC_CMD_MAXPACKET_SIZE),    /* wMaxPacketSize: maximum packe size this endpoint */
  CDC_HID_BINTERVAL_TIME,                    /* bInterval: interval for polling endpoint for data transfers */


  USB_DEVICE_IF_DESC_LEN,                /* bLength: interface descriptor size */
  USB_DESCIPTOR_TYPE_INTERFACE,          /* bDescriptorType: interface descriptor type */
  0x01,                                  /* bInterfaceNumber: number of interface */
  0x00,                                  /* bAlternateSetting: alternate set */
  0x02,                                  /* bNumEndpoints: number of endpoints */
  USB_CLASS_CODE_CDCDATA,                /* bInterfaceClass: CDC-data class code */
  0x00,                                  /* bInterfaceSubClass: Data interface subclass code 0x00*/
  0x00,                                  /* bInterfaceProtocol: data class protocol code 0x00 */
  0x00,                                  /* iInterface: index of string descriptor */

  USB_DEVICE_EPT_LEN,                    /* bLength: size of endpoint descriptor in bytes */
  USB_DESCIPTOR_TYPE_ENDPOINT,           /* bDescriptorType: endpoint descriptor type */
  USBD_CDC_BULK_IN_EPT,                  /* bEndpointAddress: the address of endpoint on usb device described by this descriptor */
  USB_EPT_DESC_BULK,                     /* bmAttributes: endpoint attributes */
  LBYTE(USBD_CDC_IN_MAXPACKET_SIZE),
  HBYTE(USBD_CDC_IN_MAXPACKET_SIZE),         /* wMaxPacketSize: maximum packe size this endpoint */
  0x00,                                  /* bInterval: interval for polling endpoint for data transfers */

  USB_DEVICE_EPT_LEN,                    /* bLength: size of endpoint descriptor in bytes */
  USB_DESCIPTOR_TYPE_ENDPOINT,           /* bDescriptorType: endpoint descriptor type */
  USBD_CDC_BULK_OUT_EPT,                 /* bEndpointAddress: the address of endpoint on usb device described by this descriptor */
  USB_EPT_DESC_BULK,                     /* bmAttributes: endpoint attributes */
  LBYTE(USBD_CDC_OUT_MAXPACKET_SIZE),
  HBYTE(USBD_CDC_OUT_MAXPACKET_SIZE),        /* wMaxPacketSize: maximum packe size this endpoint */
  0x00,                                  /* bInterval: interval for polling endpoint for data transfers */

  /* interface 2: vendor-specific WebUSB data interface */
  USB_DEVICE_IF_DESC_LEN,                /* bLength */
  USB_DESCIPTOR_TYPE_INTERFACE,          /* bDescriptorType */
  USBD_WEBUSB_INTERFACE,                 /* bInterfaceNumber */
  0x00,                                  /* bAlternateSetting */
  0x02,                                  /* bNumEndpoints */
  0xFF,                                  /* bInterfaceClass: vendor specific */
  0x00,                                  /* bInterfaceSubClass */
  0x00,                                  /* bInterfaceProtocol */
  USB_INTERFACE_STRING,                  /* iInterface: "LSM6DSV WebUSB" */

  USB_DEVICE_EPT_LEN,
  USB_DESCIPTOR_TYPE_ENDPOINT,
  USBD_WEBUSB_BULK_IN_EPT,               /* bEndpointAddress: 0x83 */
  USB_EPT_DESC_BULK,
  LBYTE(USBD_WEBUSB_MAXPACKET_SIZE),
  HBYTE(USBD_WEBUSB_MAXPACKET_SIZE),
  0x00,

  USB_DEVICE_EPT_LEN,
  USB_DESCIPTOR_TYPE_ENDPOINT,
  USBD_WEBUSB_BULK_OUT_EPT,              /* bEndpointAddress: 0x03 */
  USB_EPT_DESC_BULK,
  LBYTE(USBD_WEBUSB_MAXPACKET_SIZE),
  HBYTE(USBD_WEBUSB_MAXPACKET_SIZE),
  0x00,
};

/* wTotalLength must equal the bytes actually emitted. */
typedef char usbd_cdc_config_size_check[(sizeof(g_usbd_configuration) == USBD_CDC_CONFIG_DESC_SIZE) ? 1 : -1];

/**
  * @brief BOS descriptor: one MS OS 2.0 platform capability
  */
#if defined ( __ICCARM__ ) /* iar compiler */
  #pragma data_alignment=4
#endif
ALIGNED_HEAD static uint8_t g_usbd_bos[] ALIGNED_TAIL =
{
  USBD_BOS_HEADER_LEN,                   /* bLength */
  USB_DESCIPTOR_TYPE_BOS,                /* bDescriptorType: BOS */
  LBYTE(USBD_BOS_DESC_SIZE),             /* wTotalLength */
  HBYTE(USBD_BOS_DESC_SIZE),
  0x01,                                  /* bNumDeviceCaps */

  USBD_BOS_PLATFORM_CAP_LEN,             /* bLength */
  0x10,                                  /* bDescriptorType: device capability */
  0x05,                                  /* bDevCapabilityType: platform */
  0x00,                                  /* bReserved */
  /* PlatformCapabilityUUID {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F} */
  0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
  0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION),          /* dwWindowsVersion */
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION >> 8),
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION >> 16),
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION >> 24),
  LBYTE(USBD_MS_OS_20_DESC_SET_SIZE),    /* wMSOSDescriptorSetTotalLength */
  HBYTE(USBD_MS_OS_20_DESC_SET_SIZE),
  USBD_MS_OS_20_VENDOR_CODE,             /* bMS_VendorCode */
  0x00,                                  /* bAltEnumCode: no alternate enumeration */
};
typedef char usbd_bos_size_check[(sizeof(g_usbd_bos) == USBD_BOS_DESC_SIZE) ? 1 : -1];

/**
  * @brief MS OS 2.0 descriptor set: WinUSB + DeviceInterfaceGUIDs for
  *        function (interface) 2 only, so the CDC function keeps usbser.
  */
#if defined ( __ICCARM__ ) /* iar compiler */
  #pragma data_alignment=4
#endif
ALIGNED_HEAD static uint8_t g_usbd_ms_os_20[] ALIGNED_TAIL =
{
  /* set header */
  LBYTE(USBD_MS_OS_20_SET_HEADER_LEN), HBYTE(USBD_MS_OS_20_SET_HEADER_LEN), /* wLength */
  0x00, 0x00,                            /* wDescriptorType: MS_OS_20_SET_HEADER_DESCRIPTOR */
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION),          /* dwWindowsVersion */
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION >> 8),
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION >> 16),
  (uint8_t)(USBD_MS_OS_20_WINDOWS_VERSION >> 24),
  LBYTE(USBD_MS_OS_20_DESC_SET_SIZE), HBYTE(USBD_MS_OS_20_DESC_SET_SIZE),   /* wTotalLength */

  /* configuration subset header */
  LBYTE(USBD_MS_OS_20_CFG_SUBSET_LEN), HBYTE(USBD_MS_OS_20_CFG_SUBSET_LEN), /* wLength */
  0x01, 0x00,                            /* wDescriptorType: MS_OS_20_SUBSET_HEADER_CONFIGURATION */
  0x00,                                  /* bConfigurationValue: configuration index 0 */
  0x00,                                  /* bReserved */
  LBYTE(USBD_MS_OS_20_CFG_SUBSET_TOTAL), HBYTE(USBD_MS_OS_20_CFG_SUBSET_TOTAL), /* wTotalLength */

  /* function subset header: interface 2 (WebUSB vendor interface) */
  LBYTE(USBD_MS_OS_20_FUNC_SUBSET_LEN), HBYTE(USBD_MS_OS_20_FUNC_SUBSET_LEN), /* wLength */
  0x02, 0x00,                            /* wDescriptorType: MS_OS_20_SUBSET_HEADER_FUNCTION */
  USBD_WEBUSB_INTERFACE,                 /* bFirstInterface */
  0x00,                                  /* bReserved */
  LBYTE(USBD_MS_OS_20_FUNC_SUBSET_TOTAL), HBYTE(USBD_MS_OS_20_FUNC_SUBSET_TOTAL), /* wSubsetLength */

  /* compatible ID: WINUSB */
  LBYTE(USBD_MS_OS_20_COMPAT_ID_LEN), HBYTE(USBD_MS_OS_20_COMPAT_ID_LEN), /* wLength */
  0x03, 0x00,                            /* wDescriptorType: MS_OS_20_FEATURE_COMPATBLE_ID */
  'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,               /* CompatibleID */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         /* SubCompatibleID */

  /* registry property: DeviceInterfaceGUIDs (REG_MULTI_SZ) */
  LBYTE(USBD_MS_OS_20_REG_PROP_LEN), HBYTE(USBD_MS_OS_20_REG_PROP_LEN),   /* wLength */
  0x04, 0x00,                            /* wDescriptorType: MS_OS_20_FEATURE_REG_PROPERTY */
  0x07, 0x00,                            /* wPropertyDataType: REG_MULTI_SZ */
  LBYTE(USBD_MS_OS_20_PROP_NAME_LEN), HBYTE(USBD_MS_OS_20_PROP_NAME_LEN), /* wPropertyNameLength */
  'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
  'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
  'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
  'D', 0x00, 's', 0x00, 0x00, 0x00,
  LBYTE(USBD_MS_OS_20_PROP_DATA_LEN), HBYTE(USBD_MS_OS_20_PROP_DATA_LEN), /* wPropertyDataLength */
  '{', 0x00, '7', 0x00, 'B', 0x00, '9', 0x00, '2', 0x00, '6', 0x00,
  '4', 0x00, '8', 0x00, '6', 0x00, '-', 0x00, '7', 0x00, 'E', 0x00,
  'E', 0x00, 'E', 0x00, '-', 0x00, '4', 0x00, '9', 0x00, '9', 0x00,
  'C', 0x00, '-', 0x00, 'B', 0x00, 'F', 0x00, 'F', 0x00, 'C', 0x00,
  '-', 0x00, '1', 0x00, 'C', 0x00, 'A', 0x00, '5', 0x00, '9', 0x00,
  'C', 0x00, '7', 0x00, 'D', 0x00, 'D', 0x00, '9', 0x00, 'E', 0x00,
  'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};
typedef char usbd_ms_os_20_size_check[(sizeof(g_usbd_ms_os_20) == USBD_MS_OS_20_DESC_SET_SIZE) ? 1 : -1];
/* A reply that is an exact multiple of the 64-byte EP0 packet would need a
 * ZLP when wLength is larger; keep both control replies off that edge. */
typedef char usbd_ms_os_20_zlp_check[((USBD_MS_OS_20_DESC_SET_SIZE % USB_MAX_EP0_SIZE) != 0U &&
                                      (USBD_BOS_DESC_SIZE % USB_MAX_EP0_SIZE) != 0U) ? 1 : -1];

/**
  * @brief usb string lang id
  */
#if defined ( __ICCARM__ ) /* iar compiler */
  #pragma data_alignment=4
#endif
ALIGNED_HEAD static uint8_t g_string_lang_id[USBD_CDC_SIZ_STRING_LANGID] ALIGNED_TAIL =
{
  USBD_CDC_SIZ_STRING_LANGID,
  USB_DESCIPTOR_TYPE_STRING,
  0x09,
  0x04,
};

/**
  * @brief usb string serial
  */
#if defined ( __ICCARM__ ) /* iar compiler */
  #pragma data_alignment=4
#endif
ALIGNED_HEAD static uint8_t g_string_serial[USBD_CDC_SIZ_STRING_SERIAL] ALIGNED_TAIL =
{
  USBD_CDC_SIZ_STRING_SERIAL,
  USB_DESCIPTOR_TYPE_STRING,
};


/* device descriptor */
static usbd_desc_t device_descriptor =
{
  USB_DEVICE_DESC_LEN,
  g_usbd_descriptor
};

/* config descriptor */
static usbd_desc_t config_descriptor =
{
  USBD_CDC_CONFIG_DESC_SIZE,
  g_usbd_configuration
};

/* langid descriptor */
static usbd_desc_t langid_descriptor =
{
  USBD_CDC_SIZ_STRING_LANGID,
  g_string_lang_id
};

/* serial descriptor */
static usbd_desc_t serial_descriptor =
{
  USBD_CDC_SIZ_STRING_SERIAL,
  g_string_serial
};

static usbd_desc_t bos_descriptor =
{
  USBD_BOS_DESC_SIZE,
  g_usbd_bos
};

static usbd_desc_t ms_os_20_descriptor =
{
  USBD_MS_OS_20_DESC_SET_SIZE,
  g_usbd_ms_os_20
};

static usbd_desc_t vp_desc;

/**
  * @brief  get BOS descriptor (GET_DESCRIPTOR type 0x0F)
  * @param  none
  * @retval usbd_desc
  */
usbd_desc_t *cdc_get_bos_descriptor(void)
{
  return &bos_descriptor;
}

/**
  * @brief  get MS OS 2.0 descriptor set (vendor request, wIndex 7)
  * @param  none
  * @retval usbd_desc
  */
usbd_desc_t *cdc_get_ms_os_20_descriptor_set(void)
{
  return &ms_os_20_descriptor;
}

/**
  * @brief  standard usb unicode convert
  * @param  string: source string
  * @param  unicode_buf: unicode buffer
  * @retval length
  */
static uint16_t usbd_unicode_convert(uint8_t *string, uint8_t *unicode_buf)
{
  uint16_t str_len = 0, id_pos = 2;
  uint8_t *tmp_str = string;

  while(*tmp_str != '\0')
  {
    str_len ++;
    unicode_buf[id_pos ++] = *tmp_str ++;
    unicode_buf[id_pos ++] = 0x00;
  }

  str_len = str_len * 2 + 2;
  unicode_buf[0] = (uint8_t)str_len;
  unicode_buf[1] = USB_DESCIPTOR_TYPE_STRING;

  return str_len;
}

/**
  * @brief  usb int convert to unicode
  * @param  value: int value
  * @param  pbus: unicode buffer
  * @param  len: length
  * @retval none
  */
static void usbd_int_to_unicode (uint32_t value , uint8_t *pbuf , uint8_t len)
{
  uint8_t idx = 0;

  for( idx = 0 ; idx < len ; idx ++)
  {
    if( ((value >> 28)) < 0xA )
    {
      pbuf[ 2 * idx] = (value >> 28) + '0';
  }
  else
  {
      pbuf[2 * idx] = (value >> 28) + 'A' - 10;
    }

    value = value << 4;

    pbuf[2 * idx + 1] = 0;
  }
}

/**
  * @brief  usb get serial number
  * @param  none
  * @retval none
  */
static void get_serial_num(void)
{
  uint32_t serial0, serial1, serial2;

  serial0 = *(uint32_t*)MCU_ID1;
  serial1 = *(uint32_t*)MCU_ID2;
  serial2 = *(uint32_t*)MCU_ID3;

  serial0 += serial2;

  if (serial0 != 0)
  {
    usbd_int_to_unicode (serial0, &g_string_serial[2] ,8);
    usbd_int_to_unicode (serial1, &g_string_serial[18] ,4);
  }
}

/**
  * @brief  get device descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_descriptor(void)
{
  return &device_descriptor;
}

/**
  * @brief  get device qualifier
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t * get_device_qualifier(void)
{
  return NULL;
}

/**
  * @brief  get config descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_configuration(void)
{
  return &config_descriptor;
}

/**
  * @brief  get other speed descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_other_speed(void)
{
  return NULL;
}

/**
  * @brief  get lang id descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_lang_id(void)
{
  return &langid_descriptor;
}


/**
  * @brief  get manufacturer descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_manufacturer_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_CDC_DESC_MANUFACTURER_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}

/**
  * @brief  get product descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_product_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_CDC_DESC_PRODUCT_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}

/**
  * @brief  get serial descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_serial_string(void)
{
  get_serial_num();
  return &serial_descriptor;
}

/**
  * @brief  get interface descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_interface_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_CDC_DESC_INTERFACE_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}

/**
  * @brief  get device config descriptor
  * @param  none
  * @retval usbd_desc
  */
static usbd_desc_t *get_device_config_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_CDC_DESC_CONFIGURATION_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
