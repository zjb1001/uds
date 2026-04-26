/**
 * @file uds_tp.h
 * @brief ISO-TP (ISO 15765-2) transport layer for the UDS Diagnostic System.
 *
 * Provides:
 *   - Frame-level encode/decode for SF / FF / CF / FC (pure functions, no I/O)
 *   - A lightweight receiver state machine (UdsTpRxChannel)
 *   - High-level uds_tp_send() / uds_tp_recv() that do actual CAN I/O
 *
 * Classic CAN (8-byte frames) support:
 *   - Max SF payload: 7 bytes; Max PDU: 4095 bytes.
 *
 * CAN FD (64-byte frames) support is available when built with
 * -DENABLE_CAN_FD=ON (defines CAN_FD_ENABLED).  The FD variants
 * (uds_tp_encode_sf_fd, uds_tp_send_fd, etc.) are only compiled in that
 * configuration and operate on struct canfd_frame instead of struct can_frame.
 * Per ISO 15765-2, CAN FD SF can carry up to 62 bytes in a single frame; CFs
 * carry up to 63 bytes each.
 *
 * Thread-safety: the encode/decode helpers are reentrant.
 * uds_tp_send() and uds_tp_recv() are NOT thread-safe with respect to a
 * shared UdsCanSocket; callers must provide external locking when necessary.
 *
 * The caller is responsible for configuring CAN socket receive filters
 * (via uds_can_set_filter()) before calling uds_tp_send() / uds_tp_recv().
 */

#ifndef UDS_TP_H
#define UDS_TP_H

#include "uds_can.h"

#include <linux/can.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */

/** Maximum PDU length for standard ISO-TP (12-bit FF length field). */
#define UDS_TP_MAX_PDU_LEN 4095U

/** Maximum payload bytes in a Single Frame (8-byte CAN, 1 PCI byte). */
#define UDS_TP_SF_MAX_DATA 7U

/** Payload bytes embedded in a First Frame (2 PCI bytes → 6 data bytes). */
#define UDS_TP_FF_DATA_BYTES 6U

/** Payload bytes in a Consecutive Frame (1 PCI byte → 7 data bytes). */
#define UDS_TP_CF_DATA_BYTES 7U

/** Consecutive Frame sequence number wraps at this value (4-bit field). */
#define UDS_TP_SN_MAX 15U

/** Maximum FC WAIT frames accepted before aborting a send. */
#define UDS_TP_MAX_WAIT_FRAMES 10U

/* ── Enumerations ───────────────────────────────────────────────────────── */

/**
 * @brief ISO-TP frame type (upper nibble of the first PCI byte).
 */
typedef enum {
  UDS_TP_FRAME_SF = 0,       /**< Single Frame */
  UDS_TP_FRAME_FF = 1,       /**< First Frame */
  UDS_TP_FRAME_CF = 2,       /**< Consecutive Frame */
  UDS_TP_FRAME_FC = 3,       /**< Flow Control */
  UDS_TP_FRAME_UNKNOWN = -1, /**< Invalid / unrecognised */
} UdsTpFrameType;

/**
 * @brief Flow Control status field (lower nibble of the FC PCI byte).
 */
typedef enum {
  UDS_TP_FC_CTS = 0,      /**< Continue to Send */
  UDS_TP_FC_WAIT = 1,     /**< Wait */
  UDS_TP_FC_OVERFLOW = 2, /**< Overflow — abort transfer */
} UdsTpFlowStatus;

/**
 * @brief Return codes for all uds_tp_* functions.
 *
 * Zero indicates success; negative values indicate errors.
 */
typedef enum {
  UDS_TP_OK = 0,               /**< Success */
  UDS_TP_ERR_PARAM = -1,       /**< Invalid parameter */
  UDS_TP_ERR_TIMEOUT = -2,     /**< Receive timed out */
  UDS_TP_ERR_OVERFLOW = -3,    /**< FC reported overflow / buffer too small */
  UDS_TP_ERR_SEQUENCE = -4,    /**< Unexpected CF sequence number */
  UDS_TP_ERR_FRAME_TYPE = -5,  /**< Unexpected or invalid frame type */
  UDS_TP_ERR_BUF_FULL = -6,    /**< Caller buffer too small for PDU */
  UDS_TP_ERR_SEND = -7,        /**< CAN send failure */
  UDS_TP_ERR_INPROGRESS = -8,  /**< Transfer already in progress */
  UDS_TP_ERR_RECV = -9,        /**< CAN receive failure */
  UDS_TP_ERR_PDU_LEN = -10,    /**< PDU length out of range */
  UDS_TP_ERR_WAIT_LIMIT = -11, /**< Too many FC WAIT frames received */
} UdsTpError;

