/**
 * @file iso_tp.c
 * @brief ISO-TP (ISO 15765-2) transport layer implementation.
 *
 * Implements frame-level encode/decode and high-level PDU send/receive
 * for standard 8-byte CAN frames (max PDU size 4095 bytes).
 *
 * Design notes:
 * - All encode/decode helpers are pure: no I/O, no dynamic allocation.
 * - uds_tp_send() and uds_tp_recv() perform CAN I/O via uds_can_send/recv().
 * - Callers must set CAN socket receive filters (via uds_can_set_filter())
 *   before calling uds_tp_send() or uds_tp_recv().
 * - STmin delay (usleep) is applied between Consecutive Frames on the
 *   sender side; 0 = no delay.
 */

/* Request POSIX.1-2008 interfaces (nanosleep) */
#define _POSIX_C_SOURCE 200809L

#include "uds_tp.h"

#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

/**
 * Sleep for the duration encoded by STmin per ISO 15765-2 §6.5.5.5.
 *
 *  0x00        → no delay
 *  0x01..0x7F  → 1..127 ms
 *  0xF1..0xF9  → 100..900 µs
 *  all other   → reserved, treated as no delay
 */
static void apply_stmin(uint8_t stmin) {
  if (stmin == 0U) {
    return;
  }
  long ns = 0L;
  if (stmin <= 0x7FU) {
    ns = (long)stmin * 1000000L; /* ms → ns */
  } else if (stmin >= 0xF1U && stmin <= 0xF9U) {
    ns = (long)(stmin - 0xF0U) * 100000L; /* 100 µs units → ns */
  } else {
    return; /* reserved values → no delay */
  }
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = ns;
  nanosleep(&ts, NULL);
}

/* ── Frame classification ───────────────────────────────────────────────── */

UdsTpFrameType uds_tp_frame_type(const struct can_frame *frame) {
  if (!frame || frame->can_dlc == 0U) {
    return UDS_TP_FRAME_UNKNOWN;
  }
  uint8_t nibble = (frame->data[0] >> 4) & 0x0FU;
  if (nibble > 3U) {
    return UDS_TP_FRAME_UNKNOWN;
  }
  return (UdsTpFrameType)nibble;
}

/* ── Frame encode ───────────────────────────────────────────────────────── */

