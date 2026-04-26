/**
 * @file dtc.c
 * @brief UDS DTC services — Service 0x14 and 0x19.
 *
 * Implements ClearDiagnosticInformation (0x14) and ReadDTCInformation (0x19)
 * per ISO 14229-1.
 *
 * Design notes:
 * - No dynamic memory; all state is in caller-supplied structs.
 * - NULL pointer arguments are rejected with UDS_CORE_ERR_PARAM.
 */

#define _POSIX_C_SOURCE 200809L

#include "uds_dtc.h"

#include <string.h>

/* ── DTC registry ───────────────────────────────────────────────────────── */

void uds_dtc_registry_init(UdsDtcRegistry *reg, UdsDtcEntry *entries,
                            size_t count) {
  if (!reg) {
    return;
  }
  reg->entries = entries;
  reg->count = count;
}

/* ── Service 0x14: ClearDiagnosticInformation ───────────────────────────── */

int uds_svc_clear_dtc(UdsDtcRegistry *reg, uint32_t group_of_dtc,
                       uint8_t *resp, size_t resp_size, size_t *resp_len,
                       uint8_t *nrc_out) {
  if (!reg || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  if (resp_size < 1U) {
    return UDS_CORE_ERR_BUF;
  }

  bool clear_all = (group_of_dtc == 0xFFFFFFU);
  uint8_t group_byte = (uint8_t)((group_of_dtc >> 16) & 0xFFU);
  bool found = false;

  for (size_t i = 0U; i < reg->count; i++) {
    UdsDtcEntry *entry = &reg->entries[i];
    uint8_t entry_group = (uint8_t)((entry->dtc_code >> 16) & 0xFFU);

    if (clear_all || entry_group == group_byte) {
      entry->status = 0x00U;
      memset(entry->snapshot_data, 0x00U, sizeof(entry->snapshot_data));
      entry->snapshot_len = 0U;
      found = true;
    }
  }

  /* For a specific (non-universal) group, at least one DTC must match. */
  if (!clear_all && !found) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  resp[0] = 0x54U;
  *resp_len = 1U;

  return UDS_CORE_OK;
}

/* ── Service 0x19: ReadDTCInformation ───────────────────────────────────── */

int uds_svc_read_dtc(const UdsDtcRegistry *reg, uint8_t sub_fn,
                     const uint8_t *req_data, size_t req_data_len,
                     uint8_t *resp, size_t resp_size, size_t *resp_len,
                     uint8_t *nrc_out) {
  if (!reg || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  size_t pos = 0U;

  switch (sub_fn) {

  /* ── 0x01: reportNumberOfDTCByStatusMask ─────────────────────────────── */
  case 0x01U: {
    /* Need at least 6 bytes: [0x59, 0x01, mask, fmt, count_hi, count_lo] */
    if (resp_size < 6U) {
      return UDS_CORE_ERR_BUF;
    }

    uint8_t status_mask = (req_data && req_data_len >= 1U) ? req_data[0]
                                                            : 0xFFU;
    uint16_t count = 0U;
    for (size_t i = 0U; i < reg->count; i++) {
      if ((reg->entries[i].status & status_mask) != 0U) {
        count++;
      }
    }

    resp[pos++] = 0x59U;
    resp[pos++] = 0x01U;
    resp[pos++] = 0xFFU; /* dtcStatusAvailabilityMask */
    resp[pos++] = 0x01U; /* dtcFormatIdentifier (ISO 14229-1) */
    resp[pos++] = (uint8_t)(count >> 8);
    resp[pos++] = (uint8_t)(count & 0xFFU);
    *resp_len = pos;
    return UDS_CORE_OK;
  }

  /* ── 0x02: reportDTCByStatusMask ────────────────────────────────────── */
  case 0x02U: {
    /* Minimum: [0x59, 0x02, statusMask] = 3 bytes */
    if (resp_size < 3U) {
      return UDS_CORE_ERR_BUF;
    }

    uint8_t status_mask = (req_data && req_data_len >= 1U) ? req_data[0]
                                                            : 0xFFU;
    resp[pos++] = 0x59U;
    resp[pos++] = 0x02U;
    resp[pos++] = status_mask;

    for (size_t i = 0U; i < reg->count; i++) {
      const UdsDtcEntry *e = &reg->entries[i];
      if ((e->status & status_mask) == 0U) {
        continue;
      }
      /* 4 bytes per DTC record: 3 DTC bytes + status */
      if (pos + 4U > resp_size) {
        return UDS_CORE_ERR_BUF;
      }
      resp[pos++] = (uint8_t)((e->dtc_code >> 16) & 0xFFU);
      resp[pos++] = (uint8_t)((e->dtc_code >>  8) & 0xFFU);
      resp[pos++] = (uint8_t)( e->dtc_code        & 0xFFU);
      resp[pos++] = e->status;
    }

    *resp_len = pos;
    return UDS_CORE_OK;
  }

  /* ── 0x0A: reportSupportedDTC ───────────────────────────────────────── */
  case 0x0AU: {
    /* Minimum: [0x59, 0x0A, 0xFF] = 3 bytes */
    if (resp_size < 3U) {
      return UDS_CORE_ERR_BUF;
    }

    resp[pos++] = 0x59U;
    resp[pos++] = 0x0AU;
    resp[pos++] = 0xFFU; /* statusAvailabilityMask */

    for (size_t i = 0U; i < reg->count; i++) {
      const UdsDtcEntry *e = &reg->entries[i];
      /* 4 bytes per DTC record */
      if (pos + 4U > resp_size) {
        return UDS_CORE_ERR_BUF;
      }
      resp[pos++] = (uint8_t)((e->dtc_code >> 16) & 0xFFU);
      resp[pos++] = (uint8_t)((e->dtc_code >>  8) & 0xFFU);
      resp[pos++] = (uint8_t)( e->dtc_code        & 0xFFU);
      resp[pos++] = e->status;
    }

    *resp_len = pos;
    return UDS_CORE_OK;
  }

  default:
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }
}
