/**
 * @file delay.c
 * @ingroup platform
 * @brief Lightweight non-blocking software timers (implementation).
 * @version 0.2
 * @date 2025-10-24
 */

#include "delay.h"

static Delay_t delay_timers[MAX_TIMERS];
static bool timer_allocated[MAX_TIMERS];
static uint32_t num_allocated;

/**
 * @brief Initialize delay system
 */
void delay_init(void) {
  num_allocated = 0;

  for (uint32_t i = 0; i < MAX_TIMERS; i++) {
    timer_allocated[i] = false;
    delay_timers[i].is_running = false;
    delay_timers[i].start_time = 0;
    delay_timers[i].duration_ms = 0;
  }
}

/**
 * @brief Create a new timer
 * @return Timer index (0 to MAX_TIMERS-1) or -1 on error
 */
int32_t delay_timer_create(void) {
  if (num_allocated >= MAX_TIMERS) {
    return -1;  // No slots available
  }

  // Find first free slot
  for (uint32_t i = 0; i < MAX_TIMERS; i++) {
    if (!timer_allocated[i]) {
      timer_allocated[i] = true;
      delay_timers[i].is_running = false;
      delay_timers[i].start_time = 0;
      delay_timers[i].duration_ms = 0;
      num_allocated++;
      return (int32_t)i;
    }
  }

  return -1;  // Should never reach here
}

/**
 * @brief Destroy a timer and free its slot
 * @param timer_idx Timer index to destroy
 */
void delay_timer_destroy(int32_t timer_idx) {
  if (timer_idx < 0 || timer_idx >= MAX_TIMERS) return;
  if (!timer_allocated[timer_idx]) return;

  timer_allocated[timer_idx] = false;
  delay_timers[timer_idx].is_running = false;

  if (num_allocated > 0) {
    num_allocated--;
  }
}

/**
 * @brief Start a delay timer
 * @param timer_idx Timer index
 * @param milliseconds Duration in milliseconds
 */
void delay_start(int32_t timer_idx, uint32_t milliseconds) {
  if (timer_idx < 0 || timer_idx >= MAX_TIMERS) return;
  if (!timer_allocated[timer_idx]) return;

  delay_timers[timer_idx].start_time = HAL_GetTick();
  delay_timers[timer_idx].duration_ms = milliseconds;
  delay_timers[timer_idx].is_running = true;
}

/**
 * @brief Check if delay has finished
 * @param timer_idx Timer index
 * @return true if delay finished, false otherwise
 *
 * @note Handles HAL_GetTick() overflow correctly
 */
bool delay_has_finished(int32_t timer_idx) {
  if (timer_idx < 0 || timer_idx >= MAX_TIMERS) return false;
  if (!timer_allocated[timer_idx]) return false;
  if (!delay_timers[timer_idx].is_running) return false;

  // Calculate elapsed time (handles overflow correctly)
  uint32_t current = HAL_GetTick();
  uint32_t elapsed = current - delay_timers[timer_idx].start_time;

  if (elapsed >= delay_timers[timer_idx].duration_ms) {
    delay_timers[timer_idx].is_running = false;
    return true;
  }

  return false;
}

/**
 * @brief Stop a running timer
 * @param timer_idx Timer index
 */
void delay_stop(int32_t timer_idx) {
  if (timer_idx < 0 || timer_idx >= MAX_TIMERS) return;
  if (!timer_allocated[timer_idx]) return;

  delay_timers[timer_idx].is_running = false;
}

/**
 * @brief Get elapsed time since timer started
 * @param timer_idx Timer index
 * @return Elapsed time in milliseconds, or 0 if timer not running
 */
uint32_t delay_elapsed(int32_t timer_idx) {
  if (timer_idx < 0 || timer_idx >= MAX_TIMERS) return 0;
  if (!timer_allocated[timer_idx]) return 0;
  if (!delay_timers[timer_idx].is_running) return 0;

  uint32_t current = HAL_GetTick();
  return current - delay_timers[timer_idx].start_time;
}

/**
 * @brief Get remaining time until timer expires
 * @param timer_idx Timer index
 * @return Remaining time in milliseconds, or 0 if expired/not running
 */
uint32_t delay_remaining(int32_t timer_idx) {
  if (timer_idx < 0 || timer_idx >= MAX_TIMERS) return 0;
  if (!timer_allocated[timer_idx]) return 0;
  if (!delay_timers[timer_idx].is_running) return 0;

  uint32_t elapsed = delay_elapsed(timer_idx);

  if (elapsed >= delay_timers[timer_idx].duration_ms) {
    return 0;
  }

  return delay_timers[timer_idx].duration_ms - elapsed;
}