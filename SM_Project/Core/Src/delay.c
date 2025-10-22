#include "delay.h"
#include <stdio.h>

static uint32_t num_timers;
static Delay_t delay_timers[MAX_TIMERS];
static bool any_timer_running;

/**
 * @brief delay_init Initialize the non-blocking delay
 * @warning This delay assumes that you are using SysTick as a system timebase source
 *          .<br>
 *          Also assumes that it is used with a configure 1ms tick.<br>
 *          If this is not the case, it's mandatory to configure SysTick in this
 *          delay_init() function.
 *
 */
void delay_init() {
  num_timers = 0;
  any_timer_running = false;

  /* Add SysTick initialization and config if needed.*/
}

/**
 * @brief delay_timer_create Creates a timer used for non-blocking delay.
 * @return Timer index.
 */
int32_t delay_timer_create(void) {
  if (num_timers >= MAX_TIMERS) return -1;

  delay_timers[num_timers].delay_counter = 0;
  delay_timers[num_timers].is_running = false;

  return num_timers++;
}

/**
 * @brief delay_start Stars a delay given by milliseconds.
 * @param timer_idx Timer index of the desire timer.
 * @param milliseconds Duration of the desire delay in milli seconds (ms).
 */
void delay_start(uint32_t timer_idx, uint32_t milliseconds) {
  if (timer_idx >= MAX_TIMERS) return;

  delay_timers[timer_idx].delay_counter = milliseconds;
  delay_timers[timer_idx].is_running = true;
  any_timer_running = true;
}

/**
 * @brief delay_has_finished Checks if delay time seted it's elapsed.
 * @param timer_idx Timer index of the desire timer.
 * @retval True if delay time elapsed
 * @retval False in case time it is not elapsed yet or no delay is active.
 */
bool delay_has_finished(uint32_t timer_idx) {
  if (timer_idx >= num_timers) return false;
  if (!any_timer_running) return false;

  uint32_t counter = delay_timers[timer_idx].delay_counter;

  if (counter == 0) {
    delay_timers[timer_idx].is_running = false;
    return true;
  }

  return false;
}

/**
 * @brief delay_update Delay timers update callback.
 * @warning This function must be called every 1 ms (SysTick interrupt).
 */
void delay_update(void) {
  if (!any_timer_running) return;

  any_timer_running = false;

  for (uint32_t i = 0; i < num_timers; i++) {
    if (delay_timers[i].is_running && delay_timers[i].delay_counter > 0) {
      delay_timers[i].delay_counter--;
    } else if (delay_timers[i].delay_counter == 0) {
      delay_timers[i].is_running = false;
    }
  }
}

void check_delay_counter(uint32_t timer_idx){
  // printf("\r\nDelay counter: %d\r\n", delay_timers[timer_idx].delay_counter);
}
