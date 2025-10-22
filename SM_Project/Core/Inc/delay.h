#ifndef DELAY_H
#define DELAY_H

#include <stdbool.h>
#include <stdio.h>

#include "stm32l0xx.h"

#define MAX_TIMERS 5

typedef struct {
  volatile uint32_t delay_counter;
  bool is_running;
} Delay_t;

void delay_init();
int32_t delay_timer_create(void);
void delay_start(uint32_t timer_idx, uint32_t milliseconds);
bool delay_has_finished(uint32_t timer_idx);
void delay_update(void);
void check_delay_counter(uint32_t timer_idx);

#endif  // DELAY_H