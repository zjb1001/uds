/**
 * @file routine_control.c
 * @brief UDS Routine Control — Service 0x31.
 *
 * Implements startRoutine, stopRoutine, and requestRoutineResults per
 * ISO 14229-1.
 *
 * Design notes:
 * - No dynamic memory; all state is in caller-supplied structs.
 * - NULL pointer arguments are rejected with UDS_CORE_ERR_PARAM.
 */

#define _POSIX_C_SOURCE 200809L

#include "uds_routine.h"

#include <string.h>

/* ── Routine registry ───────────────────────────────────────────────────── */

void uds_routine_registry_init(UdsRoutineRegistry *reg,
                                UdsRoutineEntry *entries, size_t count) {
  if (!reg) {
    return;
  }
  reg->entries = entries;
  reg->count = count;
}

/* ── Internal: look up a routine entry ─────────────────────────────────── */

static UdsRoutineEntry *routine_find(UdsRoutineRegistry *reg,
                                     uint16_t routine_id) {
  for (size_t i = 0U; i < reg->count; i++) {
    if (reg->entries[i].routine_id == routine_id) {
      return &reg->entries[i];
    }
  }
  return NULL;
}

/* ── Service 0x31: RoutineControl ───────────────────────────────────────── */

int uds_svc_routine_control(UdsRoutineRegistry *reg, uint8_t sub_fn,
                             uint16_t routine_id, uint8_t session_type,
                             bool security_unlocked,
                             const uint8_t *option_data, size_t option_len,
                             uint8_t *resp, size_t resp_size, size_t *resp_len,
                             uint8_t *nrc_out) {
  (void)option_data;
  (void)option_len;

  if (!reg || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* Validate sub-function before looking up the routine */
  if (sub_fn < 0x01U || sub_fn > 0x03U) {
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }

  UdsRoutineEntry *entry = routine_find(reg, routine_id);
  if (!entry) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  /* Session level check */
  if (session_type < entry->min_session) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  /* Security access check */
  if (entry->requires_security && !security_unlocked) {
    *nrc_out = (uint8_t)UDS_NRC_SECURITY_ACCESS_DENIED;
    return UDS_CORE_ERR_NRC;
  }

  switch (sub_fn) {

  /* ── 0x01: startRoutine ─────────────────────────────────────────────── */
  case 0x01U: {
    /* Response: [0x71, 0x01, id_hi, id_lo] */
    if (resp_size < 4U) {
      return UDS_CORE_ERR_BUF;
    }
    entry->state = UDS_ROUTINE_RUNNING;
    resp[0] = 0x71U;
    resp[1] = 0x01U;
    resp[2] = (uint8_t)(routine_id >> 8);
    resp[3] = (uint8_t)(routine_id & 0xFFU);
    *resp_len = 4U;
    return UDS_CORE_OK;
  }

  /* ── 0x02: stopRoutine ──────────────────────────────────────────────── */
  case 0x02U: {
    if (entry->state != UDS_ROUTINE_RUNNING) {
      *nrc_out = (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT;
      return UDS_CORE_ERR_NRC;
    }
    /* Response: [0x71, 0x02, id_hi, id_lo] */
    if (resp_size < 4U) {
      return UDS_CORE_ERR_BUF;
    }
    entry->state = UDS_ROUTINE_STOPPED;
    resp[0] = 0x71U;
    resp[1] = 0x02U;
    resp[2] = (uint8_t)(routine_id >> 8);
    resp[3] = (uint8_t)(routine_id & 0xFFU);
    *resp_len = 4U;
    return UDS_CORE_OK;
  }

  /* ── 0x03: requestRoutineResults ────────────────────────────────────── */
  case 0x03U: {
    if (entry->state != UDS_ROUTINE_RESULT_AVAILABLE) {
      *nrc_out = (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT;
      return UDS_CORE_ERR_NRC;
    }
    /* Response: [0x71, 0x03, id_hi, id_lo, result_data...] */
    size_t needed = 4U + entry->result_len;
    if (resp_size < needed) {
      return UDS_CORE_ERR_BUF;
    }
    resp[0] = 0x71U;
    resp[1] = 0x03U;
    resp[2] = (uint8_t)(routine_id >> 8);
    resp[3] = (uint8_t)(routine_id & 0xFFU);
    if (entry->result_len > 0U) {
      memcpy(&resp[4], entry->result_data, entry->result_len);
    }
    *resp_len = needed;
    return UDS_CORE_OK;
  }

  default:
    /* Should not reach here; handled above. */
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }
}
