/**
 * @file at_device.h
 * @ingroup atcore
 * @brief Core data types shared by ATCore: the message envelope, the AT command
 *        descriptor and the command lookup-table entry.
 *
 * @version 0.2
 * @date 2025-10-24
 */

#ifndef _AT_DEVICE_H_
#define _AT_DEVICE_H_

#include <stdbool.h>
#include <stdint.h>
#include "bg95.h"
#include "stm32l0xx_hal.h"
#include "bg95_at_cmd_lib.h"

/**
 * @addtogroup atcore
 * @{
 */

#define ATCMD_MAX_PARAM_SIZE 10       /**< Max numeric parameters per command. */
#define ATCMD_MAX_STRPARAM_SIZE 260   /**< Max size of the string-parameter buffer. */
#define DEV_ID_BYTES 16               /**< Device id length (bytes). */
#define MAC_BYTES 16                  /**< MAC length (bytes). */

/**
 * @brief Initializer for an ::atcmd_desc_t struct.
 *
 * @code
 * // Usage:
 * atcmd_desc_t cmd = (atcmd_desc_t)ATCMD_DESC_DEFAULT;
 * @endcode
 */
#define ATCMD_DESC_DEFAULT \
    { .id = 0, \
      .cmd_mode = AT_CMD_EXEC, \
      .envelope = NULL, \
      .envp_size = 0, \
      .num_params = {0}, \
      .str_params = {0}, \
      .nb_num_params = 0, \
      .nb_str_params = 0, \
      .total_params = 0, \
      .param_types = {AT_NO_PARAM}}

/** @brief Type of an AT command parameter. */
typedef enum {
    AT_PARAM_NUM,   /**< Numeric parameter. */
    AT_PARAM_STR,   /**< String parameter (quoted). */
    AT_NO_PARAM     /**< No parameter in this slot. */
} at_param_type_t;

/** @brief AT command syntax mode (how the command string is formatted). */
typedef enum {
    AT_CMD_TEST,           /**< Test: `AT<cmd>=?`. */
    AT_CMD_EXEC,           /**< Execute: `AT<cmd>`. */
    AT_CMD_READ,           /**< Read: `AT<cmd>?`. */
    AT_CMD_WRITE,          /**< Write: `AT<cmd>=<params>`. */
    AT_CMD_WRITE_OPT,      /**< Write with optional parameters. */
    AT_CMD_WRITE_DEFAULT,  /**< Write using default parameters. */
} at_cmd_mode_t;

/**
 * @brief Message envelope — the fixed 46-byte header exchanged with the HES.
 *
 * Serialized big-endian in this field order: version, msg_type, device_id,
 * seq, timestamp (high|low), mac. An optional RLP payload follows the header.
 */
typedef struct {
  uint8_t version;                /**< Protocol version. */
  uint8_t msg_type;               /**< Message type (see atcom_internal.h). */
  uint8_t device_id[DEV_ID_BYTES];/**< Device id assigned by the HES. */
  uint32_t seq;                   /**< Sequence number. */
  uint32_t timestamp_high;        /**< Unix time, high 32 bits. */
  uint32_t timestamp_low;         /**< Unix time, low 32 bits. */
  uint8_t mac[MAC_BYTES];         /**< Message authentication / device MAC. */
} envelope_t;

/** @brief Descriptor of an AT command to build and send. */
typedef struct
{
  uint32_t id;                                        /*!< Command id (index into the LUT). */
  uint32_t at_cmd_size;                               /*!< Computed command-string size. */
  at_cmd_mode_t cmd_mode;                             /*!< Command syntax mode. @see at_cmd_mode_t */
  envelope_t *envelope;                               /*!< Optional envelope to embed. */
  uint32_t envp_size;                                 /*!< Envelope size (bytes). */
  uint32_t num_params[ATCMD_MAX_PARAM_SIZE];          /*!< Numeric parameter values. */
  char str_params[ATCMD_MAX_STRPARAM_SIZE];           /*!< String parameter buffer. */
  uint8_t nb_num_params;                              /*!< Number of numeric parameters. */
  uint8_t nb_str_params;                              /*!< Number of string parameters. */
  uint8_t total_params;                               /*!< Total parameter count. */
  at_param_type_t param_types[ATCMD_MAX_PARAM_SIZE];  /*!< Type of each parameter slot. */
} atcmd_desc_t;

/** @brief One entry of the BG95 AT command lookup table. */
typedef struct {
  uint16_t cmd_id;                        /**< Command id. */
  const char *cmd_string;                 /**< AT command string (e.g. "+QIOPEN"). */
  uint16_t cmd_size;                      /**< Base size hint for the command. */
  void (*build)(atcmd_desc_t *atcmd_desc);/**< Optional builder that fills the descriptor. */
} BG95_at_LUT_t;

/** @} */

#endif //_AT_DEVICE_H_