int uds_tp_encode_sf(struct can_frame *out, uint32_t can_id,
                     const uint8_t *data, size_t len) {
  if (!out || !data || len == 0U || len > UDS_TP_SF_MAX_DATA) {
    return UDS_TP_ERR_PARAM;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;
  out->can_dlc = (uint8_t)(1U + len);
  out->data[0] = (uint8_t)len; /* PCI: 0x0N, N = data length */
  memcpy(&out->data[1], data, len);

  return UDS_TP_OK;
}

int uds_tp_encode_ff(struct can_frame *out, uint32_t can_id,
                     const uint8_t *data, uint32_t total_len) {
  if (!out || !data) {
    return UDS_TP_ERR_PARAM;
  }
  if (total_len < 8U || total_len > UDS_TP_MAX_PDU_LEN) {
    return UDS_TP_ERR_PDU_LEN;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;
  out->can_dlc = 8U;
  out->data[0] = (uint8_t)(0x10U | ((total_len >> 8) & 0x0FU));
  out->data[1] = (uint8_t)(total_len & 0xFFU);
  memcpy(&out->data[2], data, UDS_TP_FF_DATA_BYTES);

  return UDS_TP_OK;
}

int uds_tp_encode_cf(struct can_frame *out, uint32_t can_id,
                     const uint8_t *data, size_t len, uint8_t sn) {
  if (!out || !data || len == 0U || len > UDS_TP_CF_DATA_BYTES) {
    return UDS_TP_ERR_PARAM;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;
  out->can_dlc = 8U;
  out->data[0] = (uint8_t)(0x20U | (sn & 0x0FU));
  memcpy(&out->data[1], data, len);
  /* Remaining bytes stay 0x00 (zero-padding per standard) */

  return UDS_TP_OK;
}

int uds_tp_encode_fc(struct can_frame *out, uint32_t can_id, UdsTpFlowStatus fs,
                     uint8_t bs, uint8_t stmin) {
  if (!out) {
    return UDS_TP_ERR_PARAM;
  }
  if ((unsigned int)fs > (unsigned int)UDS_TP_FC_OVERFLOW) {
    return UDS_TP_ERR_PARAM;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;
  out->can_dlc = 3U;
  out->data[0] = (uint8_t)(0x30U | (uint8_t)fs);
  out->data[1] = bs;
  out->data[2] = stmin;

  return UDS_TP_OK;
}

/* ── Frame decode ───────────────────────────────────────────────────────── */

int uds_tp_decode_sf(const struct can_frame *frame, uint8_t *buf,
                     size_t buf_size, size_t *out_len) {
  if (!frame || !buf || !out_len || buf_size == 0U) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type(frame) != UDS_TP_FRAME_SF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }

  size_t len = (size_t)(frame->data[0] & 0x0FU);
  if (len == 0U || len > UDS_TP_SF_MAX_DATA) {
    return UDS_TP_ERR_PARAM;
  }
  if ((size_t)frame->can_dlc < 1U + len) {
    return UDS_TP_ERR_PARAM;
  }
  if (len > buf_size) {
    return UDS_TP_ERR_BUF_FULL;
  }

  memcpy(buf, &frame->data[1], len);
  *out_len = len;
  return UDS_TP_OK;
}

int uds_tp_decode_ff(const struct can_frame *frame, uint8_t *buf,
                     size_t buf_size, uint32_t *total_len) {
  if (!frame || !buf || !total_len || buf_size < UDS_TP_FF_DATA_BYTES) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type(frame) != UDS_TP_FRAME_FF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }
  if (frame->can_dlc < 8U) {
    return UDS_TP_ERR_PARAM;
  }

  uint32_t len =
      ((uint32_t)(frame->data[0] & 0x0FU) << 8U) | (uint32_t)frame->data[1];
  if (len < 8U || len > UDS_TP_MAX_PDU_LEN) {
    return UDS_TP_ERR_PDU_LEN;
  }

  *total_len = len;
  memcpy(buf, &frame->data[2], UDS_TP_FF_DATA_BYTES);
  return UDS_TP_OK;
}

int uds_tp_decode_cf(const struct can_frame *frame, uint8_t *sn_out,
                     uint8_t *buf, size_t buf_size, size_t max_data) {
  if (!frame || !sn_out || !buf || buf_size == 0U) {
    return UDS_TP_ERR_PARAM;
  }
  if (max_data == 0U || max_data > UDS_TP_CF_DATA_BYTES) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type(frame) != UDS_TP_FRAME_CF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }
  if (frame->can_dlc < 2U) {
    return UDS_TP_ERR_PARAM;
  }
  if (max_data > buf_size) {
    return UDS_TP_ERR_BUF_FULL;
  }

  *sn_out = frame->data[0] & 0x0FU;
  memcpy(buf, &frame->data[1], max_data);
  return UDS_TP_OK;
}

int uds_tp_decode_fc(const struct can_frame *frame, UdsTpFlowStatus *fs,
                     uint8_t *bs, uint8_t *stmin) {
  if (!frame || !fs || !bs || !stmin) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type(frame) != UDS_TP_FRAME_FC) {
    return UDS_TP_ERR_FRAME_TYPE;
  }
  if (frame->can_dlc < 3U) {
    return UDS_TP_ERR_PARAM;
  }

  uint8_t fs_raw = frame->data[0] & 0x0FU;
  if (fs_raw > (uint8_t)UDS_TP_FC_OVERFLOW) {
    return UDS_TP_ERR_PARAM; /* reserved / invalid flow status */
  }

  *fs = (UdsTpFlowStatus)fs_raw;
  *bs = frame->data[1];
  *stmin = frame->data[2];
  return UDS_TP_OK;
}

/* ── Receiver channel ───────────────────────────────────────────────────── */

int uds_tp_rx_init(UdsTpRxChannel *ch, uint8_t *buf, size_t buf_size) {
  if (!ch || !buf || buf_size == 0U) {
    return UDS_TP_ERR_PARAM;
  }

  ch->buf = buf;
  ch->buf_size = buf_size;
  ch->msg_len = 0U;
  ch->received = 0U;
  ch->next_sn = 1U;
  ch->in_progress = false;
  return UDS_TP_OK;
}

/* ── High-level send ────────────────────────────────────────────────────── */