/* ── Configuration ──────────────────────────────────────────────────────── */

/**
 * @brief Per-transfer ISO-TP configuration.
 *
 * Governs timeouts and the Flow Control parameters this node advertises
 * when acting as the receiver.
 */
typedef struct {
  unsigned int timeout_ms; /**< N_As / N_Bs / N_Cr timeout (> 0, ms). */
  uint8_t block_size;      /**< FC Block Size: 0 = all CFs without pause. */
  uint8_t st_min; /**< FC Separation Time minimum (raw byte, 0x00-0xFF). */
} UdsTpConfig;

/** Convenience initialiser: 1 s timeout, no block limit, no STmin delay. */
#define UDS_TP_DEFAULT_CONFIG                                                  \
  { .timeout_ms = 1000U, .block_size = 0U, .st_min = 0U }

/* ── Receiver channel ───────────────────────────────────────────────────── */

/**
 * @brief Receiver state machine for multi-frame ISO-TP reassembly.
 *
 * Initialise with uds_tp_rx_init() before the first call to uds_tp_recv().
 * The caller retains ownership of @p buf.
 */
typedef struct {
  uint8_t *buf;      /**< Caller-supplied receive buffer. */
  size_t buf_size;   /**< Capacity of @p buf in bytes. */
  uint32_t msg_len;  /**< Total PDU length declared in the FF PCI. */
  uint32_t received; /**< Bytes written to buf so far. */
  uint8_t next_sn;   /**< Expected CF sequence number (0..15, wraps). */
  bool in_progress;  /**< True while a multi-frame transfer is active. */
} UdsTpRxChannel;

/* ── Frame classification ───────────────────────────────────────────────── */

/**
 * @brief Determine the ISO-TP frame type of a raw CAN frame.
 *
 * Inspects the upper nibble of @p frame->data[0].
 *
 * @param frame  Non-NULL pointer to a received CAN frame.
 * @return Frame type, or UDS_TP_FRAME_UNKNOWN if @p frame is NULL or DLC == 0.
 */
UdsTpFrameType uds_tp_frame_type(const struct can_frame *frame);

/* ── Frame encode ───────────────────────────────────────────────────────── */

/**
 * @brief Encode a Single Frame (SF).
 *
 * Valid when @p len is in [1 … UDS_TP_SF_MAX_DATA].
 *
 * @param[out] out    CAN frame to populate.
 * @param[in]  can_id CAN identifier.
 * @param[in]  data   Payload bytes.
 * @param[in]  len    Payload length (1–7).
 * @return UDS_TP_OK or UDS_TP_ERR_PARAM.
 */
int uds_tp_encode_sf(struct can_frame *out, uint32_t can_id,
                     const uint8_t *data, size_t len);

/**
 * @brief Encode a First Frame (FF).
 *
 * Embeds the first UDS_TP_FF_DATA_BYTES of @p data; remaining bytes
 * must be sent as Consecutive Frames.
 *
 * @param[out] out        CAN frame to populate.
 * @param[in]  can_id     CAN identifier.
 * @param[in]  data       Start of the full PDU payload.
 * @param[in]  total_len  Total PDU length (8 … UDS_TP_MAX_PDU_LEN).
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, or UDS_TP_ERR_PDU_LEN.
 */
int uds_tp_encode_ff(struct can_frame *out, uint32_t can_id,
                     const uint8_t *data, uint32_t total_len);

/**
 * @brief Encode a Consecutive Frame (CF).
 *
 * Embeds up to UDS_TP_CF_DATA_BYTES of @p data; the remaining bytes
 * in the 8-byte frame are zero-padded.
 *
 * @param[out] out    CAN frame to populate.
 * @param[in]  can_id CAN identifier.
 * @param[in]  data   Next payload chunk.
 * @param[in]  len    Chunk length (1–7).
 * @param[in]  sn     Sequence number (0–15); the lower 4 bits are used.
 * @return UDS_TP_OK or UDS_TP_ERR_PARAM.
 */
