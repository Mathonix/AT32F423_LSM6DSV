/**
  **************************************************************************
  * @file     at32f423_conf.h
  * @brief    at32f423 config header file
  **************************************************************************
  */

#ifndef __AT32F423_CONF_H
#define __AT32F423_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#if !defined  HEXT_VALUE
#define HEXT_VALUE                       ((uint32_t)8000000)
#endif

#define HEXT_STARTUP_TIMEOUT             ((uint16_t)0x3000)
#define HICK_VALUE                       ((uint32_t)8000000)
#define LEXT_VALUE                       ((uint32_t)32768)

#define CRM_MODULE_ENABLED
#define GPIO_MODULE_ENABLED
#define USART_MODULE_ENABLED
#define PWC_MODULE_ENABLED
#define SPI_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define MISC_MODULE_ENABLED
#define SCFG_MODULE_ENABLED
#define DMA_MODULE_ENABLED
#define CAN_MODULE_ENABLED
#define USB_MODULE_ENABLED

#ifdef CRM_MODULE_ENABLED
#include "at32f423_crm.h"
#endif
#ifdef GPIO_MODULE_ENABLED
#include "at32f423_gpio.h"
#endif
#ifdef USART_MODULE_ENABLED
#include "at32f423_usart.h"
#endif
#ifdef PWC_MODULE_ENABLED
#include "at32f423_pwc.h"
#endif
#ifdef SPI_MODULE_ENABLED
#include "at32f423_spi.h"
#endif
#ifdef FLASH_MODULE_ENABLED
#include "at32f423_flash.h"
#endif
#ifdef MISC_MODULE_ENABLED
#include "at32f423_misc.h"
#endif
#ifdef SCFG_MODULE_ENABLED
#include "at32f423_scfg.h"
#endif
#ifdef DMA_MODULE_ENABLED
#include "at32f423_dma.h"
#endif

#ifdef CAN_MODULE_ENABLED
#include "at32f423_can.h"
#endif

#ifdef USB_MODULE_ENABLED
#include "at32f423_usb.h"
#endif

#ifdef __cplusplus
}
#endif

#endif