int uds_tp_send(UdsCanSocket *sock, uint32_t tx_id, const uint8_t *data,
                size_t len, const UdsTpConfig *cfg) {
  if (!sock || !data || len == 0U || len > UDS_TP_MAX_PDU_LEN || !cfg) {
    return UDS_TP_ERR_PARAM;
  }
  if (cfg->timeout_ms == 0U) {
    return UDS_TP_ERR_PARAM;
  }

  struct can_frame frame;

  /* ── Single Frame path ──────────────────────────────────────────────── */
  if (len <= UDS_TP_SF_MAX_DATA) {
    int rc = uds_tp_encode_sf(&frame, tx_id, data, len);
    if (rc != UDS_TP_OK) {
      return rc;
    }
    if (uds_can_send(sock, &frame) != UDS_CAN_OK) {
      return UDS_TP_ERR_SEND;
    }
    return UDS_TP_OK;
  }

  /* ── Multi-frame path ───────────────────────────────────────────────── */
  int rc = uds_tp_encode_ff(&frame, tx_id, data, (uint32_t)len);
  if (rc != UDS_TP_OK) {
    return rc;
  }
  if (uds_can_send(sock, &frame) != UDS_CAN_OK) {
    return UDS_TP_ERR_SEND;
  }

  size_t offset = UDS_TP_FF_DATA_BYTES;
  uint8_t sn = 1U;

  while (offset < len) {
    /* Wait for a Flow Control frame from the receiver */
    struct can_frame fc;
    int recv_rc = uds_can_recv(sock, &fc, cfg->timeout_ms);
    if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
      return UDS_TP_ERR_TIMEOUT;
    }
    if (recv_rc != UDS_CAN_OK) {
      return UDS_TP_ERR_RECV;
    }

    UdsTpFlowStatus fs;
    uint8_t bs, stmin;
    rc = uds_tp_decode_fc(&fc, &fs, &bs, &stmin);
    if (rc != UDS_TP_OK) {
      return rc;
    }

    if (fs == UDS_TP_FC_OVERFLOW) {
      return UDS_TP_ERR_OVERFLOW;
    }

    /* Handle WAIT frames — up to UDS_TP_MAX_WAIT_FRAMES */
    unsigned int wait_count = 0U;
    while (fs == UDS_TP_FC_WAIT) {
      if (++wait_count > UDS_TP_MAX_WAIT_FRAMES) {
        return UDS_TP_ERR_WAIT_LIMIT;
      }
      recv_rc = uds_can_recv(sock, &fc, cfg->timeout_ms);
      if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
        return UDS_TP_ERR_TIMEOUT;
      }
      if (recv_rc != UDS_CAN_OK) {
        return UDS_TP_ERR_RECV;
      }
      rc = uds_tp_decode_fc(&fc, &fs, &bs, &stmin);
      if (rc != UDS_TP_OK) {
        return rc;
      }
      if (fs == UDS_TP_FC_OVERFLOW) {
        return UDS_TP_ERR_OVERFLOW;
      }
    }

    /* CTS received: send a block of Consecutive Frames */
    uint8_t bs_sent = 0U;
    while (offset < len) {
      apply_stmin(stmin);

      size_t remaining = len - offset;
      size_t chunk =
          (remaining > UDS_TP_CF_DATA_BYTES) ? UDS_TP_CF_DATA_BYTES : remaining;

      rc = uds_tp_encode_cf(&frame, tx_id, &data[offset], chunk, sn);
      if (rc != UDS_TP_OK) {
        return rc;
      }
      if (uds_can_send(sock, &frame) != UDS_CAN_OK) {
        return UDS_TP_ERR_SEND;
      }

      sn = (uint8_t)((sn + 1U) & 0x0FU);
      offset += chunk;
      bs_sent++;

      /* If block size is non-zero, pause after bs_sent == bs */
      if (bs > 0U && bs_sent >= bs) {
        break; /* Go back to outer loop to wait for next FC */
      }
    }
  }

  return UDS_TP_OK;
}

/* ── High-level receive ─────────────────────────────────────────────────── */

