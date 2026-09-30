/**
 * @file ATCom.c
 * @ingroup atcom
 * @author Gonzalo Puy (gpuy@fi.uba.ar)
 * @brief Initialization glue and shared storage across the ATCom sub-modules.
 *        The actual logic lives in atcom_udp.c, atcom_registration.c and
 *        atcom_session.c.
 *
 * @version 0.2
 * @date 2026-05-14
 */

#include "ATCom.h"

#include "atcom_internal.h"
#include "atcom_udp.h"
#include "rtc.h"
#include "storage.h"

/* Seconds until next wake-up as dictated by the HES. 0 means "no pending value".
 * Written by registration and session, read by the main loop. */
static uint32_t pending_wake_seconds = 0;

void Com_Init(void) {
  atcom_udp_init();
  atcom_registration_init();
  atcom_session_init();
}

/* Seconds from now until next_wake minus the cold-start head start, or 0 if
 * next_wake is not a usable appointment: already past (or too close to power
 * up the modem in time) or more than 24 h ahead. The 24 h bound also rejects
 * an erased or corrupt EEPROM word. */
static uint32_t wake_delay_seconds(uint64_t next_wake) {
  uint32_t now_hi, now_lo;
  RTC_get_timestamp(&now_hi, &now_lo);
  uint64_t now = ((uint64_t)now_hi << 32) | now_lo;
  if (next_wake <= now + COLD_START_OFFSET_SEC) return 0;
  uint64_t delay = next_wake - now - COLD_START_OFFSET_SEC;
  return (delay < 86400ULL) ? (uint32_t)delay : 0;
}

void atcom_set_next_wake(uint64_t next_wake) {
  uint32_t delay = wake_delay_seconds(next_wake);
  if (delay == 0) return;
  pending_wake_seconds = delay;
  /* Persist the absolute time: after a reset the device can still honor the
   * appointment instead of opening an unscheduled session. */
  Storage_save_next_wake((uint32_t)next_wake);
}

uint32_t Com_saved_wake_seconds(void) {
  return wake_delay_seconds(Storage_load_next_wake());
}

uint32_t Com_pop_pending_wake_seconds(void) {
  uint32_t v = pending_wake_seconds;
  pending_wake_seconds = 0;
  return v;
}
