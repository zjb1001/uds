/**
 * @file uds_data.h
 * @brief UDS data services — DID registry, Service 0x22, 0x2E, 0x11, 0x28.
 *
 * Implements:
 *   - Service 0x22: ReadDataByIdentifier (RDBI)
 *   - Service 0x2E: WriteDataByIdentifier (WDBI)
 *   - Service 0x11: ECUReset
 *   - Service 0x28: CommunicationControl
 *
 * Design:
 *   - No dynamic memory allocation; registry points to caller-supplied arrays.
 *   - Return codes match UdsCoreError (UDS_CORE_OK=0, UDS_CORE_ERR_PARAM=-1,
 *     UDS_CORE_ERR_NRC=-2, UDS_CORE_ERR_BUF=-3).
 *   - On NRC failure, *nrc_out is set; caller builds the 0x7F negative frame.
 */

#ifndef UDS_DATA_H
#define UDS_DATA_H

#include "uds_core.h"
#include "uds_nrc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── DID registry ───────────────────────────────────────────────────────── */

/**
 * @brief Entry describing a single Data Identifier (DID).
 *
 * The @p data pointer must remain valid for the lifetime of the registry.
 */
typedef struct {
  uint16_t did_id;            /**< 2-byte DID value (e.g. 0xF190). */
  uint8_t *data;              /**< Pointer to the DID data buffer. */
  size_t   data_len;          /**< Length of @p data in bytes. */
  bool     writable;          /**< True if the DID may be written (0x2E). */
  uint8_t  min_session;       /**< Minimum session type to read (0x01 = any). */
  uint8_t  min_write_session; /**< Minimum session type to write. */
  bool     requires_security; /**< True if a security unlock is required
                                    to write this DID. */
} UdsDidEntry;

/**
 * @brief Registry of all supported DIDs for one ECU.
 *
 * Initialise with uds_did_registry_init() before use.
 */
typedef struct {
  UdsDidEntry *entries; /**< Pointer to array of DID entries. */
  size_t       count;   /**< Number of entries in the array. */
} UdsDidRegistry;

/**
 * @brief Initialise a DID registry with a caller-supplied entry array.
 *
 * @param[out] reg     Registry to initialise.
 * @param[in]  entries Array of DID entries (may be NULL if count == 0).
 * @param[in]  count   Number of entries.
 */
void uds_did_registry_init(UdsDidRegistry *reg, UdsDidEntry *entries,
                            size_t count);

/* ── Service 0x22: ReadDataByIdentifier ─────────────────────────────────── */

/**
 * @brief Process a Service 0x22 (ReadDataByIdentifier) request.
 *
 * For each DID in @p did_list:
 *   - If not found in registry → NRC 0x31 (requestOutOfRange).
 *   - If found, appends [DID_HI, DID_LO, data...] after the 0x62 SID byte.
 *
 * On the first failed DID lookup the function returns immediately with the
 * corresponding NRC.  All DIDs must be valid for a positive response.
 *
 * @param[in]  reg       DID registry.
 * @param[in]  did_list  Array of DID values to read.
 * @param[in]  did_count Number of DIDs in @p did_list.
 * @param[out] resp      Buffer for the positive response bytes (SID 0x62 +
 *                       DID records).
 * @param[in]  resp_size Capacity of @p resp.
 * @param[out] resp_len  Number of bytes written on success.
 * @param[out] nrc_out   NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_read_did(const UdsDidRegistry *reg, const uint16_t *did_list,
                     size_t did_count, uint8_t *resp, size_t resp_size,
                     size_t *resp_len, uint8_t *nrc_out);

/* ── Service 0x2E: WriteDataByIdentifier ────────────────────────────────── */

/**
 * @brief Process a Service 0x2E (WriteDataByIdentifier) request.
 *
 * Validates session level, security access, writability, and data length
 * before copying @p data into the registry entry buffer.
 *
 * Response on success: [0x6E, DID_HI, DID_LO].
 *
 * NRC conditions:
 *   - DID not found or not writable → 0x31 (requestOutOfRange).
 *   - Session too low → 0x31 (requestOutOfRange).
 *   - Security required but not unlocked → 0x33 (securityAccessDenied).
 *   - Wrong data length → 0x13 (incorrectMessageLengthOrFormat).
 *
 * @param[in]  reg               DID registry (entries are modified on write).
 * @param[in]  did_id            DID to write.
 * @param[in]  data              Data bytes to write.
 * @param[in]  data_len          Length of @p data.
 * @param[in]  session_type      Current session type byte.
 * @param[in]  security_unlocked True if security access has been granted.
 * @param[out] resp              Buffer for positive response (≥ 3 bytes).
 * @param[in]  resp_size         Capacity of @p resp.
 * @param[out] resp_len          Number of bytes written on success.
 * @param[out] nrc_out           NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_write_did(UdsDidRegistry *reg, uint16_t did_id,
                      const uint8_t *data, size_t data_len,
                      uint8_t session_type, bool security_unlocked,
                      uint8_t *resp, size_t resp_size, size_t *resp_len,
                      uint8_t *nrc_out);

/* ── Service 0x11: ECUReset ─────────────────────────────────────────────── */

/**
 * @brief Process a Service 0x11 (ECUReset) request.
 *
 * Supported reset types (simulation only — no real reset is performed):
 *   - 0x01: hardReset
 *   - 0x02: keyOffOnReset
 *   - 0x03: softReset
 *
 * Any other reset_type → NRC 0x12 (subFunctionNotSupported).
 *
 * Response on success: [0x51, reset_type].
 *
 * @param[in]  reset_type Reset type byte (0x01–0x03).
 * @param[out] resp       Buffer for positive response (≥ 2 bytes).
 * @param[in]  resp_size  Capacity of @p resp.
 * @param[out] resp_len   Number of bytes written on success.
 * @param[out] nrc_out    NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_ecu_reset(uint8_t reset_type, uint8_t *resp, size_t resp_size,
                      size_t *resp_len, uint8_t *nrc_out);

/* ── Service 0x28: CommunicationControl ─────────────────────────────────── */

/**
 * @brief Process a Service 0x28 (CommunicationControl) request.
 *
 * Supported control types:
 *   - 0x00: enableRxAndTx
 *   - 0x01: enableRxAndDisableTx
 *   - 0x02: disableRxAndEnableTx
 *   - 0x03: disableRxAndTx
 *
 * Any other control_type → NRC 0x12 (subFunctionNotSupported).
 * comm_type must be non-zero → NRC 0x31 (requestOutOfRange) if zero.
 *
 * Response on success: [0x68, control_type].
 *
 * @param[in]  control_type Communication control sub-function (0x00–0x03).
 * @param[in]  comm_type    Communication type byte (must be non-zero).
 * @param[out] resp         Buffer for positive response (≥ 2 bytes).
 * @param[in]  resp_size    Capacity of @p resp.
 * @param[out] resp_len     Number of bytes written on success.
 * @param[out] nrc_out      NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_comm_control(uint8_t control_type, uint8_t comm_type,
                          uint8_t *resp, size_t resp_size, size_t *resp_len,
                          uint8_t *nrc_out);

#ifdef __cplusplus
}
#endif

#endif /* UDS_DATA_H */