int uds_tp_recv(UdsCanSocket *sock, uint32_t tx_id, uint8_t *buf,
                size_t buf_size, size_t *out_len, const UdsTpConfig *cfg) {
  if (!sock || !buf || !out_len || buf_size == 0U || !cfg) {
    return UDS_TP_ERR_PARAM;
  }
  if (cfg->timeout_ms == 0U) {
    return UDS_TP_ERR_PARAM;
  }

  struct can_frame frame;

  /* Wait for first incoming frame (SF or FF) */
  int recv_rc = uds_can_recv(sock, &frame, cfg->timeout_ms);
  if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
    return UDS_TP_ERR_TIMEOUT;
  }
  if (recv_rc != UDS_CAN_OK) {
    return UDS_TP_ERR_RECV;
  }

  UdsTpFrameType ft = uds_tp_frame_type(&frame);

  /* ── Single Frame path ──────────────────────────────────────────────── */
  if (ft == UDS_TP_FRAME_SF) {
    return uds_tp_decode_sf(&frame, buf, buf_size, out_len);
  }

  /* ── Multi-frame path ───────────────────────────────────────────────── */
  if (ft != UDS_TP_FRAME_FF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }

  uint32_t total_len = 0U;
  int rc = uds_tp_decode_ff(&frame, buf, buf_size, &total_len);
  if (rc != UDS_TP_OK) {
    return rc;
  }
  if (total_len > (uint32_t)buf_size) {
    return UDS_TP_ERR_BUF_FULL;
  }

  /* Send initial Flow Control (CTS) */
  struct can_frame fc;
  rc =
      uds_tp_encode_fc(&fc, tx_id, UDS_TP_FC_CTS, cfg->block_size, cfg->st_min);
  if (rc != UDS_TP_OK) {
    return rc;
  }
  if (uds_can_send(sock, &fc) != UDS_CAN_OK) {
    return UDS_TP_ERR_SEND;
  }

  uint32_t received = (uint32_t)UDS_TP_FF_DATA_BYTES;
  uint8_t expected_sn = 1U;
  uint8_t bs_count = 0U;

  while (received < total_len) {
    /* Re-send FC if block size is exhausted */
    if (cfg->block_size > 0U && bs_count >= cfg->block_size) {
      rc = uds_tp_encode_fc(&fc, tx_id, UDS_TP_FC_CTS, cfg->block_size,
                            cfg->st_min);
      if (rc != UDS_TP_OK) {
        return rc;
      }
      if (uds_can_send(sock, &fc) != UDS_CAN_OK) {
        return UDS_TP_ERR_SEND;
      }
      bs_count = 0U;
    }

    /* Receive next Consecutive Frame */
    recv_rc = uds_can_recv(sock, &frame, cfg->timeout_ms);
    if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
      return UDS_TP_ERR_TIMEOUT;
    }
    if (recv_rc != UDS_CAN_OK) {
      return UDS_TP_ERR_RECV;
    }

    if (uds_tp_frame_type(&frame) != UDS_TP_FRAME_CF) {
      return UDS_TP_ERR_FRAME_TYPE;
    }

    uint32_t remaining = total_len - received;
    size_t chunk = (remaining > UDS_TP_CF_DATA_BYTES) ? UDS_TP_CF_DATA_BYTES
                                                      : (size_t)remaining;

    uint8_t sn = 0U;
    rc = uds_tp_decode_cf(&frame, &sn, buf + received, buf_size - received,
                          chunk);
    if (rc != UDS_TP_OK) {
      return rc;
    }

    if (sn != expected_sn) {
      return UDS_TP_ERR_SEQUENCE;
    }

    received += (uint32_t)chunk;
    expected_sn = (uint8_t)((expected_sn + 1U) & 0x0FU);
    bs_count++;
  }

  *out_len = (size_t)total_len;
  return UDS_TP_OK;
}

/* ── Error string ───────────────────────────────────────────────────────── */

const char *uds_tp_strerror(int err) {
  switch ((UdsTpError)err) {
  case UDS_TP_OK:
    return "Success";
  case UDS_TP_ERR_PARAM:
    return "Invalid parameter";
  case UDS_TP_ERR_TIMEOUT:
    return "Receive timed out";
  case UDS_TP_ERR_OVERFLOW:
    return "Overflow (FC overflow or buffer too small)";
  case UDS_TP_ERR_SEQUENCE:
    return "Consecutive frame sequence number error";
  case UDS_TP_ERR_FRAME_TYPE:
    return "Unexpected or invalid frame type";
  case UDS_TP_ERR_BUF_FULL:
    return "Caller buffer too small for PDU";
  case UDS_TP_ERR_SEND:
    return "CAN send failure";
  case UDS_TP_ERR_INPROGRESS:
    return "Transfer already in progress";
  case UDS_TP_ERR_RECV:
    return "CAN receive failure";
  case UDS_TP_ERR_PDU_LEN:
    return "PDU length out of range";
  case UDS_TP_ERR_WAIT_LIMIT:
    return "Too many FC WAIT frames received";
  default:
    return "Unknown error";
  }
}

/* ── CAN FD extensions ──────────────────────────────────────────────────── */

#ifdef CAN_FD_ENABLED

/* ── CAN FD frame type classification ──────────────────────────────────── */

UdsTpFrameType uds_tp_frame_type_fd(const struct canfd_frame *frame) {
  if (!frame || frame->len == 0U) {
    return UDS_TP_FRAME_UNKNOWN;
  }
  uint8_t nibble = (frame->data[0] >> 4) & 0x0FU;
  if (nibble > 3U) {
    return UDS_TP_FRAME_UNKNOWN;
  }
  return (UdsTpFrameType)nibble;
}

/* ── CAN FD frame encode ────────────────────────────────────────────────── */