int uds_tp_encode_cf(struct can_frame *out, uint32_t can_id,
                     const uint8_t *data, size_t len, uint8_t sn);

/**
 * @brief Encode a Flow Control frame (FC).
 *
 * @param[out] out    CAN frame to populate.
 * @param[in]  can_id CAN identifier.
 * @param[in]  fs     Flow status (CTS / Wait / Overflow).
 * @param[in]  bs     Block Size (0 = no limit).
 * @param[in]  stmin  Separation time minimum (raw byte 0x00–0xFF).
 * @return UDS_TP_OK or UDS_TP_ERR_PARAM.
 */
int uds_tp_encode_fc(struct can_frame *out, uint32_t can_id, UdsTpFlowStatus fs,
                     uint8_t bs, uint8_t stmin);

/* ── Frame decode ───────────────────────────────────────────────────────── */

/**
 * @brief Decode a Single Frame, copying the payload into @p buf.
 *
 * @param[in]  frame     CAN frame (must be type SF).
 * @param[out] buf       Destination buffer.
 * @param[in]  buf_size  Buffer capacity.
 * @param[out] out_len   Actual payload length on success.
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, UDS_TP_ERR_FRAME_TYPE,
 *         or UDS_TP_ERR_BUF_FULL.
 */
int uds_tp_decode_sf(const struct can_frame *frame, uint8_t *buf,
                     size_t buf_size, size_t *out_len);

/**
 * @brief Decode the header and first data chunk of a First Frame.
 *
 * Copies exactly UDS_TP_FF_DATA_BYTES into @p buf.
 *
 * @param[in]  frame      CAN frame (must be type FF).
 * @param[out] buf        Destination buffer (must hold ≥ UDS_TP_FF_DATA_BYTES).
 * @param[in]  buf_size   Buffer capacity.
 * @param[out] total_len  Declared total PDU length (from FF PCI).
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, UDS_TP_ERR_FRAME_TYPE,
 *         or UDS_TP_ERR_PDU_LEN.
 */
int uds_tp_decode_ff(const struct can_frame *frame, uint8_t *buf,
                     size_t buf_size, uint32_t *total_len);

/**
 * @brief Decode a Consecutive Frame, extracting the sequence number and
 *        up to @p max_data payload bytes.
 *
 * The caller should pass min(UDS_TP_CF_DATA_BYTES, remaining_bytes) as
 * @p max_data to avoid writing beyond the expected PDU boundary.
 *
 * @param[in]  frame     CAN frame (must be type CF).
 * @param[out] sn_out    Sequence number (0–15).
 * @param[out] buf       Destination buffer.
 * @param[in]  buf_size  Buffer capacity.
 * @param[in]  max_data  Maximum bytes to copy (1–UDS_TP_CF_DATA_BYTES).
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, UDS_TP_ERR_FRAME_TYPE,
 *         or UDS_TP_ERR_BUF_FULL.
 */
int uds_tp_decode_cf(const struct can_frame *frame, uint8_t *sn_out,
                     uint8_t *buf, size_t buf_size, size_t max_data);

/**
 * @brief Decode a Flow Control frame.
 *
 * @param[in]  frame  CAN frame (must be type FC).
 * @param[out] fs     Flow status.
 * @param[out] bs     Block size.
 * @param[out] stmin  Separation time minimum (raw byte).
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, or UDS_TP_ERR_FRAME_TYPE.
 */
int uds_tp_decode_fc(const struct can_frame *frame, UdsTpFlowStatus *fs,
                     uint8_t *bs, uint8_t *stmin);

/* ── Receiver channel ───────────────────────────────────────────────────── */

/**
 * @brief Initialise a UdsTpRxChannel before first use.
 *
 * @param[out] ch        Channel to initialise.
 * @param[in]  buf       Caller-supplied receive buffer.
 * @param[in]  buf_size  Buffer capacity (must be > 0).
 * @return UDS_TP_OK or UDS_TP_ERR_PARAM.
 */
int uds_tp_rx_init(UdsTpRxChannel *ch, uint8_t *buf, size_t buf_size);

/* ── High-level send / receive ──────────────────────────────────────────── */

