/**
  **************************************************************************
  * @file     at32f423_clock.h
  * @brief    header file of clock program
  **************************************************************************
  */

#ifndef __AT32F423_CLOCK_H
#define __AT32F423_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f423.h"

/* Build-time overrides used by dedicated SPI signal-integrity tests. */
#ifndef SYSTEM_CLOCK_TARGET_HZ
#define SYSTEM_CLOCK_TARGET_HZ 150000000U
#endif

#ifndef SYSTEM_APB2_DIV_VALUE
#define SYSTEM_APB2_DIV_VALUE 2U
#endif

void system_clock_config(void);

#ifdef __cplusplus
}
#endif

#endif