int uds_tp_encode_sf_fd(struct canfd_frame *out, uint32_t can_id,
                        const uint8_t *data, size_t len) {
  if (!out || !data || len == 0U || len > UDS_TP_CANFD_SF_MAX_DATA) {
    return UDS_TP_ERR_PARAM;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;

  if (len <= UDS_TP_SF_MAX_DATA) {
    /* Classic one-byte PCI: compatible with standard CAN SF */
    out->len = (uint8_t)(1U + len);
    out->data[0] = (uint8_t)len;
    memcpy(&out->data[1], data, len);
  } else {
    /* CAN FD escape sequence: data[0] = 0x00, data[1] = SF_DL (≥ 8) */
    out->len = (uint8_t)(2U + len);
    out->data[0] = 0x00U;
    out->data[1] = (uint8_t)len;
    memcpy(&out->data[2], data, len);
  }

  return UDS_TP_OK;
}

int uds_tp_encode_ff_fd(struct canfd_frame *out, uint32_t can_id,
                        const uint8_t *data, uint32_t total_len) {
  if (!out || !data) {
    return UDS_TP_ERR_PARAM;
  }
  if (total_len == 0U) {
    return UDS_TP_ERR_PDU_LEN;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;

  if (total_len <= UDS_TP_CANFD_FF_EXT_THRESHOLD) {
    /* Regular FF: 2-byte PCI, embed up to 62 data bytes */
    if (total_len <= UDS_TP_CANFD_SF_MAX_DATA) {
      /* Must use SF for payloads that fit in a single frame */
      return UDS_TP_ERR_PDU_LEN;
    }
    size_t embed = (total_len > UDS_TP_CANFD_FF_DATA_BYTES)
                       ? UDS_TP_CANFD_FF_DATA_BYTES
                       : (size_t)total_len;
    out->data[0] = (uint8_t)(0x10U | ((total_len >> 8) & 0x0FU));
    out->data[1] = (uint8_t)(total_len & 0xFFU);
    memcpy(&out->data[2], data, embed);
    out->len = (uint8_t)(2U + embed);
  } else {
    /* Extended FF: 6-byte PCI (FF_DL[0:1] = 0x10 0x00, then 32-bit length) */
    size_t embed = (total_len > (uint32_t)UDS_TP_CANFD_FF_EXT_DATA_BYTES)
                       ? UDS_TP_CANFD_FF_EXT_DATA_BYTES
                       : (size_t)total_len;
    out->data[0] = 0x10U;
    out->data[1] = 0x00U;
    out->data[2] = (uint8_t)((total_len >> 24) & 0xFFU);
    out->data[3] = (uint8_t)((total_len >> 16) & 0xFFU);
    out->data[4] = (uint8_t)((total_len >> 8) & 0xFFU);
    out->data[5] = (uint8_t)(total_len & 0xFFU);
    memcpy(&out->data[6], data, embed);
    out->len = (uint8_t)(6U + embed);
  }

  return UDS_TP_OK;
}

int uds_tp_encode_cf_fd(struct canfd_frame *out, uint32_t can_id,
                        const uint8_t *data, size_t len, uint8_t sn) {
  if (!out || !data || len == 0U || len > UDS_TP_CANFD_CF_DATA_BYTES) {
    return UDS_TP_ERR_PARAM;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;
  out->len = (uint8_t)(1U + len);
  out->data[0] = (uint8_t)(0x20U | (sn & 0x0FU));
  memcpy(&out->data[1], data, len);

  return UDS_TP_OK;
}

int uds_tp_encode_fc_fd(struct canfd_frame *out, uint32_t can_id,
                        UdsTpFlowStatus fs, uint8_t bs, uint8_t stmin) {
  if (!out) {
    return UDS_TP_ERR_PARAM;
  }
  if ((unsigned int)fs > (unsigned int)UDS_TP_FC_OVERFLOW) {
    return UDS_TP_ERR_PARAM;
  }

  memset(out, 0, sizeof(*out));
  out->can_id = can_id;
  out->len = 3U;
  out->data[0] = (uint8_t)(0x30U | (uint8_t)fs);
  out->data[1] = bs;
  out->data[2] = stmin;

  return UDS_TP_OK;
}

/* ── CAN FD frame decode ────────────────────────────────────────────────── */

int uds_tp_decode_sf_fd(const struct canfd_frame *frame, uint8_t *buf,
                        size_t buf_size, size_t *out_len) {
  if (!frame || !buf || !out_len || buf_size == 0U) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type_fd(frame) != UDS_TP_FRAME_SF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }

  size_t len;
  size_t data_offset;

  if ((frame->data[0] & 0x0FU) == 0U) {
    /* CAN FD escape sequence: data[0] == 0x00, data[1] == SF_DL */
    if (frame->len < 2U) {
      return UDS_TP_ERR_PARAM;
    }
    len = (size_t)frame->data[1];
    data_offset = 2U;
    if (len < UDS_TP_CANFD_SF_ESCAPE_MIN_DL || len > UDS_TP_CANFD_SF_MAX_DATA) {
      return UDS_TP_ERR_PARAM;
    }
  } else {
    /* Classic one-byte PCI */
    len = (size_t)(frame->data[0] & 0x0FU);
    data_offset = 1U;
    if (len > UDS_TP_SF_MAX_DATA) {
      return UDS_TP_ERR_PARAM;
    }
  }

  if ((size_t)frame->len < data_offset + len) {
    return UDS_TP_ERR_PARAM;
  }
  if (len > buf_size) {
    return UDS_TP_ERR_BUF_FULL;
  }

  memcpy(buf, &frame->data[data_offset], len);
  *out_len = len;
  return UDS_TP_OK;
}

int uds_tp_decode_ff_fd(const struct canfd_frame *frame, uint8_t *buf,
                        size_t buf_size, uint32_t *total_len,
                        size_t *ff_data_bytes) {
  if (!frame || !buf || !total_len || !ff_data_bytes) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type_fd(frame) != UDS_TP_FRAME_FF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }

  uint32_t len;
  size_t data_offset;
  size_t embed;

  if (frame->data[1] == 0x00U && (frame->data[0] & 0x0FU) == 0U) {
    /* Extended FF: 32-bit FF_DL in bytes [2..5] */
    if (frame->len < 6U) {
      return UDS_TP_ERR_PARAM;
    }
    len = ((uint32_t)frame->data[2] << 24U) |
          ((uint32_t)frame->data[3] << 16U) | ((uint32_t)frame->data[4] << 8U) |
          (uint32_t)frame->data[5];
    if (len == 0U) {
      return UDS_TP_ERR_PDU_LEN;
    }
    data_offset = 6U;
    embed = ((size_t)frame->len > data_offset)
                ? ((size_t)frame->len - data_offset)
                : 0U;
    if (embed > UDS_TP_CANFD_FF_EXT_DATA_BYTES) {
      embed = UDS_TP_CANFD_FF_EXT_DATA_BYTES;
    }
  } else {
    /* Regular FF: 12-bit FF_DL in nibble+byte */
    len = ((uint32_t)(frame->data[0] & 0x0FU) << 8U) | (uint32_t)frame->data[1];
    if (len <= UDS_TP_CANFD_SF_MAX_DATA ||
        len > UDS_TP_CANFD_FF_EXT_THRESHOLD) {
      return UDS_TP_ERR_PDU_LEN;
    }
    data_offset = 2U;
    embed = ((size_t)frame->len > data_offset)
                ? ((size_t)frame->len - data_offset)
                : 0U;
    if (embed > UDS_TP_CANFD_FF_DATA_BYTES) {
      embed = UDS_TP_CANFD_FF_DATA_BYTES;
    }
  }

  if (embed > buf_size) {
    return UDS_TP_ERR_BUF_FULL;
  }

  *total_len = len;
  *ff_data_bytes = embed;
  memcpy(buf, &frame->data[data_offset], embed);
  return UDS_TP_OK;
}

int uds_tp_decode_cf_fd(const struct canfd_frame *frame, uint8_t *sn_out,
                        uint8_t *buf, size_t buf_size, size_t max_data) {
  if (!frame || !sn_out || !buf || buf_size == 0U) {
    return UDS_TP_ERR_PARAM;
  }
  if (max_data == 0U || max_data > UDS_TP_CANFD_CF_DATA_BYTES) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type_fd(frame) != UDS_TP_FRAME_CF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }
  if (frame->len < 2U) {
    return UDS_TP_ERR_PARAM;
  }
  if (max_data > buf_size) {
    return UDS_TP_ERR_BUF_FULL;
  }

  *sn_out = frame->data[0] & 0x0FU;
  memcpy(buf, &frame->data[1], max_data);
  return UDS_TP_OK;
}

