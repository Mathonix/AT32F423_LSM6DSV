/**
  **************************************************************************
  * @file     cdc_class.c
  * @brief    usb cdc class type
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
#include "usbd_core.h"
#include "cdc_class.h"
#include "cdc_desc.h"

/** @addtogroup AT32F423_middlewares_usbd_class
  * @{
  */

/** @defgroup USB_cdc_class
  * @brief usb device class cdc demo
  * @{
  */

/** @defgroup USB_cdc_class_private_functions
  * @{
  */

static usb_sts_type class_init_handler(void *udev);
static usb_sts_type class_clear_handler(void *udev);
static usb_sts_type class_setup_handler(void *udev, usb_setup_type *setup);
static usb_sts_type class_ept0_tx_handler(void *udev);
static usb_sts_type class_ept0_rx_handler(void *udev);
static usb_sts_type class_in_handler(void *udev, uint8_t ept_num);
static usb_sts_type class_out_handler(void *udev, uint8_t ept_num);
static usb_sts_type class_sof_handler(void *udev);
static usb_sts_type class_event_handler(void *udev, usbd_event_type event);

static usb_sts_type cdc_struct_init(cdc_struct_type *pcdc);
extern void usb_usart_config( linecoding_type linecoding);
static void usb_vcp_cmd_process(void *udev, uint8_t cmd, uint8_t *buff, uint16_t len);

linecoding_type linecoding =
{
  115200,
  0,
  0,
  8
};

/* cdc data struct */
cdc_struct_type cdc_struct;

/* usb device class handler */
usbd_class_handler cdc_class_handler =
{
  class_init_handler,
  class_clear_handler,
  class_setup_handler,
  class_ept0_tx_handler,
  class_ept0_rx_handler,
  class_in_handler,
  class_out_handler,
  class_sof_handler,
  class_event_handler,
  &cdc_struct
};
/**
  * @brief  initialize usb custom hid endpoint
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_init_handler(void *udev)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;

  /* init cdc struct */
  cdc_struct_init(pcdc);

  /* open in endpoint */
  usbd_ept_open(pudev, USBD_CDC_INT_EPT, EPT_INT_TYPE, USBD_CDC_CMD_MAXPACKET_SIZE);

  /* open in endpoint */
  usbd_ept_open(pudev, USBD_CDC_BULK_IN_EPT, EPT_BULK_TYPE, USBD_CDC_IN_MAXPACKET_SIZE);

  /* open out endpoint */
  usbd_ept_open(pudev, USBD_CDC_BULK_OUT_EPT, EPT_BULK_TYPE, USBD_CDC_OUT_MAXPACKET_SIZE);

  /* set out endpoint to receive status */
  usbd_ept_recv(pudev, USBD_CDC_BULK_OUT_EPT, pcdc->g_rx_buff, USBD_CDC_OUT_MAXPACKET_SIZE);

  /* interface 2: WebUSB vendor bulk pair */
  usbd_ept_open(pudev, USBD_WEBUSB_BULK_IN_EPT, EPT_BULK_TYPE, USBD_WEBUSB_MAXPACKET_SIZE);
  usbd_ept_open(pudev, USBD_WEBUSB_BULK_OUT_EPT, EPT_BULK_TYPE, USBD_WEBUSB_MAXPACKET_SIZE);
  usbd_ept_recv(pudev, USBD_WEBUSB_BULK_OUT_EPT, pcdc->g_webusb_rx_buff, USBD_WEBUSB_MAXPACKET_SIZE);

  return status;
}

/**
  * @brief  clear endpoint or other state
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_clear_handler(void *udev)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;

  /* close in endpoint */
  usbd_ept_close(pudev, USBD_CDC_INT_EPT);

  /* close in endpoint */
  usbd_ept_close(pudev, USBD_CDC_BULK_IN_EPT);

  /* close out endpoint */
  usbd_ept_close(pudev, USBD_CDC_BULK_OUT_EPT);

  usbd_ept_close(pudev, USBD_WEBUSB_BULK_IN_EPT);
  usbd_ept_close(pudev, USBD_WEBUSB_BULK_OUT_EPT);

  return status;
}

/**
  * @brief  usb device class setup request handler
  * @param  udev: to the structure of usbd_core_type
  * @param  setup: setup packet
  * @retval status of usb_sts_type
  */
