/**
 * @file flash_services.c
 * @brief UDS flash programming services — 0x34, 0x35, 0x36, 0x37.
 *
 * Implements Request Download, Request Upload, Transfer Data, and Request
 * Transfer Exit per ISO 14229-1.
 *
 * Design notes:
 * - No dynamic memory; all state lives in the caller-supplied UdsXferSession.
 * - Block sequence counter wraps: 0xFF → 0x01 (never 0x00).
 * - Max block size is fixed at 15 bytes for this simulation.
 */

#include "uds_flash.h"

#include <string.h>

/** Negotiated maximum data bytes per Transfer Data block (excluding header). */
#define FLASH_MAX_BLOCK_DATA 15U

/* ── Transfer session lifecycle ─────────────────────────────────────────── */

void uds_xfer_init(UdsXferSession *xfer) {
  if (!xfer) {
    return;
  }
  memset(xfer, 0, sizeof(*xfer));
}

/* ── Internal: parse addressAndLengthFormatIdentifier ──────────────────── */

/**
 * Parse the addressAndLengthFormatIdentifier byte and the packed
 * address+memorySize data into separate uint32 values.
 *
 * @param fmt          Format byte: high nibble = address bytes (1–4),
 *                     low nibble = mem-size bytes (1–4).
 * @param data         Packed bytes: address first, then memSize, big-endian.
 * @param data_len     Total length of @p data.
 * @param addr_out     Parsed address.
 * @param len_out      Parsed memory size.
 * @return true on success, false if the format is invalid or data too short.
 */
static bool parse_addr_len_fmt(uint8_t fmt, const uint8_t *data,
                               size_t data_len, uint32_t *addr_out,
                               uint32_t *len_out) {
  /* ISO 14229-1: high nibble = memorySize length,
   *              low nibble  = memoryAddress length */
  uint8_t addr_bytes = fmt & 0x0FU;
  uint8_t size_bytes = (fmt >> 4U) & 0x0FU;

  /* Each field must be 1–4 bytes */
  if (addr_bytes < 1U || addr_bytes > 4U || size_bytes < 1U ||
      size_bytes > 4U) {
    return false;
  }

  if (data_len < (size_t)(addr_bytes + size_bytes)) {
    return false;
  }

  /* Decode address big-endian */
  uint32_t address = 0U;
  for (uint8_t i = 0U; i < addr_bytes; i++) {
    address = (address << 8U) | data[i];
  }

  /* Decode memory size big-endian */
  uint32_t mem_size = 0U;
  for (uint8_t i = 0U; i < size_bytes; i++) {
    mem_size = (mem_size << 8U) | data[addr_bytes + i];
  }

  *addr_out = address;
  *len_out = mem_size;
  return true;
}

/* ── Service 0x34: Request Download ────────────────────────────────────── */

