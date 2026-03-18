/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
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
/* Includes ------------------------------------------------------------------*/
#include "rtc.h"

/* USER CODE BEGIN 0 */
#include <stdbool.h>

/* Days in each month (non-leap year). Index 0 unused. */
static const uint8_t days_in_month[] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool _is_leap_year(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief  Convert calendar date/time to UNIX epoch (seconds since 1970-01-01).
 *         Handles years 1970–2099 correctly.
 */
static uint64_t _calendar_to_epoch(uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hours, uint8_t minutes,
                                   uint8_t seconds) {
  uint32_t total_days = 0;

  /* Sum full years from 1970 to year-1 */
  for (uint16_t y = 1970; y < year; y++) {
    total_days += _is_leap_year(y) ? 366 : 365;
  }

  /* Sum full months in current year */
  for (uint8_t m = 1; m < month; m++) {
    total_days += days_in_month[m];
    if (m == 2 && _is_leap_year(year)) total_days++;
  }

  /* Add days in current month (day is 1-based) */
  total_days += (day - 1);

  return (uint64_t)total_days * 86400 + (uint64_t)hours * 3600 +
         (uint64_t)minutes * 60 + seconds;
}

/**
 * @brief  Convert UNIX epoch back to calendar components.
 */
static void _epoch_to_calendar(uint64_t epoch, uint16_t *year, uint8_t *month,
                               uint8_t *day, uint8_t *hours, uint8_t *minutes,
                               uint8_t *seconds) {
  uint32_t rem = (uint32_t)(epoch % 86400);
  *hours = rem / 3600;
  rem %= 3600;
  *minutes = rem / 60;
  *seconds = rem % 60;

  uint32_t total_days = (uint32_t)(epoch / 86400);
  uint16_t y = 1970;
  while (1) {
    uint16_t ydays = _is_leap_year(y) ? 366 : 365;
    if (total_days < ydays) break;
    total_days -= ydays;
    y++;
  }
  *year = y;

  uint8_t m = 1;
  while (m <= 12) {
    uint8_t mdays = days_in_month[m];
    if (m == 2 && _is_leap_year(y)) mdays++;
    if (total_days < mdays) break;
    total_days -= mdays;
    m++;
  }
  *month = m;
  *day = total_days + 1;
}

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 124;
  hrtc.Init.SynchPrediv = 295;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */
    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

void RTC_get_timestamp(uint32_t *ts_high, uint32_t *ts_low) {
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;

  /* Must read Time first, then Date — HAL locks the shadow registers */
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  /* RTC year is 0-99 offset from 2000 */
  uint16_t full_year = 2000 + sDate.Year;

  uint64_t epoch = _calendar_to_epoch(full_year, sDate.Month, sDate.Date,
                                      sTime.Hours, sTime.Minutes,
                                      sTime.Seconds);

  *ts_high = (uint32_t)(epoch >> 32);
  *ts_low = (uint32_t)(epoch & 0xFFFFFFFF);
}

bool RTC_set_datetime(uint32_t ts_high, uint32_t ts_low) {
  uint64_t epoch = ((uint64_t)ts_high << 32) | ts_low;

  uint16_t year;
  uint8_t month, day, hours, minutes, seconds;
  _epoch_to_calendar(epoch, &year, &month, &day, &hours, &minutes, &seconds);

  if (year < 2000 || year > 2099) return false;

  RTC_TimeTypeDef sTime = {0};
  sTime.Hours = hours;
  sTime.Minutes = minutes;
  sTime.Seconds = seconds;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) return false;

  RTC_DateTypeDef sDate = {0};
  sDate.Year = year - 2000;
  sDate.Month = month;
  sDate.Date = day;
  sDate.WeekDay = RTC_WEEKDAY_MONDAY; /* Not critical for our use */

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) return false;

  return true;
}

/* USER CODE END 1 */