static usb_sts_type class_setup_handler(void *udev, usb_setup_type *setup)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;

  /* A new SETUP cancels any pending control OUT transaction. */
  pcdc->g_req = 0U;
  pcdc->g_len = 0U;
  switch(setup->bmRequestType & USB_REQ_TYPE_RESERVED)
  {
    /* class request */
    case USB_REQ_TYPE_CLASS:
      /* CDC line coding is exactly seven bytes. The control buffer is only
       * eight bytes long; never let host supplied wLength reach the USB
       * transfer engine unchecked. */
      if(setup->wIndex != 0U ||
         (setup->bmRequestType & USB_REQ_RECIPIENT_MASK) != USB_REQ_RECIPIENT_INTERFACE)
      {
        usbd_ctrl_unsupport(pudev);
        return USB_FAIL;
      }
      if(setup->bRequest == GET_LINE_CODING &&
         (setup->bmRequestType & USB_REQ_DIR_DTH) &&
         setup->wLength == 7U && setup->wValue == 0U)
      {
        usb_vcp_cmd_process(udev, setup->bRequest, pcdc->g_cmd, 7U);
        usbd_ctrl_send(pudev, pcdc->g_cmd, 7U);
      }
      else if(setup->bRequest == SET_LINE_CODING &&
              !(setup->bmRequestType & USB_REQ_DIR_DTH) &&
              setup->wLength == 7U && setup->wValue == 0U)
      {
        pcdc->g_req = setup->bRequest;
        pcdc->g_len = 7U;
        usbd_ctrl_recv(pudev, pcdc->g_cmd, 7U);
      }
      else if(setup->bRequest == 0x22U && /* SET_CONTROL_LINE_STATE (DTR/RTS) */
              !(setup->bmRequestType & USB_REQ_DIR_DTH) &&
              setup->wLength == 0U && (setup->wValue & ~3U) == 0U)
      {
        /* No UART modem lines; the core sends the zero-length status ACK. */
      }
      else
      {
        usbd_ctrl_unsupport(pudev);
        return USB_FAIL;
      }
      break;
    /* standard request */
    case USB_REQ_TYPE_STANDARD:
      switch(setup->bRequest)
      {
        case USB_STD_REQ_GET_DESCRIPTOR:
          /* The core forwards BOS (type 0x0F) here; everything else stalls. */
          if((setup->wValue >> 8) == USB_DESCIPTOR_TYPE_BOS &&
             (setup->wValue & 0xFFU) == 0U &&
             setup->bmRequestType == (USB_REQ_DIR_DTH | USB_REQ_TYPE_STANDARD | USB_REQ_RECIPIENT_DEVICE) &&
             setup->wLength != 0U)
          {
            usbd_desc_t *bos = cdc_get_bos_descriptor();
            usbd_ctrl_send(pudev, bos->descriptor, MIN(bos->length, setup->wLength));
          }
          else
          {
            usbd_ctrl_unsupport(pudev);
            status = USB_FAIL;
          }
          break;
        case USB_STD_REQ_GET_INTERFACE:
          /* every interface (0..2) has only alternate setting 0 */
          if(setup->wIndex > USBD_WEBUSB_INTERFACE) { usbd_ctrl_unsupport(pudev); break; }
          usbd_ctrl_send(pudev, (uint8_t *)&pcdc->alt_setting, 1);
          break;
        case USB_STD_REQ_SET_INTERFACE:
          if(setup->wIndex > USBD_WEBUSB_INTERFACE || setup->wValue != 0U)
          { usbd_ctrl_unsupport(pudev); break; }
          pcdc->alt_setting = 0U;
          break;
        case USB_STD_REQ_CLEAR_FEATURE:
          break;
        case USB_STD_REQ_SET_FEATURE:
          break;
        default:
          usbd_ctrl_unsupport(pudev);
          break;
      }
      break;
    /* vendor request: only the MS OS 2.0 descriptor set (device, IN, wIndex 7) */
    case USB_REQ_TYPE_VENDOR:
      if(setup->bmRequestType == (USB_REQ_DIR_DTH | USB_REQ_TYPE_VENDOR | USB_REQ_RECIPIENT_DEVICE) &&
         setup->bRequest == USBD_MS_OS_20_VENDOR_CODE &&
         setup->wIndex == USBD_MS_OS_20_DESCRIPTOR_INDEX &&
         setup->wValue == 0U && setup->wLength != 0U)
      {
        usbd_desc_t *set = cdc_get_ms_os_20_descriptor_set();
        usbd_ctrl_send(pudev, set->descriptor, MIN(set->length, setup->wLength));
      }
      else
      {
        usbd_ctrl_unsupport(pudev);
        status = USB_FAIL;
      }
      break;
    default:
      usbd_ctrl_unsupport(pudev);
      break;
  }
  return status;
}

