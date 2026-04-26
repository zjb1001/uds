/**
 * @file uds_routine.h
 * @brief UDS Routine Control — registry and Service 0x31.
 *
 * Implements:
 *   - Service 0x31: RoutineControl
 *     Sub-functions: 0x01 startRoutine, 0x02 stopRoutine,
 *                    0x03 requestRoutineResults
 *
 * Design:
 *   - No dynamic memory allocation; registry points to caller-supplied arrays.
 *   - Return codes match UdsCoreError (UDS_CORE_OK=0, UDS_CORE_ERR_PARAM=-1,
 *     UDS_CORE_ERR_NRC=-2, UDS_CORE_ERR_BUF=-3).
 *   - On NRC failure, *nrc_out is set; caller builds the 0x7F negative frame.
 */

#ifndef UDS_ROUTINE_H
#define UDS_ROUTINE_H

#include "uds_core.h"
#include "uds_nrc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Routine state ──────────────────────────────────────────────────────── */

/**
 * @brief Operational state of a routine.
 */
typedef enum {
  UDS_ROUTINE_IDLE = 0,             /**< Not yet started. */
  UDS_ROUTINE_RUNNING = 1,          /**< Currently executing. */
  UDS_ROUTINE_STOPPED = 2,          /**< Explicitly stopped. */
  UDS_ROUTINE_RESULT_AVAILABLE = 3, /**< Finished; results ready. */
} UdsRoutineState;

/* ── Routine registry ───────────────────────────────────────────────────── */

/**
 * @brief Entry describing a single routine.
 */
typedef struct {
  uint16_t routine_id;     /**< 2-byte routine identifier. */
  UdsRoutineState state;   /**< Current execution state. */
  uint8_t result_data[16]; /**< Result bytes (valid when
                                state == RESULT_AVAILABLE). */
  size_t result_len;       /**< Number of valid bytes in result_data. */
  uint8_t min_session;     /**< Minimum session type to execute. */
  bool requires_security;  /**< True if security access required. */
} UdsRoutineEntry;

/**
 * @brief Registry of all supported routines for one ECU.
 *
 * Initialise with uds_routine_registry_init() before use.
 */
typedef struct {
  UdsRoutineEntry *entries; /**< Pointer to array of routine entries. */
  size_t count;             /**< Number of entries in the array. */
} UdsRoutineRegistry;

/**
 * @brief Initialise a routine registry with a caller-supplied entry array.
 *
 * @param[out] reg     Registry to initialise.
 * @param[in]  entries Array of routine entries (may be NULL if count == 0).
 * @param[in]  count   Number of entries.
 */
void uds_routine_registry_init(UdsRoutineRegistry *reg,
                               UdsRoutineEntry *entries, size_t count);

/* ── Service 0x31: RoutineControl ───────────────────────────────────────── */

/**
 * @brief Process a Service 0x31 (RoutineControl) request.
 *
 * Sub-functions:
 *   0x01 — startRoutine:
 *     Checks session/security.  Sets state = RUNNING.
 *     Response: [0x71, 0x01, id_hi, id_lo].
 *
 *   0x02 — stopRoutine:
 *     If state == RUNNING, sets state = STOPPED.
 *     If state is not RUNNING → NRC 0x22 (conditionsNotCorrect).
 *     Response: [0x71, 0x02, id_hi, id_lo].
 *
 *   0x03 — requestRoutineResults:
 *     If state == RESULT_AVAILABLE, returns result_data.
 *     Otherwise → NRC 0x22 (conditionsNotCorrect).
 *     Response: [0x71, 0x03, id_hi, id_lo, result_data...].
 *
 *   Any other sub-function → NRC 0x12 (subFunctionNotSupported).
 *
 * NRC conditions (checked before sub-function logic):
 *   - Routine not found → 0x31 (requestOutOfRange).
 *   - Session too low   → 0x31 (requestOutOfRange).
 *   - Security required but not unlocked → 0x33 (securityAccessDenied).
 *
 * @param[in]  reg               Routine registry (state is mutated).
 * @param[in]  sub_fn            Sub-function (0x01, 0x02, or 0x03).
 * @param[in]  routine_id        Routine to operate on.
 * @param[in]  session_type      Current session type byte.
 * @param[in]  security_unlocked True if security access has been granted.
 * @param[in]  option_data       Optional option record bytes (may be NULL).
 * @param[in]  option_len        Length of @p option_data.
 * @param[out] resp              Buffer for positive response bytes.
 * @param[in]  resp_size         Capacity of @p resp.
 * @param[out] resp_len          Number of bytes written on success.
 * @param[out] nrc_out           NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_routine_control(UdsRoutineRegistry *reg, uint8_t sub_fn,
                            uint16_t routine_id, uint8_t session_type,
                            bool security_unlocked, const uint8_t *option_data,
                            size_t option_len, uint8_t *resp, size_t resp_size,
                            size_t *resp_len, uint8_t *nrc_out);

#ifdef __cplusplus
}
#endif

#endif /* UDS_ROUTINE_H */
