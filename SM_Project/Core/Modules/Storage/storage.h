/**
 * @file storage.h
 * @ingroup storage
 * @brief Persistent storage in the STM32L031 internal Data EEPROM
 *        (device credentials, IMEI and pulse-count checkpoint).
 */

#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stdbool.h>
#include <stdint.h>

#include "at_device.h"

/**
 * @addtogroup storage
 * @{
 */

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

#define STORAGE_IMEI_LEN  16  /**< IMEI buffer size: 15 digits + NUL. */

/**
 * @brief Initialize the module and cache whether valid credentials exist.
 * @return Always true (kept as bool for API symmetry).
 */
bool     Storage_init(void);

/**
 * @brief Whether the device has valid credentials stored (magic matches).
 * @retval true  Device was previously registered.
 * @retval false No valid credentials in EEPROM.
 */
bool     Storage_is_registered(void);

/**
 * @brief Persist device credentials. The magic value is written last so that
 *        a power loss mid-write does not leave partial credentials marked valid.
 * @param device_id 16-byte device id assigned by the HES.
 * @param mac        16-byte MAC assigned by the HES.
 * @retval true  Written successfully.
 * @retval false NULL argument or EEPROM programming error.
 */
bool     Storage_save_credentials(const uint8_t *device_id, const uint8_t *mac);

/**
 * @brief Load device credentials previously stored with
 *        Storage_save_credentials().
 * @param[out] device_id Buffer of at least ::DEV_ID_BYTES bytes.
 * @param[out] mac        Buffer of at least ::MAC_BYTES bytes.
 * @retval true  Credentials copied out.
 * @retval false NULL argument or device not registered.
 */
bool     Storage_load_credentials(uint8_t *device_id, uint8_t *mac);

/**
 * @brief Save the pulse-count checkpoint.
 * @param count Current accumulated pulse count.
 * @retval true  Written successfully.
 * @retval false EEPROM programming error.
 */
bool     Storage_save_pulse_count(uint32_t count);

/**
 * @brief Load the pulse-count checkpoint.
 * @return Stored pulse count, or 0 if none/erased (unwritten EEPROM reads 0xFF).
 */
uint32_t Storage_load_pulse_count(void);

/**
 * @brief Erase all stored data (credentials, pulse count and IMEI).
 * @retval true  Erased successfully; the device is now unregistered.
 * @retval false EEPROM programming error.
 */
bool     Storage_erase_all(void);

/**
 * @brief Whether an IMEI has been persisted (IMEI magic matches).
 * @retval true  An IMEI is stored.
 * @retval false No IMEI stored.
 */
bool     Storage_has_imei(void);

/**
 * @brief Persist the modem IMEI string.
 * @param imei NUL-terminated 15-digit IMEI string.
 * @retval true  Written successfully.
 * @retval false NULL argument or EEPROM programming error.
 */
bool     Storage_save_imei(const char *imei);

/**
 * @brief Load the persisted IMEI string into a caller buffer.
 * @param[out] out Destination buffer.
 * @param      cap Buffer capacity (must be >= ::STORAGE_IMEI_LEN).
 * @retval true  IMEI copied out (NUL-terminated).
 * @retval false NULL/short buffer or no IMEI stored.
 */
bool     Storage_load_imei(char *out, uint16_t cap);

/** @} */

#endif //_STORAGE_H_