/**
 * @brief Send a complete UDS PDU over ISO-TP.
 *
 * Automatically selects Single Frame (≤7 bytes) or multi-frame
 * (First Frame + Consecutive Frames) transmission, and handles
 * Flow Control (CTS / Wait / Overflow) from the remote side.
 *
 * The caller must configure the socket receive filter to accept FC frames
 * from the remote node before calling this function.
 *
 * @param[in] sock   Open CAN socket.
 * @param[in] tx_id  CAN ID used for outgoing data frames.
 * @param[in] data   PDU payload.
 * @param[in] len    PDU length (1 … UDS_TP_MAX_PDU_LEN).
 * @param[in] cfg    Transfer configuration (must not be NULL, timeout_ms > 0).
 * @return UDS_TP_OK on success, or a negative UdsTpError code.
 */
int uds_tp_send(UdsCanSocket *sock, uint32_t tx_id, const uint8_t *data,
                size_t len, const UdsTpConfig *cfg);

/**
 * @brief Receive a complete UDS PDU over ISO-TP.
 *
 * Blocks until the full PDU is reassembled, a timeout occurs, or an
 * error is detected.  Automatically sends Flow Control frames in response
 * to First Frames.
 *
 * The caller must configure the socket receive filter to accept data frames
 * from the remote node before calling this function.
 *
 * @param[in]  sock      Open CAN socket.
 * @param[in]  tx_id     CAN ID used for outgoing FC frames.
 * @param[out] buf       Caller-supplied buffer for the reassembled PDU.
 * @param[in]  buf_size  Buffer capacity.
 * @param[out] out_len   Actual PDU length on success.
 * @param[in]  cfg       Transfer configuration (must not be NULL, timeout_ms >
 * 0).
 * @return UDS_TP_OK on success, or a negative UdsTpError code.
 */
int uds_tp_recv(UdsCanSocket *sock, uint32_t tx_id, uint8_t *buf,
                size_t buf_size, size_t *out_len, const UdsTpConfig *cfg);

/* ── Utility ────────────────────────────────────────────────────────────── */

/**
 * @brief Return a human-readable string for a UdsTpError code.
 *
 * @param err Error code.
 * @return Pointer to a static string; never NULL.
 */
const char *uds_tp_strerror(int err);

/* ── CAN FD extensions (ISO 15765-2 CAN FD) ────────────────────────────── */

#ifdef CAN_FD_ENABLED

#include <linux/can.h>

/** Minimum SF_DL value when using the CAN FD escape sequence (ISO 15765-2:
 *  escape sequence is only used when SF_DL > UDS_TP_SF_MAX_DATA). */
#define UDS_TP_CANFD_SF_ESCAPE_MIN_DL (UDS_TP_SF_MAX_DATA + 1U) /* 8 */

/** Maximum payload bytes in a CAN FD Single Frame using the escape sequence
 *  (PCI byte 0x00 + SF_DL byte): 64 - 2 = 62. */
#define UDS_TP_CANFD_SF_MAX_DATA 62U

/** Payload bytes embedded in a CAN FD First Frame (regular, 12-bit FF_DL):
 *  64 frame bytes - 2 PCI bytes = 62. */
#define UDS_TP_CANFD_FF_DATA_BYTES 62U

/** Payload bytes embedded in a CAN FD First Frame (extended, 32-bit FF_DL):
 *  64 frame bytes - 6 PCI bytes = 58. */
#define UDS_TP_CANFD_FF_EXT_DATA_BYTES 58U

/** Maximum payload bytes in a CAN FD Consecutive Frame:
 *  64 frame bytes - 1 PCI byte = 63. */
#define UDS_TP_CANFD_CF_DATA_BYTES 63U

/** FF_DL threshold above which the extended (32-bit) FF format must be used
 *  (ISO 15765-2: regular FF_DL field is 12 bits → max 4095). */
#define UDS_TP_CANFD_FF_EXT_THRESHOLD 4095U

/** Maximum PDU length with CAN FD extended First Frame (32-bit FF_DL). */
#define UDS_TP_CANFD_MAX_PDU_LEN 0xFFFFFFFFUL

/* ── CAN FD frame type classification ──────────────────────────────────── */

/**
 * @brief Determine the ISO-TP frame type of a raw CAN FD frame.
 *
 * @param frame  Non-NULL pointer to a received CAN FD frame.
 * @return Frame type, or UDS_TP_FRAME_UNKNOWN if @p frame is NULL or len == 0.
 */
