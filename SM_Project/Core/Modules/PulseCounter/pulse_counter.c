/**
 * @file pulse_counter.c
 * @ingroup pulsecounter
 * @brief Pulse counter implementation.
 *        Increments a volatile counter on each EXTI rising edge from the
 *        water-meter pulse input (PA8), with RTC-based debouncing.
 */

#include "pulse_counter.h"

#include <stdbool.h>

#include "rtc.h"

/* Debounce dead-time in RTC sub-second ticks (256 ticks = 1 s, ~3.9 ms each).
 * Mechanical contacts (reed, and especially a hand-held test button) can chatter
 * both on make and on break, with the two bursts tens of ms apart; the window
 * must span a whole actuation to yield one count. Real meter pulses are seconds
 * to minutes apart (100 L/pulse), so a quarter-second lock-out has huge margin
 * and never drops a real pulse. The RTC is the time base because it keeps
 * running in STOP, whereas HAL_GetTick is frozen there. */
#define PULSE_DEBOUNCE_TICKS 64u  /* ~250 ms */

static volatile uint32_t pulse_count = 0;
static volatile uint32_t last_edge_ticks = 0;
static volatile bool have_last_edge = false;

uint32_t PulseCounter_get_count(void) {
  return pulse_count;
}

uint32_t PulseCounter_get_volume_liters(void) {
  return pulse_count * LITERS_PER_PULSE;
}

void PulseCounter_reset(void) {
  pulse_count = 0;
}

void PulseCounter_set_count(uint32_t count) {
  pulse_count = count;
}

void PulseCounter_consume(uint32_t n) {
  /* Read-modify-write shared with the EXTI ISR: mask interrupts so a pulse
   * arriving mid-update is not lost. */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  pulse_count = (pulse_count > n) ? (pulse_count - n) : 0U;
  __set_PRIMASK(primask);
}

void PulseCounter_irq_handler(void) {
  uint32_t now = RTC_get_subsecond_ticks();

  /* Re-arm the dead-time on every edge (including rejected bounce edges), so a
   * pulse is only counted once the line has been quiet for the full window. */
  bool settled = !have_last_edge ||
                 (uint32_t)(now - last_edge_ticks) >= PULSE_DEBOUNCE_TICKS;
  last_edge_ticks = now;
  have_last_edge = true;

  if (settled) {
    pulse_count++;
  }
}
