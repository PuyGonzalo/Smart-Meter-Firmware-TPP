/**
 * @file ATCom.h
 * @ingroup atcom
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief Public API of the ATCom module — orchestrates registration and
 *        periodic sessions against the HES through the BG95 modem.
 *
 * The internal FSMs (registration / UDP / session) live in
 * atcom_registration.h, atcom_udp.h and atcom_session.h respectively.
 *
 * @version 0.2
 * @date 2026-05-14
 */

#ifndef _ATCOM_H_
#define _ATCOM_H_

#include <stdbool.h>
#include <stdint.h>

#include "at_core.h"  /* re-exported to keep transitive includes in main.c */
#include "atcom_registration.h"
#include "atcom_session.h"

/**
 * @addtogroup atcom
 * @{
 */

/**
 * @brief Initialize the internal timers of the three FSMs (registration,
 *        UDP context and session). Call once at boot.
 */
void Com_Init(void);

/**
 * @brief Pop the wake-up delay (seconds) requested by the HES, then clear it.
 * @return Delay in seconds, or 0 if the HES has not provided a value.
 */
uint32_t Com_pop_pending_wake_seconds(void);

/** @} */

#endif /* _ATCOM_H_ */
