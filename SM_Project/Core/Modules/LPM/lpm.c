/**
 * @file lpm.c
 * @brief STM32L0 STOP mode implementation.
 */

#include "lpm.h"

#include "rtc.h"
#include "stm32l0xx_hal.h"

/* Provided by main.c — must be re-run after STOP to restore HSI16 + PLL */
extern void SystemClock_Config(void);

void LPM_init(void) {
  __HAL_RCC_PWR_CLK_ENABLE();

#ifdef DEBUG
  /* Keep SWD alive during STOP so we can breakpoint code that runs after
   * wake-up. Costs ~100 uA extra in STOP — DEBUG builds only. */
  HAL_DBGMCU_EnableDBGStopMode();
#endif

  /* Ultra-low-power: shut down V_REFINT during STOP (~0.1 uA extra savings).
   * Fast wake-up: skip the V_REFINT settling wait on exit. Safe because we
   * don't use ADC/comparator immediately after wake. */
  HAL_PWREx_EnableUltraLowPower();
  HAL_PWREx_EnableFastWakeUp();
}

void LPM_sleep_until_alarm(void) {
  do {
    /* Suspend SysTick: its IRQ would wake us every 1 ms otherwise. */
    HAL_SuspendTick();

    /* Enter STOP. WFI returns either when the RTC alarm fires or when any
     * other enabled EXTI (e.g. pulse counter on PA8) wakes us. */
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* On wake the sysclock falls back to MSI (~2 MHz). Restore the original
     * configuration before re-enabling SysTick (which depends on HCLK freq). */
    SystemClock_Config();
    HAL_ResumeTick();
  } while (!RTC_alarm_fired());

  RTC_clear_alarm_flag();
}