/**
  * @brief  usb device endpoint 0 in status stage complete
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_ept0_tx_handler(void *udev)
{
  usb_sts_type status = USB_OK;

  /* ...user code... */

  return status;
}

/**
  * @brief  usb device endpoint 0 out status stage complete
  * @param  udev: usb device core handler type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_ept0_rx_handler(void *udev)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;
  uint32_t recv_len = usbd_get_recv_len(pudev, 0);
  /* ...user code... */
  if(pcdc->g_req == SET_LINE_CODING && pcdc->g_len == 7U && recv_len == 7U)
  {
    /* class process */
    usb_vcp_cmd_process(udev, pcdc->g_req, pcdc->g_cmd, recv_len);
  }
  pcdc->g_req = 0U;
  pcdc->g_len = 0U;
  return status;
}

/**
  * @brief  usb device transmision complete handler
  * @param  udev: to the structure of usbd_core_type
  * @param  ept_num: endpoint number
  * @retval status of usb_sts_type
  */
static usb_sts_type class_in_handler(void *udev, uint8_t ept_num)
{
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;
  usb_sts_type status = USB_OK;

  /* ...user code...
    trans next packet data
  */
  ept_num &= 0x7FU;
  if(ept_num == (USBD_CDC_BULK_IN_EPT & 0x7FU))
  {
    usbd_flush_tx_fifo(pudev, ept_num);
    pcdc->g_tx_completed = 1;
  }
  else if(ept_num == (USBD_WEBUSB_BULK_IN_EPT & 0x7FU))
  {
    usbd_flush_tx_fifo(pudev, ept_num);
    pcdc->g_webusb_tx_completed = 1;
  }
  /* EP2 (CDC notification) is never armed; ignore anything else */

  return status;
}

/**
  * @brief  usb device endpoint receive data
  * @param  udev: to the structure of usbd_core_type
  * @param  ept_num: endpoint number
  * @retval status of usb_sts_type
  */
static usb_sts_type class_out_handler(void *udev, uint8_t ept_num)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;

  ept_num &= 0x7FU;
  if(ept_num == USBD_CDC_BULK_OUT_EPT)
  {
    /* get endpoint receive data length  */
    pcdc->g_rxlen = usbd_get_recv_len(pudev, ept_num);
    /*set recv flag*/
    pcdc->g_rx_completed = 1;
  }
  else if(ept_num == USBD_WEBUSB_BULK_OUT_EPT)
  {
    uint32_t n = usbd_get_recv_len(pudev, ept_num);
    pcdc->g_webusb_rxlen = (uint16_t)(n > USBD_WEBUSB_MAXPACKET_SIZE ? USBD_WEBUSB_MAXPACKET_SIZE : n);
    pcdc->g_webusb_rx_completed = 1;
  }

  return status;
}

/**
  * @brief  usb device sof handler
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_sof_handler(void *udev)
{
  usb_sts_type status = USB_OK;

  /* ...user code... */

  return status;
}

/**
  * @brief  usb device event handler
  * @param  udev: to the structure of usbd_core_type
  * @param  event: usb device event
  * @retval status of usb_sts_type
  */
static usb_sts_type class_event_handler(void *udev, usbd_event_type event)
{
  usb_sts_type status = USB_OK;
  switch(event)
  {
    case USBD_RESET_EVENT:

      /* ...user code... */

      break;
    case USBD_SUSPEND_EVENT:

      /* ...user code... */

      break;
    case USBD_WAKEUP_EVENT:
      /* ...user code... */

      break;
    case USBD_INISOINCOM_EVENT:
      break;
    case USBD_OUTISOINCOM_EVENT:
      break;

    default:
      break;
  }
  return status;
}

/**
  * @brief  usb device cdc init
  * @param  pcdc: to the structure of cdc_struct
  * @retval status of usb_sts_type
  */
static usb_sts_type cdc_struct_init(cdc_struct_type *pcdc)
{
  pcdc->g_tx_completed = 1;
  pcdc->g_rx_completed = 0;
  pcdc->g_webusb_tx_completed = 1;
  pcdc->g_webusb_rx_completed = 0;
  pcdc->g_webusb_rxlen = 0U;
  pcdc->alt_setting = 0;
  pcdc->g_req = 0U;
  pcdc->g_len = 0U;
  pcdc->linecoding.bitrate = linecoding.bitrate;
  pcdc->linecoding.data = linecoding.data;
  pcdc->linecoding.format = linecoding.format;
  pcdc->linecoding.parity = linecoding.parity;
  return USB_OK;
}

