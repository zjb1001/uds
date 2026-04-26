/**
 * @file data.c
 * @brief UDS data services — Service 0x22, 0x2E, 0x11, 0x28.
 *
 * Implements ReadDataByIdentifier, WriteDataByIdentifier, ECUReset, and
 * CommunicationControl per ISO 14229-1.
 *
 * Design notes:
 * - No dynamic memory; all state is in caller-supplied structs.
 * - NULL pointer arguments are rejected with UDS_CORE_ERR_PARAM.
 */

#define _POSIX_C_SOURCE 200809L

#include "uds_data.h"

#include <string.h>

/* ── DID registry ───────────────────────────────────────────────────────── */

void uds_did_registry_init(UdsDidRegistry *reg, UdsDidEntry *entries,
                            size_t count) {
  if (!reg) {
    return;
  }
  reg->entries = entries;
  reg->count = count;
}

/* ── Internal: look up a DID entry ─────────────────────────────────────── */

/**
 * Find a DID entry by ID.  Returns NULL if not found.
 */
static const UdsDidEntry *did_find(const UdsDidRegistry *reg, uint16_t did_id) {
  for (size_t i = 0U; i < reg->count; i++) {
    if (reg->entries[i].did_id == did_id) {
      return &reg->entries[i];
    }
  }
  return NULL;
}

/** Mutable variant of did_find. */
static UdsDidEntry *did_find_mut(UdsDidRegistry *reg, uint16_t did_id) {
  for (size_t i = 0U; i < reg->count; i++) {
    if (reg->entries[i].did_id == did_id) {
      return &reg->entries[i];
    }
  }
  return NULL;
}

/* ── Service 0x22: ReadDataByIdentifier ─────────────────────────────────── */

int uds_svc_read_did(const UdsDidRegistry *reg, const uint16_t *did_list,
                     size_t did_count, uint8_t *resp, size_t resp_size,
                     size_t *resp_len, uint8_t *nrc_out) {
  if (!reg || !did_list || !resp || !resp_len || !nrc_out || did_count == 0U) {
    return UDS_CORE_ERR_PARAM;
  }

  /* Response starts with SID 0x62 */
  if (resp_size < 1U) {
    return UDS_CORE_ERR_BUF;
  }

  resp[0] = 0x62U;
  size_t pos = 1U;

  for (size_t i = 0U; i < did_count; i++) {
    const UdsDidEntry *entry = did_find(reg, did_list[i]);
    if (!entry) {
      *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
      return UDS_CORE_ERR_NRC;
    }

    /* 2-byte DID (big-endian) + data bytes */
    size_t needed = 2U + entry->data_len;
    if (pos + needed > resp_size) {
      return UDS_CORE_ERR_BUF;
    }

    resp[pos++] = (uint8_t)(did_list[i] >> 8);
    resp[pos++] = (uint8_t)(did_list[i] & 0xFFU);
    if (entry->data && entry->data_len > 0U) {
      memcpy(&resp[pos], entry->data, entry->data_len);
    }
    pos += entry->data_len;
  }

  *resp_len = pos;
  return UDS_CORE_OK;
}

/* ── Service 0x2E: WriteDataByIdentifier ────────────────────────────────── */

int uds_svc_write_did(UdsDidRegistry *reg, uint16_t did_id,
                      const uint8_t *data, size_t data_len,
                      uint8_t session_type, bool security_unlocked,
                      uint8_t *resp, size_t resp_size, size_t *resp_len,
                      uint8_t *nrc_out) {
  if (!reg || !data || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  UdsDidEntry *entry = did_find_mut(reg, did_id);

  /* DID not found or not writable */
  if (!entry || !entry->writable) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  /* Session level check */
  if (session_type < entry->min_write_session) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  /* Security access check */
  if (entry->requires_security && !security_unlocked) {
    *nrc_out = (uint8_t)UDS_NRC_SECURITY_ACCESS_DENIED;
    return UDS_CORE_ERR_NRC;
  }

  /* Data length must match the registered buffer size */
  if (data_len != entry->data_len) {
    *nrc_out = (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT;
    return UDS_CORE_ERR_NRC;
  }

  /* Response: [0x6E, DID_HI, DID_LO] */
  if (resp_size < 3U) {
    return UDS_CORE_ERR_BUF;
  }

  /* Write data into the registry buffer */
  if (entry->data && data_len > 0U) {
    memcpy(entry->data, data, data_len);
  }

  resp[0] = 0x6EU;
  resp[1] = (uint8_t)(did_id >> 8);
  resp[2] = (uint8_t)(did_id & 0xFFU);
  *resp_len = 3U;

  return UDS_CORE_OK;
}

/* ── Service 0x11: ECUReset ─────────────────────────────────────────────── */

int uds_svc_ecu_reset(uint8_t reset_type, uint8_t *resp, size_t resp_size,
                      size_t *resp_len, uint8_t *nrc_out) {
  if (!resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* Validate reset type: 0x01=hardReset, 0x02=keyOffOnReset, 0x03=softReset */
  if (reset_type < 0x01U || reset_type > 0x03U) {
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }

  /* Response: [0x51, reset_type] */
  if (resp_size < 2U) {
    return UDS_CORE_ERR_BUF;
  }

  resp[0] = 0x51U;
  resp[1] = reset_type;
  *resp_len = 2U;

  return UDS_CORE_OK;
}

/* ── Service 0x28: CommunicationControl ─────────────────────────────────── */

int uds_svc_comm_control(uint8_t control_type, uint8_t comm_type,
                          uint8_t *resp, size_t resp_size, size_t *resp_len,
                          uint8_t *nrc_out) {
  if (!resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* Validate control type: 0x00–0x03 supported */
  if (control_type > 0x03U) {
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }

  /* comm_type must be non-zero */
  if (comm_type == 0x00U) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  /* Response: [0x68, control_type] */
  if (resp_size < 2U) {
    return UDS_CORE_ERR_BUF;
  }

  resp[0] = 0x68U;
  resp[1] = control_type;
  *resp_len = 2U;

  return UDS_CORE_OK;
}