int uds_svc_request_download(UdsXferSession *xfer, UdsFlashMemory *flash,
                             uint8_t address_and_len_fmt,
                             const uint8_t *addr_and_len_data, size_t data_len,
                             uint8_t *resp, size_t resp_size, size_t *resp_len,
                             uint8_t *nrc_out) {
  if (!xfer || !flash || !addr_and_len_data || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* A transfer must not already be in progress */
  if (xfer->mode != UDS_XFER_IDLE) {
    *nrc_out = (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT;
    return UDS_CORE_ERR_NRC;
  }

  /* Parse address and memory size */
  uint32_t address = 0U;
  uint32_t mem_size = 0U;
  if (!parse_addr_len_fmt(address_and_len_fmt, addr_and_len_data, data_len,
                          &address, &mem_size)) {
    *nrc_out = (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT;
    return UDS_CORE_ERR_NRC;
  }

  /* Validate address range against flash regions */
  if (!uds_flash_address_valid(flash, address, mem_size)) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  /* Erase the target region before programming */
  if (uds_flash_erase(flash, address, mem_size) != UDS_CORE_OK) {
    *nrc_out = (uint8_t)UDS_NRC_GENERAL_PROGRAMMING_FAILURE;
    return UDS_CORE_ERR_NRC;
  }

  /* Positive response: [0x74, lengthOfMaxBlockSizeParam, maxBlockSize_hi,
   *                     maxBlockSize_lo]
   * lengthOfMaxBlockSizeParam = 0x20 → 2-byte field follows. */
  if (resp_size < 4U) {
    return UDS_CORE_ERR_BUF;
  }

  /* Initialise transfer session */
  xfer->mode = UDS_XFER_DOWNLOAD;
  xfer->address = address;
  xfer->total_length = mem_size;
  xfer->transferred = 0U;
  xfer->block_seq = 0x01U;
  xfer->max_block_size = FLASH_MAX_BLOCK_DATA;

  resp[0] = 0x74U;
  resp[1] = 0x20U; /* lengthOfMaxBlockSizeParam: 2-byte field */
  resp[2] = 0x00U; /* maxBlockSize high byte */
  resp[3] = (uint8_t)FLASH_MAX_BLOCK_DATA;
  *resp_len = 4U;

  return UDS_CORE_OK;
}

/* ── Service 0x35: Request Upload ──────────────────────────────────────── */

int uds_svc_request_upload(UdsXferSession *xfer, const UdsFlashMemory *flash,
                           uint8_t address_and_len_fmt,
                           const uint8_t *addr_and_len_data, size_t data_len,
                           uint8_t *resp, size_t resp_size, size_t *resp_len,
                           uint8_t *nrc_out) {
  if (!xfer || !flash || !addr_and_len_data || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* A transfer must not already be in progress */
  if (xfer->mode != UDS_XFER_IDLE) {
    *nrc_out = (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT;
    return UDS_CORE_ERR_NRC;
  }

  /* Parse address and memory size */
  uint32_t address = 0U;
  uint32_t mem_size = 0U;
  if (!parse_addr_len_fmt(address_and_len_fmt, addr_and_len_data, data_len,
                          &address, &mem_size)) {
    *nrc_out = (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT;
    return UDS_CORE_ERR_NRC;
  }

  /* Validate address range against flash regions */
  if (!uds_flash_address_valid(flash, address, mem_size)) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE;
    return UDS_CORE_ERR_NRC;
  }

  /* Positive response: [0x75, lengthOfMaxBlockSizeParam, hi, lo] */
  if (resp_size < 4U) {
    return UDS_CORE_ERR_BUF;
  }

  /* Initialise transfer session (no erase for upload) */
  xfer->mode = UDS_XFER_UPLOAD;
  xfer->address = address;
  xfer->total_length = mem_size;
  xfer->transferred = 0U;
  xfer->block_seq = 0x01U;
  xfer->max_block_size = FLASH_MAX_BLOCK_DATA;

  resp[0] = 0x75U;
  resp[1] = 0x20U;
  resp[2] = 0x00U;
  resp[3] = (uint8_t)FLASH_MAX_BLOCK_DATA;
  *resp_len = 4U;

  return UDS_CORE_OK;
}

/* ── Service 0x36: Transfer Data ───────────────────────────────────────── */

int uds_svc_transfer_data(UdsXferSession *xfer, UdsFlashMemory *flash,
                          uint8_t block_seq_counter, const uint8_t *data,
                          size_t data_len, uint8_t *resp, size_t resp_size,
                          size_t *resp_len, uint8_t *nrc_out) {
  if (!xfer || !flash || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* Must have an active transfer */
  if (xfer->mode == UDS_XFER_IDLE) {
    *nrc_out = (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT;
    return UDS_CORE_ERR_NRC;
  }

  /* Validate block sequence counter */
  if (block_seq_counter != xfer->block_seq) {
    *nrc_out = (uint8_t)UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER;
    return UDS_CORE_ERR_NRC;
  }

  if (xfer->mode == UDS_XFER_DOWNLOAD) {
    /* Download: write data into flash */
    if (!data || data_len == 0U) {
      return UDS_CORE_ERR_PARAM;
    }

    /* Ensure we do not exceed the declared total length */
    uint32_t remaining = xfer->total_length - xfer->transferred;
    if ((uint32_t)data_len > remaining) {
      *nrc_out = (uint8_t)UDS_NRC_TRANSFER_DATA_SUSPENDED;
      return UDS_CORE_ERR_NRC;
    }

    uint32_t dest = xfer->address + xfer->transferred;
    if (uds_flash_write(flash, dest, data, data_len) != UDS_CORE_OK) {
      *nrc_out = (uint8_t)UDS_NRC_GENERAL_PROGRAMMING_FAILURE;
      return UDS_CORE_ERR_NRC;
    }

    xfer->transferred += (uint32_t)data_len;

    /* Response: [0x76, block_seq_counter] */
    if (resp_size < 2U) {
      return UDS_CORE_ERR_BUF;
    }
    resp[0] = 0x76U;
    resp[1] = block_seq_counter;
    *resp_len = 2U;

  } else {
    /* Upload: read flash data into response */
    uint32_t remaining = xfer->total_length - xfer->transferred;
    uint32_t to_read = (remaining < (uint32_t)xfer->max_block_size)
                           ? remaining
                           : (uint32_t)xfer->max_block_size;

    /* Response: [0x76, block_seq_counter, data...] */
    if (resp_size < (size_t)(2U + to_read)) {
      return UDS_CORE_ERR_BUF;
    }

    uint32_t src = xfer->address + xfer->transferred;
    if (uds_flash_read(flash, src, &resp[2], (size_t)to_read) != UDS_CORE_OK) {
      *nrc_out = (uint8_t)UDS_NRC_GENERAL_PROGRAMMING_FAILURE;
      return UDS_CORE_ERR_NRC;
    }

    xfer->transferred += to_read;

    resp[0] = 0x76U;
    resp[1] = block_seq_counter;
    *resp_len = 2U + (size_t)to_read;
  }

  /* Advance block sequence counter: wrap 0xFF → 0x01, never 0x00 */
  xfer->block_seq = (xfer->block_seq == 0xFFU) ? 0x01U : xfer->block_seq + 1U;

  return UDS_CORE_OK;
}

/* ── Service 0x37: Request Transfer Exit ───────────────────────────────── */

int uds_svc_transfer_exit(UdsXferSession *xfer, uint8_t *resp, size_t resp_size,
                          size_t *resp_len, uint8_t *nrc_out) {
  if (!xfer || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* Must have an active transfer */
  if (xfer->mode == UDS_XFER_IDLE) {
    *nrc_out = (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT;
    return UDS_CORE_ERR_NRC;
  }

  /* Positive response: [0x77] */
  if (resp_size < 1U) {
    return UDS_CORE_ERR_BUF;
  }

  /* Reset session to idle */
  memset(xfer, 0, sizeof(*xfer));

  resp[0] = 0x77U;
  *resp_len = 1U;

  return UDS_CORE_OK;
}