UdsTpFrameType uds_tp_frame_type_fd(const struct canfd_frame *frame);

/* ── CAN FD frame encode ────────────────────────────────────────────────── */

/**
 * @brief Encode a CAN FD Single Frame (SF).
 *
 * For @p len in [1 … 7]: classic one-byte PCI (compatible with classic CAN).
 * For @p len in [8 … UDS_TP_CANFD_SF_MAX_DATA]: two-byte escape-sequence PCI
 * (data[0] = 0x00, data[1] = SF_DL) per ISO 15765-2.
 *
 * @param[out] out    CAN FD frame to populate.
 * @param[in]  can_id CAN identifier.
 * @param[in]  data   Payload bytes.
 * @param[in]  len    Payload length (1 … UDS_TP_CANFD_SF_MAX_DATA).
 * @return UDS_TP_OK or UDS_TP_ERR_PARAM.
 */
int uds_tp_encode_sf_fd(struct canfd_frame *out, uint32_t can_id,
                        const uint8_t *data, size_t len);

/**
 * @brief Encode a CAN FD First Frame (FF).
 *
 * Selects regular (12-bit FF_DL, ≤ 4095) or extended (32-bit FF_DL, > 4095)
 * PCI format automatically.  Embeds the first available data bytes into the
 * frame (62 bytes for regular; 58 bytes for extended).
 *
 * @param[out] out        CAN FD frame to populate.
 * @param[in]  can_id     CAN identifier.
 * @param[in]  data       Start of the full PDU payload.
 * @param[in]  total_len  Total PDU length (> UDS_TP_CANFD_SF_MAX_DATA).
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, or UDS_TP_ERR_PDU_LEN.
 */
int uds_tp_encode_ff_fd(struct canfd_frame *out, uint32_t can_id,
                        const uint8_t *data, uint32_t total_len);

/**
 * @brief Encode a CAN FD Consecutive Frame (CF).
 *
 * @param[out] out    CAN FD frame to populate.
 * @param[in]  can_id CAN identifier.
 * @param[in]  data   Next payload chunk.
 * @param[in]  len    Chunk length (1 … UDS_TP_CANFD_CF_DATA_BYTES).
 * @param[in]  sn     Sequence number (0–15); the lower 4 bits are used.
 * @return UDS_TP_OK or UDS_TP_ERR_PARAM.
 */
int uds_tp_encode_cf_fd(struct canfd_frame *out, uint32_t can_id,
                        const uint8_t *data, size_t len, uint8_t sn);

/**
 * @brief Encode a CAN FD Flow Control frame (FC).
 *
 * @param[out] out    CAN FD frame to populate.
 * @param[in]  can_id CAN identifier.
 * @param[in]  fs     Flow status (CTS / Wait / Overflow).
 * @param[in]  bs     Block Size (0 = no limit).
 * @param[in]  stmin  Separation time minimum (raw byte 0x00–0xFF).
 * @return UDS_TP_OK or UDS_TP_ERR_PARAM.
 */
int uds_tp_encode_fc_fd(struct canfd_frame *out, uint32_t can_id,
                        UdsTpFlowStatus fs, uint8_t bs, uint8_t stmin);

/* ── CAN FD frame decode ────────────────────────────────────────────────── */

/**
 * @brief Decode a CAN FD Single Frame, copying the payload into @p buf.
 *
 * Handles both the classic (1-byte PCI) and escape-sequence (2-byte PCI)
 * formats transparently.
 *
 * @param[in]  frame     CAN FD frame (must be type SF).
 * @param[out] buf       Destination buffer.
 * @param[in]  buf_size  Buffer capacity.
 * @param[out] out_len   Actual payload length on success.
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, UDS_TP_ERR_FRAME_TYPE,
 *         or UDS_TP_ERR_BUF_FULL.
 */
int uds_tp_decode_sf_fd(const struct canfd_frame *frame, uint8_t *buf,
                        size_t buf_size, size_t *out_len);

