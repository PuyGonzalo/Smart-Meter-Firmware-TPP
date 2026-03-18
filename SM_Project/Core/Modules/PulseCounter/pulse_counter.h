/**
 * @file pulse_counter.h
 * @brief Pulse counter module for water meter reed switch input.
 *        Counts rising-edge interrupts on PULSE_INPUT_Pin (PA8).
 */

#ifndef PULSE_COUNTER_H
#define PULSE_COUNTER_H

#include <stdint.h>

#define LITERS_PER_PULSE 100

/**
 * @brief  Get the accumulated pulse count since last reset.
 * @retval Current pulse count
 */
uint32_t PulseCounter_get_count(void);

/**
 * @brief  Get the accumulated volume in liters.
 * @retval count * LITERS_PER_PULSE
 */
uint32_t PulseCounter_get_volume_liters(void);

/**
 * @brief  Reset the pulse counter to zero.
 *         Call after a successful data transmission.
 */
void PulseCounter_reset(void);

/**
 * @brief  Called from HAL_GPIO_EXTI_Callback when PULSE_INPUT_Pin triggers.
 *         Do NOT call this directly — it is invoked by the ISR path.
 */
void PulseCounter_irq_handler(void);

#endif /* PULSE_COUNTER_H */