int uds_tp_decode_fc_fd(const struct canfd_frame *frame, UdsTpFlowStatus *fs,
                        uint8_t *bs, uint8_t *stmin) {
  if (!frame || !fs || !bs || !stmin) {
    return UDS_TP_ERR_PARAM;
  }
  if (uds_tp_frame_type_fd(frame) != UDS_TP_FRAME_FC) {
    return UDS_TP_ERR_FRAME_TYPE;
  }
  if (frame->len < 3U) {
    return UDS_TP_ERR_PARAM;
  }

  uint8_t fs_raw = frame->data[0] & 0x0FU;
  if (fs_raw > (uint8_t)UDS_TP_FC_OVERFLOW) {
    return UDS_TP_ERR_PARAM;
  }

  *fs = (UdsTpFlowStatus)fs_raw;
  *bs = frame->data[1];
  *stmin = frame->data[2];
  return UDS_TP_OK;
}

/* ── CAN FD high-level send ─────────────────────────────────────────────── */

int uds_tp_send_fd(UdsCanSocket *sock, uint32_t tx_id, const uint8_t *data,
                   size_t len, const UdsTpConfig *cfg) {
  if (!sock || !data || len == 0U || !cfg) {
    return UDS_TP_ERR_PARAM;
  }
  if (cfg->timeout_ms == 0U) {
    return UDS_TP_ERR_PARAM;
  }

  struct canfd_frame frame;

  /* ── Single Frame path ──────────────────────────────────────────────── */
  if (len <= UDS_TP_CANFD_SF_MAX_DATA) {
    int rc = uds_tp_encode_sf_fd(&frame, tx_id, data, len);
    if (rc != UDS_TP_OK) {
      return rc;
    }
    if (uds_can_send_fd(sock, &frame) != UDS_CAN_OK) {
      return UDS_TP_ERR_SEND;
    }
    return UDS_TP_OK;
  }

  /* ── Multi-frame path ───────────────────────────────────────────────── */
  /* Reject PDU lengths that exceed the 32-bit FF_DL field */
  if (len > (size_t)UDS_TP_CANFD_MAX_PDU_LEN) {
    return UDS_TP_ERR_PDU_LEN;
  }

  int rc = uds_tp_encode_ff_fd(&frame, tx_id, data, (uint32_t)len);
  if (rc != UDS_TP_OK) {
    return rc;
  }
  if (uds_can_send_fd(sock, &frame) != UDS_CAN_OK) {
    return UDS_TP_ERR_SEND;
  }

  /* Determine how many bytes were embedded in the FF */
  size_t ff_embed = (len <= UDS_TP_CANFD_FF_EXT_THRESHOLD)
                        ? UDS_TP_CANFD_FF_DATA_BYTES
                        : UDS_TP_CANFD_FF_EXT_DATA_BYTES;
  if (ff_embed > len) {
    ff_embed = len;
  }

  size_t offset = ff_embed;
  uint8_t sn = 1U;

  while (offset < len) {
    /* Wait for a Flow Control frame from the receiver */
    struct canfd_frame fc;
    int recv_rc = uds_can_recv_fd(sock, &fc, cfg->timeout_ms);
    if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
      return UDS_TP_ERR_TIMEOUT;
    }
    if (recv_rc != UDS_CAN_OK) {
      return UDS_TP_ERR_RECV;
    }

    UdsTpFlowStatus fs;
    uint8_t bs, stmin;
    rc = uds_tp_decode_fc_fd(&fc, &fs, &bs, &stmin);
    if (rc != UDS_TP_OK) {
      return rc;
    }

    if (fs == UDS_TP_FC_OVERFLOW) {
      return UDS_TP_ERR_OVERFLOW;
    }

    /* Handle WAIT frames */
    unsigned int wait_count = 0U;
    while (fs == UDS_TP_FC_WAIT) {
      if (++wait_count > UDS_TP_MAX_WAIT_FRAMES) {
        return UDS_TP_ERR_WAIT_LIMIT;
      }
      recv_rc = uds_can_recv_fd(sock, &fc, cfg->timeout_ms);
      if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
        return UDS_TP_ERR_TIMEOUT;
      }
      if (recv_rc != UDS_CAN_OK) {
        return UDS_TP_ERR_RECV;
      }
      rc = uds_tp_decode_fc_fd(&fc, &fs, &bs, &stmin);
      if (rc != UDS_TP_OK) {
        return rc;
      }
      if (fs == UDS_TP_FC_OVERFLOW) {
        return UDS_TP_ERR_OVERFLOW;
      }
    }

    /* CTS received: send a block of Consecutive Frames */
    uint8_t bs_sent = 0U;
    while (offset < len) {
      apply_stmin(stmin);

      size_t remaining = len - offset;
      size_t chunk = (remaining > UDS_TP_CANFD_CF_DATA_BYTES)
                         ? UDS_TP_CANFD_CF_DATA_BYTES
                         : remaining;

      rc = uds_tp_encode_cf_fd(&frame, tx_id, &data[offset], chunk, sn);
      if (rc != UDS_TP_OK) {
        return rc;
      }
      if (uds_can_send_fd(sock, &frame) != UDS_CAN_OK) {
        return UDS_TP_ERR_SEND;
      }

      sn = (uint8_t)((sn + 1U) & 0x0FU);
      offset += chunk;
      bs_sent++;

      if (bs > 0U && bs_sent >= bs) {
        break;
      }
    }
  }

  return UDS_TP_OK;
}

