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
#define STORAGE_BASE_ADDR         DATA_EEPROM_BASE
#define STORAGE_MAGIC_ADDR        (STORAGE_BASE_ADDR + 0x00)  /* 4 B */
#define STORAGE_DEVID_ADDR        (STORAGE_BASE_ADDR + 0x04)  /* 16 B */
#define STORAGE_MAC_ADDR          (STORAGE_BASE_ADDR + 0x14)  /* 16 B */
#define STORAGE_PULSE_ADDR        (STORAGE_BASE_ADDR + 0x24)  /* 4 B */
#define STORAGE_IMEI_MAGIC_ADDR   (STORAGE_BASE_ADDR + 0x28)  /* 4 B */
#define STORAGE_IMEI_ADDR         (STORAGE_BASE_ADDR + 0x2C)  /* 16 B (15 chars + NUL) */

#define STORAGE_MAGIC_VALUE       0xDEADBEEFUL
#define STORAGE_IMEI_MAGIC_VALUE  0xC0FFEE42UL

#define STORAGE_IMEI_LEN  16  /* 15 IMEI digits + NUL */

bool     Storage_init(void);
bool     Storage_is_registered(void);
bool     Storage_save_credentials(const uint8_t *device_id, const uint8_t *mac);
bool     Storage_load_credentials(uint8_t *device_id, uint8_t *mac);
bool     Storage_save_pulse_count(uint32_t count);
uint32_t Storage_load_pulse_count(void);
bool     Storage_erase_all(void);
bool     Storage_has_imei(void);
bool     Storage_save_imei(const char *imei);
bool     Storage_load_imei(char *out, uint16_t cap);

#endif //_STORAGE_H_
