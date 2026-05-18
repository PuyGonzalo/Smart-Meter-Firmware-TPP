/**
 * @file pulse_counter.c
 * @brief Pulse counter implementation.
 *        Increments a volatile counter on each EXTI rising edge from the
 *        water meter reed switch (PA8).
 */

#include "pulse_counter.h"

static volatile uint32_t pulse_count = 0;

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

void PulseCounter_irq_handler(void) {
  pulse_count++;
}