/* ── CAN FD high-level receive ──────────────────────────────────────────── */

int uds_tp_recv_fd(UdsCanSocket *sock, uint32_t tx_id, uint8_t *buf,
                   size_t buf_size, size_t *out_len, const UdsTpConfig *cfg) {
  if (!sock || !buf || !out_len || buf_size == 0U || !cfg) {
    return UDS_TP_ERR_PARAM;
  }
  if (cfg->timeout_ms == 0U) {
    return UDS_TP_ERR_PARAM;
  }

  struct canfd_frame frame;

  /* Wait for first incoming frame (SF or FF) */
  int recv_rc = uds_can_recv_fd(sock, &frame, cfg->timeout_ms);
  if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
    return UDS_TP_ERR_TIMEOUT;
  }
  if (recv_rc != UDS_CAN_OK) {
    return UDS_TP_ERR_RECV;
  }

  UdsTpFrameType ft = uds_tp_frame_type_fd(&frame);

  /* ── Single Frame path ──────────────────────────────────────────────── */
  if (ft == UDS_TP_FRAME_SF) {
    return uds_tp_decode_sf_fd(&frame, buf, buf_size, out_len);
  }

  /* ── Multi-frame path ───────────────────────────────────────────────── */
  if (ft != UDS_TP_FRAME_FF) {
    return UDS_TP_ERR_FRAME_TYPE;
  }

  uint32_t total_len = 0U;
  size_t ff_data_bytes = 0U;
  int rc =
      uds_tp_decode_ff_fd(&frame, buf, buf_size, &total_len, &ff_data_bytes);
  if (rc != UDS_TP_OK) {
    return rc;
  }
  if ((size_t)total_len > buf_size) {
    return UDS_TP_ERR_BUF_FULL;
  }

  /* Send initial Flow Control (CTS) */
  struct canfd_frame fc;
  rc = uds_tp_encode_fc_fd(&fc, tx_id, UDS_TP_FC_CTS, cfg->block_size,
                           cfg->st_min);
  if (rc != UDS_TP_OK) {
    return rc;
  }
  if (uds_can_send_fd(sock, &fc) != UDS_CAN_OK) {
    return UDS_TP_ERR_SEND;
  }

  uint32_t received = (uint32_t)ff_data_bytes;
  uint8_t expected_sn = 1U;
  uint8_t bs_count = 0U;

  while (received < total_len) {
    /* Re-send FC if block size is exhausted */
    if (cfg->block_size > 0U && bs_count >= cfg->block_size) {
      rc = uds_tp_encode_fc_fd(&fc, tx_id, UDS_TP_FC_CTS, cfg->block_size,
                               cfg->st_min);
      if (rc != UDS_TP_OK) {
        return rc;
      }
      if (uds_can_send_fd(sock, &fc) != UDS_CAN_OK) {
        return UDS_TP_ERR_SEND;
      }
      bs_count = 0U;
    }

    /* Receive next Consecutive Frame */
    recv_rc = uds_can_recv_fd(sock, &frame, cfg->timeout_ms);
    if (recv_rc == UDS_CAN_ERR_TIMEOUT) {
      return UDS_TP_ERR_TIMEOUT;
    }
    if (recv_rc != UDS_CAN_OK) {
      return UDS_TP_ERR_RECV;
    }

    if (uds_tp_frame_type_fd(&frame) != UDS_TP_FRAME_CF) {
      return UDS_TP_ERR_FRAME_TYPE;
    }

    uint32_t remaining = total_len - received;
    size_t chunk = (remaining > UDS_TP_CANFD_CF_DATA_BYTES)
                       ? UDS_TP_CANFD_CF_DATA_BYTES
                       : (size_t)remaining;

    uint8_t sn = 0U;
    rc = uds_tp_decode_cf_fd(&frame, &sn, buf + received, buf_size - received,
                             chunk);
    if (rc != UDS_TP_OK) {
      return rc;
    }

    if (sn != expected_sn) {
      return UDS_TP_ERR_SEQUENCE;
    }

    received += (uint32_t)chunk;
    expected_sn = (uint8_t)((expected_sn + 1U) & 0x0FU);
    bs_count++;
  }

  *out_len = (size_t)total_len;
  return UDS_TP_OK;
}

#endif /* CAN_FD_ENABLED */
