/**
 * @file delay.h
 * @ingroup platform
 * @brief Lightweight non-blocking software timers built on HAL_GetTick().
 *
 * A small pool of one-shot timers used by the FSMs to implement timeouts and
 * inter-step delays without blocking. Timer handles are indices into a fixed
 * pool; -1 denotes an invalid/unallocated handle.
 * See delay.c for the per-function documentation.
 *
 * @version 0.2
 * @date 2025-10-24
 */

#ifndef DELAY_H
#define DELAY_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "stm32l0xx_hal.h"

/**
 * @addtogroup platform
 * @{
 */

#define MAX_TIMERS 10   /**< Number of timers in the pool. */

/** @brief State of one software timer. */
typedef struct {
    uint32_t start_time;   /**< HAL_GetTick() value when started. */
    uint32_t duration_ms;  /**< Configured duration (ms). */
    bool is_running;       /**< Whether the timer is currently running. */
} Delay_t;

void delay_init(void);
int32_t delay_timer_create(void);
void delay_timer_destroy(int32_t timer_idx);
void delay_start(int32_t timer_idx, uint32_t milliseconds);
bool delay_has_finished(int32_t timer_idx);
void delay_stop(int32_t timer_idx);
uint32_t delay_elapsed(int32_t timer_idx);
uint32_t delay_remaining(int32_t timer_idx);

/** @} */

#endif  // DELAY_H
