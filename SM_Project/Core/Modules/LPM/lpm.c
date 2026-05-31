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

/* The UART RX lines (USART2 RX on PB7, LPUART1 RX on PA3) float in STOP: the
 * modem is powered off and the level shifter disabled, so nothing drives them.
 * A floating pin with the input Schmitt buffer active can sink mA — dwarfing
 * the ~0.4 uA STOP target. Park all four UART pins (TX too) in analog mode for
 * the duration of STOP; the modem is off so no traffic is lost. They are safe
 * to toggle: power_off() already aborted any in-flight DMA via reset_rx(), and
 * the next power_on() re-arms reception on its first AT command. */
static void lpm_uart_pins_to_analog(void) {
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;

  g.Pin = GPIO_PIN_6 | GPIO_PIN_7;  /* USART2 TX/RX */
  HAL_GPIO_Init(GPIOB, &g);

  g.Pin = GPIO_PIN_2 | GPIO_PIN_3;  /* LPUART1 TX/RX */
  HAL_GPIO_Init(GPIOA, &g);
}

/* Restore the UART pins to the AF configuration CubeMX set in MX_*_UART_Init. */
static void lpm_uart_pins_restore(void) {
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  g.Pin = GPIO_PIN_6 | GPIO_PIN_7;  /* USART2 TX/RX */
  g.Alternate = GPIO_AF0_USART2;
  HAL_GPIO_Init(GPIOB, &g);

  g.Pin = GPIO_PIN_2 | GPIO_PIN_3;  /* LPUART1 TX/RX */
  g.Alternate = GPIO_AF6_LPUART1;
  HAL_GPIO_Init(GPIOA, &g);
}

void LPM_sleep_until_alarm(void) {
  do {
    /* Mask interrupts around STOP entry to close the race between arming the
     * RTC alarm and executing WFI: if the alarm (or a pulse EXTI) fired in the
     * window before WFI, its handler would clear the pending bit and WFI would
     * then sleep forever — the one-shot alarm is already consumed. With PRIMASK
     * set, a pending-but-masked interrupt still wakes the M0+ from WFI; the
     * handler runs at __enable_irq() below and sets alarm_fired before the
     * loop condition is tested. */
    __disable_irq();

    /* Suspend SysTick: its IRQ would wake us every 1 ms otherwise. */
    HAL_SuspendTick();
    lpm_uart_pins_to_analog();

    /* Enter STOP. WFI returns either when the RTC alarm fires or when any
     * other enabled EXTI (e.g. pulse counter on PA8) wakes us. */
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* On wake the sysclock falls back to MSI (~2 MHz). Restore the original
     * configuration before re-enabling SysTick (which depends on HCLK freq). */
    SystemClock_Config();
    lpm_uart_pins_restore();
    HAL_ResumeTick();

    /* Run the pending wake handler (RTC alarm / pulse) now, which sets
     * alarm_fired, then test the loop condition. */
    __enable_irq();
  } while (!RTC_alarm_fired());

  RTC_clear_alarm_flag();
}
