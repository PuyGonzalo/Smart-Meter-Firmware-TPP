/**
 * @file delay.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-10-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef DELAY_H
#define DELAY_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "stm32l0xx_hal.h"

#define MAX_TIMERS 10

typedef struct {
    uint32_t start_time;
    uint32_t duration_ms;
    bool is_running;
} Delay_t;

void delay_init(void);
int32_t delay_timer_create(void);
void delay_timer_destroy(int32_t timer_idx);
void delay_start(int32_t timer_idx, uint32_t milliseconds);
bool delay_has_finished(int32_t timer_idx);
void delay_stop(int32_t timer_idx);
uint32_t delay_elapsed(int32_t timer_idx);
uint32_t delay_remaining(int32_t timer_idx);

#endif  // DELAY_H