/**
  * @brief  usb device class rx data process
  * @param  udev: to the structure of usbd_core_type
  * @param  recv_data: receive buffer
  * @retval receive data len
  */
uint16_t usb_vcp_get_rxdata(void *udev, uint8_t *recv_data)
{
  uint16_t i_index = 0;
  uint16_t tmp_len = 0;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;

  if(pcdc->g_rx_completed == 0)
  {
    return 0;
  }
  pcdc->g_rx_completed = 0;
  tmp_len = pcdc->g_rxlen;
  for(i_index = 0; i_index < pcdc->g_rxlen; i_index ++)
  {
    recv_data[i_index] = pcdc->g_rx_buff[i_index];
  }

  usbd_ept_recv(pudev, USBD_CDC_BULK_OUT_EPT, pcdc->g_rx_buff, USBD_CDC_OUT_MAXPACKET_SIZE);

  return tmp_len;
}

/**
  * @brief  usb device class send data
  * @param  udev: to the structure of usbd_core_type
  * @param  send_data: send data buffer
  * @param  len: send length
  * @retval error status
  */
error_status usb_vcp_send_data(void *udev, uint8_t *send_data, uint16_t len)
{
  error_status status = SUCCESS;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;
  if(pcdc->g_tx_completed)
  {
    pcdc->g_tx_completed = 0;
    usbd_ept_send(pudev, USBD_CDC_BULK_IN_EPT, send_data, len);
  }
  else
  {
    status = ERROR;
  }
  return status;
}

/* WebUSB interface 2: same contract as the VCP helpers above, separate state.
 * The OUT endpoint is re-armed only after the packet has been copied out, so
 * the host is NAKed (back-pressured) while the firmware ring is busy. */
uint16_t usb_webusb_get_rxdata(void *udev, uint8_t *recv_data)
{
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;
  uint16_t n;
  if(pcdc->g_webusb_rx_completed == 0) return 0;
  pcdc->g_webusb_rx_completed = 0;
  n = pcdc->g_webusb_rxlen;
  for(uint16_t i = 0; i < n; ++i) recv_data[i] = pcdc->g_webusb_rx_buff[i];
  usbd_ept_recv(pudev, USBD_WEBUSB_BULK_OUT_EPT, pcdc->g_webusb_rx_buff, USBD_WEBUSB_MAXPACKET_SIZE);
  return n;
}

error_status usb_webusb_send_data(void *udev, uint8_t *send_data, uint16_t len)
{
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;
  if(len > USBD_WEBUSB_MAXPACKET_SIZE || !pcdc->g_webusb_tx_completed) return ERROR;
  pcdc->g_webusb_tx_completed = 0;
  usbd_ept_send(pudev, USBD_WEBUSB_BULK_IN_EPT, send_data, len);
  return SUCCESS;
}

/**
  * @brief  usb device function
  * @param  udev: to the structure of usbd_core_type
  * @param  cmd: request number
  * @param  buff: request buffer
  * @param  len: buffer length
  * @retval none
  */
static void usb_vcp_cmd_process(void *udev, uint8_t cmd, uint8_t *buff, uint16_t len)
{
  usbd_core_type *pudev = (usbd_core_type *)udev;
  cdc_struct_type *pcdc = (cdc_struct_type *)pudev->class_handler->pdata;
  switch(cmd)
  {
    case SET_LINE_CODING:
      pcdc->linecoding.bitrate = (uint32_t)buff[0] | ((uint32_t)buff[1] << 8) |
                                ((uint32_t)buff[2] << 16) | ((uint32_t)buff[3] << 24);
      pcdc->linecoding.format = buff[4];
      pcdc->linecoding.parity = buff[5];
      pcdc->linecoding.data = buff[6];
#ifdef USB_VIRTUAL_COMPORT
      /* set hardware usart */
      usb_usart_config(pcdc->linecoding);
#endif
      break;

    case GET_LINE_CODING:
      buff[0] = (uint8_t)pcdc->linecoding.bitrate;
      buff[1] = (uint8_t)(pcdc->linecoding.bitrate >> 8);
      buff[2] = (uint8_t)(pcdc->linecoding.bitrate >> 16);
      buff[3] = (uint8_t)(pcdc->linecoding.bitrate >> 24);
      buff[4] = (uint8_t)(pcdc->linecoding.format);
      buff[5] = (uint8_t)(pcdc->linecoding.parity);
      buff[6] = (uint8_t)(pcdc->linecoding.data);
      break;

    default:
      break;
  }
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

