/**
 * @file storage.c
 * @brief Persistent storage using STM32L031 internal Data EEPROM (1 KB)
 *
 * Memory map:
 *   0x08080000 + 0x00: Magic number (4 bytes) - 0xDEADBEEF if valid
 *   0x08080000 + 0x04: device_id (16 bytes)
 *   0x08080000 + 0x14: mac (16 bytes)
 *   0x08080000 + 0x24: pulse_count (4 bytes)
 */

#include "storage.h"

#include <string.h>

#include "stm32l0xx_hal.h"
#include "stm32l0xx_hal_flash_ex.h"

static bool storage_registered = false;

/**
 * @brief Write a block of bytes to Data EEPROM
 */
static bool eeprom_write(uint32_t addr, const uint8_t *data, uint16_t len) {
  if (HAL_FLASHEx_DATAEEPROM_Unlock() != HAL_OK) return false;

  for (uint16_t i = 0; i < len; i++) {
    if (HAL_FLASHEx_DATAEEPROM_Program(FLASH_TYPEPROGRAMDATA_BYTE,
                                        addr + i, data[i]) != HAL_OK) {
      HAL_FLASHEx_DATAEEPROM_Lock();
      return false;
    }
  }

  HAL_FLASHEx_DATAEEPROM_Lock();
  return true;
}

/**
 * @brief Write a 32-bit word to Data EEPROM
 */
static bool eeprom_write_word(uint32_t addr, uint32_t value) {
  if (HAL_FLASHEx_DATAEEPROM_Unlock() != HAL_OK) return false;

  HAL_StatusTypeDef status = HAL_FLASHEx_DATAEEPROM_Program(
      FLASH_TYPEPROGRAMDATA_WORD, addr, value);

  HAL_FLASHEx_DATAEEPROM_Lock();
  return (status == HAL_OK);
}

/**
 * @brief Initialize storage module. Checks if valid credentials exist.
 */
bool Storage_init(void) {
  uint32_t magic = *(__IO uint32_t *)STORAGE_MAGIC_ADDR;
  storage_registered = (magic == STORAGE_MAGIC_VALUE);
  return true;
}

/**
 * @brief Check if device has been previously registered
 */
bool Storage_is_registered(void) {
  return storage_registered;
}

/**
 * @brief Save device credentials to EEPROM
 */
bool Storage_save_credentials(const uint8_t *device_id, const uint8_t *mac) {
  if (device_id == NULL || mac == NULL) return false;

  if (!eeprom_write(STORAGE_DEVID_ADDR, device_id, DEV_ID_BYTES)) return false;
  if (!eeprom_write(STORAGE_MAC_ADDR, mac, MAC_BYTES)) return false;

  /* Write magic last — if power is lost mid-write, credentials won't be
     considered valid on next boot */
  if (!eeprom_write_word(STORAGE_MAGIC_ADDR, STORAGE_MAGIC_VALUE)) return false;

  storage_registered = true;
  return true;
}

/**
 * @brief Load device credentials from EEPROM
 */
bool Storage_load_credentials(uint8_t *device_id, uint8_t *mac) {
  if (device_id == NULL || mac == NULL) return false;
  if (!storage_registered) return false;

  memcpy(device_id, (const void *)STORAGE_DEVID_ADDR, DEV_ID_BYTES);
  memcpy(mac, (const void *)STORAGE_MAC_ADDR, MAC_BYTES);
  return true;
}

/**
 * @brief Save pulse counter checkpoint to EEPROM
 */
bool Storage_save_pulse_count(uint32_t count) {
  return eeprom_write_word(STORAGE_PULSE_ADDR, count);
}

/**
 * @brief Load pulse counter from EEPROM (returns 0 if no valid data)
 */
uint32_t Storage_load_pulse_count(void) {
  if (!storage_registered) return 0;

  uint32_t count = *(__IO uint32_t *)STORAGE_PULSE_ADDR;
  /* Unwritten EEPROM reads as 0xFFFFFFFF */
  if (count == 0xFFFFFFFF) return 0;
  return count;
}

/**
 * @brief Erase all stored data (magic, credentials, pulse count)
 */
bool Storage_erase_all(void) {
  if (HAL_FLASHEx_DATAEEPROM_Unlock() != HAL_OK) return false;

  /* Write 0xFF to all used bytes (0x00 to 0x27 = 40 bytes) */
  for (uint16_t i = 0; i < 0x28; i++) {
    if (HAL_FLASHEx_DATAEEPROM_Program(FLASH_TYPEPROGRAMDATA_BYTE,
                                        STORAGE_BASE_ADDR + i, 0xFF) != HAL_OK) {
      HAL_FLASHEx_DATAEEPROM_Lock();
      return false;
    }
  }

  HAL_FLASHEx_DATAEEPROM_Lock();
  storage_registered = false;
  return true;
}
