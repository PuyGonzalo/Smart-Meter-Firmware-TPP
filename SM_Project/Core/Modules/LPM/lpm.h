/**
 * @file lpm.h
 * @brief Low-Power Mode helpers — STM32L0 STOP mode with RTC alarm wake-up.
 *
 * Wraps the entry/exit dance: SysTick suspend, STOP mode entry with low-power
 * regulator, clock re-init on wake, and a loop that re-enters STOP if the
 * wake-up was caused by something other than the RTC alarm (e.g. a pulse
 * counter EXTI).
 */

#ifndef _LPM_H_
#define _LPM_H_

#include <stdint.h>

/**
 * @brief One-shot configuration. Call once from main() after SystemClock_Config().
 *
 * Enables the PWR clock, configures DBGMCU to keep SWD alive during STOP in
 * DEBUG builds (~100 uA penalty, but lets you breakpoint post-wake), and
 * leaves the MCU ready to enter STOP at any time.
 */
void LPM_init(void);

/**
 * @brief Enter STOP mode (low-power regulator) and stay there until the RTC
 *        alarm fires. Pulse counter wake-ups (EXTI8 on PA8) are handled
 *        transparently: the ISR runs, then we re-enter STOP.
 *
 * Caller is responsible for arming the RTC alarm BEFORE calling this.
 * On return, the alarm flag has been cleared.
 *
 * Power: ~0.5 uA during STOP (LSE + RTC running).
 */
void LPM_sleep_until_alarm(void);

/**
 * @brief Sleep in STOP mode for @p total_sec seconds, refreshing the IWDG along
 *        the way. The sleep is split into chunks no longer than the watchdog
 *        timeout and the dog is fed at every wake, so the IWDG (which cannot be
 *        frozen in STOP on the STM32L0) does not reset us mid-sleep.
 *
 * Arms the RTC alarm internally — unlike LPM_sleep_until_alarm(), the caller
 * must NOT arm it. Pulse-counter EXTI wake-ups are handled transparently.
 * Returns once the full duration has elapsed.
 */
void LPM_sleep_seconds(uint32_t total_sec);

#endif  /* _LPM_H_ */
