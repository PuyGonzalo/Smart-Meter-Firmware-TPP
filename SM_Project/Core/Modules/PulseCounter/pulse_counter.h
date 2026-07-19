/**
 * @file pulse_counter.h
 * @ingroup pulsecounter
 * @brief Pulse counter module for the water-meter pulse input on PA8.
 *        Counts rising-edge interrupts and converts them to a volume.
 */

#ifndef PULSE_COUNTER_H
#define PULSE_COUNTER_H

#include <stdint.h>

/**
 * @addtogroup pulsecounter
 * @{
 */

/** Volume of water, in liters, represented by a single meter pulse. */
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
 * @brief  Set the pulse counter to a specific value.
 *         Used to restore from EEPROM on boot.
 */
void PulseCounter_set_count(uint32_t count);

/**
 * @brief  Called from HAL_GPIO_EXTI_Callback when PULSE_INPUT_Pin triggers.
 *         Do NOT call this directly — it is invoked by the ISR path.
 */
void PulseCounter_irq_handler(void);

/** @} */

#endif /* PULSE_COUNTER_H */
