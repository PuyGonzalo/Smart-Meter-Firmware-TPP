/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.h
  * @brief   This file contains all the function prototypes for
  *          the rtc.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __RTC_H__
#define __RTC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdint.h>
/* USER CODE END Includes */

extern RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_RTC_Init(void);

/* USER CODE BEGIN Prototypes */

/**
 * @brief  Get current RTC time as a 64-bit UNIX timestamp (seconds since
 *         1970-01-01 00:00:00 UTC), split into two 32-bit halves matching
 *         the envelope format.
 * @param  ts_high  pointer to store upper 32 bits
 * @param  ts_low   pointer to store lower 32 bits
 */
void RTC_get_timestamp(uint32_t *ts_high, uint32_t *ts_low);

/**
 * @brief  Set RTC date/time from a 64-bit UNIX timestamp.
 * @param  ts_high  upper 32 bits of UNIX epoch
 * @param  ts_low   lower 32 bits of UNIX epoch
 * @retval true on success
 */
bool RTC_set_datetime(uint32_t ts_high, uint32_t ts_low);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __RTC_H__ */