/**
 * @brief Decode the header and first data chunk of a CAN FD First Frame.
 *
 * Handles both regular (12-bit FF_DL) and extended (32-bit FF_DL) formats.
 * Copies the embedded data bytes (up to UDS_TP_CANFD_FF_DATA_BYTES or
 * UDS_TP_CANFD_FF_EXT_DATA_BYTES depending on format) into @p buf.
 *
 * @param[in]  frame      CAN FD frame (must be type FF).
 * @param[out] buf        Destination buffer.
 * @param[in]  buf_size   Buffer capacity.
 * @param[out] total_len  Declared total PDU length (from FF PCI).
 * @param[out] ff_data_bytes  Number of payload bytes embedded in this FF.
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, UDS_TP_ERR_FRAME_TYPE,
 *         or UDS_TP_ERR_PDU_LEN.
 */
int uds_tp_decode_ff_fd(const struct canfd_frame *frame, uint8_t *buf,
                        size_t buf_size, uint32_t *total_len,
                        size_t *ff_data_bytes);

/**
 * @brief Decode a CAN FD Consecutive Frame.
 *
 * @param[in]  frame     CAN FD frame (must be type CF).
 * @param[out] sn_out    Sequence number (0–15).
 * @param[out] buf       Destination buffer.
 * @param[in]  buf_size  Buffer capacity.
 * @param[in]  max_data  Maximum bytes to copy (1–UDS_TP_CANFD_CF_DATA_BYTES).
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, UDS_TP_ERR_FRAME_TYPE,
 *         or UDS_TP_ERR_BUF_FULL.
 */
int uds_tp_decode_cf_fd(const struct canfd_frame *frame, uint8_t *sn_out,
                        uint8_t *buf, size_t buf_size, size_t max_data);

/**
 * @brief Decode a CAN FD Flow Control frame.
 *
 * @param[in]  frame  CAN FD frame (must be type FC).
 * @param[out] fs     Flow status.
 * @param[out] bs     Block size.
 * @param[out] stmin  Separation time minimum (raw byte).
 * @return UDS_TP_OK, UDS_TP_ERR_PARAM, or UDS_TP_ERR_FRAME_TYPE.
 */
int uds_tp_decode_fc_fd(const struct canfd_frame *frame, UdsTpFlowStatus *fs,
                        uint8_t *bs, uint8_t *stmin);

/* ── CAN FD high-level send / receive ───────────────────────────────────── */

/**
 * @brief Send a complete UDS PDU over ISO-TP using CAN FD frames.
 *
 * Automatically selects Single Frame (≤ UDS_TP_CANFD_SF_MAX_DATA bytes) or
 * multi-frame transmission.  Handles Flow Control (CTS / Wait / Overflow).
 *
 * The socket must have been prepared with uds_can_enable_fd() first.
 *
 * @param[in] sock   Open CAN socket (with CAN FD enabled).
 * @param[in] tx_id  CAN ID for outgoing data frames.
 * @param[in] data   PDU payload.
 * @param[in] len    PDU length (1 … UDS_TP_CANFD_MAX_PDU_LEN).
 * @param[in] cfg    Transfer configuration (must not be NULL, timeout_ms > 0).
 * @return UDS_TP_OK on success, or a negative UdsTpError code.
 */
int uds_tp_send_fd(UdsCanSocket *sock, uint32_t tx_id, const uint8_t *data,
                   size_t len, const UdsTpConfig *cfg);

/**
 * @brief Receive a complete UDS PDU over ISO-TP using CAN FD frames.
 *
 * Blocks until the full PDU is reassembled, a timeout occurs, or an error is
 * detected.  Automatically sends Flow Control frames in response to First
 * Frames.
 *
 * The socket must have been prepared with uds_can_enable_fd() first.
 *
 * @param[in]  sock      Open CAN socket (with CAN FD enabled).
 * @param[in]  tx_id     CAN ID for outgoing FC frames.
 * @param[out] buf       Caller-supplied buffer for the reassembled PDU.
 * @param[in]  buf_size  Buffer capacity.
 * @param[out] out_len   Actual PDU length on success.
 * @param[in]  cfg       Transfer configuration (must not be NULL, timeout_ms >
 * 0).
 * @return UDS_TP_OK on success, or a negative UdsTpError code.
 */
int uds_tp_recv_fd(UdsCanSocket *sock, uint32_t tx_id, uint8_t *buf,
                   size_t buf_size, size_t *out_len, const UdsTpConfig *cfg);

#endif /* CAN_FD_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* UDS_TP_H */
