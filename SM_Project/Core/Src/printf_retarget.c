#include <stdio.h>
#include <unistd.h>

#include "usart.h"

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

extern UART_HandleTypeDef hlpuart1;

int _write(int file, char *data, int len) {
  if ((file != STDOUT_FILENO) && (file != STDERR_FILENO)) {
    return -1;
  }

  if (HAL_UART_Transmit(&hlpuart1, (uint8_t *)data, len, HAL_MAX_DELAY) == HAL_OK) {
    return len;
  }
  return -1;
}