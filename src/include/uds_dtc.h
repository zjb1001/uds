/**
 * @file uds_dtc.h
 * @brief UDS DTC services — DTC registry, Service 0x14 and 0x19.
 *
 * Implements:
 *   - Service 0x14: ClearDiagnosticInformation
 *   - Service 0x19: ReadDTCInformation (sub-functions 0x01, 0x02, 0x0A)
 *
 * Design:
 *   - No dynamic memory allocation; registry points to caller-supplied arrays.
 *   - Return codes match UdsCoreError (UDS_CORE_OK=0, UDS_CORE_ERR_PARAM=-1,
 *     UDS_CORE_ERR_NRC=-2, UDS_CORE_ERR_BUF=-3).
 *   - On NRC failure, *nrc_out is set; caller builds the 0x7F negative frame.
 */

#ifndef UDS_DTC_H
#define UDS_DTC_H

#include "uds_core.h"
#include "uds_nrc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── DTC status byte bit definitions ────────────────────────────────────── */

/** Bit 0: test failed in current operation cycle. */
#define UDS_DTC_STATUS_TEST_FAILED              0x01U
/** Bit 1: test failed this operation cycle. */
#define UDS_DTC_STATUS_TEST_FAILED_THIS_CYCLE   0x02U
/** Bit 2: pending DTC. */
#define UDS_DTC_STATUS_PENDING_DTC              0x04U
/** Bit 3: confirmed DTC. */
#define UDS_DTC_STATUS_CONFIRMED_DTC            0x08U
/** Bit 4: test not completed since last clear. */
#define UDS_DTC_STATUS_TEST_NOT_COMPLETED_SINCE_CLEAR 0x10U
/** Bit 5: test failed since last clear. */
#define UDS_DTC_STATUS_TEST_FAILED_SINCE_CLEAR  0x20U
/** Bit 6: test not completed this operation cycle. */
#define UDS_DTC_STATUS_TEST_NOT_COMPLETED_THIS_CYCLE 0x40U
/** Bit 7: warning indicator requested. */
#define UDS_DTC_STATUS_WARNING_INDICATOR        0x80U

/* ── DTC registry ───────────────────────────────────────────────────────── */

/**
 * @brief Entry describing a single Diagnostic Trouble Code (DTC).
 *
 * dtc_code is stored as a 24-bit value in the low three bytes of a uint32_t:
 *   byte1 = (dtc_code >> 16) & 0xFF   (high / group byte)
 *   byte2 = (dtc_code >>  8) & 0xFF
 *   byte3 =  dtc_code        & 0xFF
 */
typedef struct {
  uint32_t dtc_code;            /**< 24-bit DTC code (bits 23..0 used). */
  uint8_t  status;              /**< Current DTC status byte. */
  uint8_t  snapshot_data[16];   /**< Freeze-frame / snapshot bytes. */
  size_t   snapshot_len;        /**< Number of valid bytes in snapshot_data. */
} UdsDtcEntry;

/**
 * @brief Registry of all supported DTCs for one ECU.
 *
 * Initialise with uds_dtc_registry_init() before use.
 */
typedef struct {
  UdsDtcEntry *entries; /**< Pointer to array of DTC entries. */
  size_t       count;   /**< Number of entries in the array. */
} UdsDtcRegistry;

/**
 * @brief Initialise a DTC registry with a caller-supplied entry array.
 *
 * @param[out] reg     Registry to initialise.
 * @param[in]  entries Array of DTC entries (may be NULL if count == 0).
 * @param[in]  count   Number of entries.
 */
void uds_dtc_registry_init(UdsDtcRegistry *reg, UdsDtcEntry *entries,
                            size_t count);

/* ── Service 0x14: ClearDiagnosticInformation ───────────────────────────── */

/**
 * @brief Process a Service 0x14 (ClearDiagnosticInformation) request.
 *
 * Clears the status byte and snapshot data of matching DTC entries:
 *   - group_of_dtc == 0xFFFFFF → clear ALL DTCs.
 *   - Otherwise, clear DTCs whose high byte (bits 23..16) matches the high
 *     byte of group_of_dtc.
 *
 * Returns NRC 0x31 if a non-universal group is requested but no matching
 * DTCs are found.
 *
 * Response on success: [0x54].
 *
 * @param[in]  reg          DTC registry (entries are modified on clear).
 * @param[in]  group_of_dtc 24-bit group identifier (0xFFFFFF = all).
 * @param[out] resp         Buffer for positive response (≥ 1 byte).
 * @param[in]  resp_size    Capacity of @p resp.
 * @param[out] resp_len     Number of bytes written on success.
 * @param[out] nrc_out      NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_clear_dtc(UdsDtcRegistry *reg, uint32_t group_of_dtc,
                       uint8_t *resp, size_t resp_size, size_t *resp_len,
                       uint8_t *nrc_out);

/* ── Service 0x19: ReadDTCInformation ───────────────────────────────────── */

/**
 * @brief Process a Service 0x19 (ReadDTCInformation) request.
 *
 * Supported sub-functions:
 *
 *   0x01 — reportNumberOfDTCByStatusMask
 *     req_data[0] = statusMask
 *     Response: [0x59, 0x01, 0xFF, 0x01, count_hi, count_lo]
 *     count = number of DTCs where (status & statusMask) != 0.
 *
 *   0x02 — reportDTCByStatusMask
 *     req_data[0] = statusMask
 *     Response: [0x59, 0x02, statusMask, DTC1_b1, DTC1_b2, DTC1_b3,
 *                status1, ...]
 *
 *   0x0A — reportSupportedDTC
 *     No request data needed.
 *     Response: [0x59, 0x0A, 0xFF, DTC1_b1, DTC1_b2, DTC1_b3, status1, ...]
 *
 *   Any other sub-function → NRC 0x12 (subFunctionNotSupported).
 *
 * @param[in]  reg          DTC registry.
 * @param[in]  sub_fn       Sub-function byte (0x01, 0x02, or 0x0A).
 * @param[in]  req_data     Optional request data bytes (may be NULL).
 * @param[in]  req_data_len Length of @p req_data.
 * @param[out] resp         Buffer for positive response bytes.
 * @param[in]  resp_size    Capacity of @p resp.
 * @param[out] resp_len     Number of bytes written on success.
 * @param[out] nrc_out      NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_read_dtc(const UdsDtcRegistry *reg, uint8_t sub_fn,
                     const uint8_t *req_data, size_t req_data_len,
                     uint8_t *resp, size_t resp_size, size_t *resp_len,
                     uint8_t *nrc_out);

#ifdef __cplusplus
}
#endif

#endif /* UDS_DTC_H */
