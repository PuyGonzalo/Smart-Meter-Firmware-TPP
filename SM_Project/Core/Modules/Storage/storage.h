/**
 * @file storage.h
 * @brief Persistent storage using STM32L031 internal Data EEPROM
 */

#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stdbool.h>
#include <stdint.h>

#include "at_device.h"

/* EEPROM memory map */
#define STORAGE_BASE_ADDR       DATA_EEPROM_BASE
#define STORAGE_MAGIC_ADDR      (STORAGE_BASE_ADDR + 0x00)
#define STORAGE_DEVID_ADDR      (STORAGE_BASE_ADDR + 0x04)
#define STORAGE_MAC_ADDR        (STORAGE_BASE_ADDR + 0x14)
#define STORAGE_PULSE_ADDR      (STORAGE_BASE_ADDR + 0x24)

#define STORAGE_MAGIC_VALUE     0xDEADBEEFUL

bool     Storage_init(void);
bool     Storage_is_registered(void);
bool     Storage_save_credentials(const uint8_t *device_id, const uint8_t *mac);
bool     Storage_load_credentials(uint8_t *device_id, uint8_t *mac);
bool     Storage_save_pulse_count(uint32_t count);
uint32_t Storage_load_pulse_count(void);

#endif //_STORAGE_H_
