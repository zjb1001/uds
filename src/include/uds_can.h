/**
 * @file uds_can.h
 * @brief SocketCAN adapter layer for the UDS Diagnostic Simulation System.
 *
 * Provides a thin, testable wrapper around Linux SocketCAN raw sockets.
 * Covers socket lifecycle, CAN frame send/receive, CAN ID filter management,
 * and UDS-specific CAN ID helpers (0x600/0x680 scheme, functional 0x7DF).
 *
 * All functions are thread-safe with respect to distinct file descriptors.
 * Sharing a single fd across threads requires external locking by the caller.
 *
 * ISO-TP segmentation is NOT handled here; see uds_tp.h.
 */

#ifndef UDS_CAN_H
#define UDS_CAN_H

#include <linux/can.h>
#include <linux/can/raw.h>
#include <stdint.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */

/** Maximum number of CAN ID filters per socket. */
#define UDS_CAN_MAX_FILTERS 32

/** Base CAN ID for physical-addressing requests  (Tester → ECU).
 *  Full request ID = UDS_CAN_REQ_BASE + ecu_id  (e.g. 0x601 for ECU 1). */
#define UDS_CAN_REQ_BASE 0x600U

/** Base CAN ID for physical-addressing responses (ECU → Tester).
 *  Full response ID = UDS_CAN_RESP_BASE + ecu_id. */
#define UDS_CAN_RESP_BASE 0x680U

/** Functional addressing broadcast ID (ISO 14229 / ISO 15765-2). */
#define UDS_CAN_FUNCTIONAL_ID 0x7DFU

/** Minimum valid ECU ID. */
#define UDS_CAN_ECU_ID_MIN 0x01U

/** Maximum valid ECU ID. */
#define UDS_CAN_ECU_ID_MAX 0x7FU

/* ── Error codes ────────────────────────────────────────────────────────── */

/**
 * @brief Return codes for all uds_can_* functions.
 *
 * Zero or positive values indicate success; negative values indicate errors.
 */
typedef enum {
  UDS_CAN_OK = 0,           /**< Success */
  UDS_CAN_ERR_PARAM = -1,   /**< Invalid parameter */
  UDS_CAN_ERR_SOCKET = -2,  /**< socket() call failed */
  UDS_CAN_ERR_BIND = -3,    /**< bind() call failed */
  UDS_CAN_ERR_IFINDEX = -4, /**< Interface not found */
  UDS_CAN_ERR_FILTER = -5,  /**< setsockopt(SO_RX_FILTER) failed */
  UDS_CAN_ERR_SEND = -6,    /**< write()/send() failed */
  UDS_CAN_ERR_RECV = -7,    /**< read()/recv() failed */
  UDS_CAN_ERR_TIMEOUT = -8, /**< Receive timed out */
  UDS_CAN_ERR_TRUNC = -9,   /**< Received frame was truncated */
} UdsCanError;

/* ── Data types ─────────────────────────────────────────────────────────── */

/**
 * @brief Opaque handle for an open CAN socket.
 *
 * Users should treat this as opaque; the underlying int fd is exposed only
 * to allow integration with poll()/select() loops.
 */
typedef struct {
  int fd; /**< Raw SocketCAN file descriptor (read-only, do not modify). */
} UdsCanSocket;

/**
 * @brief CAN ID filter entry (mirrors struct can_filter from <linux/can.h>).
 */
typedef struct {
  uint32_t can_id;   /**< CAN ID to match (after masking). */
  uint32_t can_mask; /**< Mask applied before comparison. */
} UdsCanFilter;

/* ── Socket lifecycle ───────────────────────────────────────────────────── */

/**
 * @brief Open a SocketCAN raw socket and bind it to @p ifname.
 *
 * @param[out] sock   Handle to initialise on success.
 * @param[in]  ifname CAN interface name, e.g. "vcan0".
 * @return UDS_CAN_OK on success, or a negative UdsCanError code.
 *
 * @note The caller must call uds_can_close() when the socket is no longer
 *       needed, even after a partial-success (e.g. bind failure after a
 *       successful socket() call — the fd is still released internally).
 */
int uds_can_open(UdsCanSocket *sock, const char *ifname);

/**
 * @brief Close a SocketCAN socket opened with uds_can_open().
 *
 * Safe to call even if @p sock->fd is already -1 (idempotent).
 *
 * @param[in,out] sock Handle to close.
 */
void uds_can_close(UdsCanSocket *sock);

/* ── Filter management ──────────────────────────────────────────────────── */

/**
 * @brief Replace all CAN ID receive filters on @p sock.
 *
 * Passes @p filters directly to `setsockopt(CAN_RAW, CAN_RAW_FILTER, …)`.
 * Calling with @p count == 0 removes all filters (receive nothing).
 * Calling with @p filters == NULL and @p count == 0 resets to "receive all".
 *
 * @param[in] sock    Open socket handle.
 * @param[in] filters Array of filter entries (may be NULL when count == 0).
 * @param[in] count   Number of entries in @p filters (0 … UDS_CAN_MAX_FILTERS).
 * @return UDS_CAN_OK on success, or UDS_CAN_ERR_PARAM / UDS_CAN_ERR_FILTER.
 */
int uds_can_set_filter(UdsCanSocket *sock, const UdsCanFilter *filters,
                       unsigned int count);

/* ── Frame I/O ──────────────────────────────────────────────────────────── */

/**
 * @brief Send a single CAN frame.
 *
 * @param[in] sock  Open socket handle.
 * @param[in] frame CAN frame to transmit.
 * @return UDS_CAN_OK on success, or UDS_CAN_ERR_PARAM / UDS_CAN_ERR_SEND.
 */
int uds_can_send(UdsCanSocket *sock, const struct can_frame *frame);

/**
 * @brief Receive a single CAN frame, with an optional timeout.
 *
 * Blocks until a frame arrives or the timeout expires.
 *
 * @param[in]  sock       Open socket handle.
 * @param[out] frame      Buffer to receive into.
 * @param[in]  timeout_ms Timeout in milliseconds; 0 = block indefinitely.
 * @return UDS_CAN_OK on success, UDS_CAN_ERR_TIMEOUT if the deadline
 *         elapsed without a frame, or another negative UdsCanError on error.
 */
int uds_can_recv(UdsCanSocket *sock, struct can_frame *frame,
                 unsigned int timeout_ms);

/* ── UDS CAN ID helpers ─────────────────────────────────────────────────── */

/**
 * @brief Compute the physical-addressing request CAN ID for @p ecu_id.
 *
 * @param ecu_id ECU identifier (UDS_CAN_ECU_ID_MIN … UDS_CAN_ECU_ID_MAX).
 * @return Request CAN ID (e.g. 0x601 for ecu_id == 1), or 0 on error.
 */
uint32_t uds_can_req_id(uint8_t ecu_id);

/**
 * @brief Compute the response CAN ID for @p ecu_id.
 *
 * @param ecu_id ECU identifier (UDS_CAN_ECU_ID_MIN … UDS_CAN_ECU_ID_MAX).
 * @return Response CAN ID (e.g. 0x681 for ecu_id == 1), or 0 on error.
 */
uint32_t uds_can_resp_id(uint8_t ecu_id);

/**
 * @brief Derive the ECU ID from a response CAN ID.
 *
 * @param resp_id Response CAN ID (UDS_CAN_RESP_BASE+1 …
 * UDS_CAN_RESP_BASE+0x7F).
 * @return ECU ID on success, or 0 if @p resp_id is out of range.
 */
uint8_t uds_can_ecu_id_from_resp(uint32_t resp_id);

/**
 * @brief Populate a UdsCanFilter array to accept all diagnostic traffic for
 *        a single ECU (physical request + functional broadcast + response).
 *
 * Writes up to 3 entries into @p filters.
 *
 * @param[out] filters  Caller-allocated array with room for at least 3 entries.
 * @param[in]  ecu_id   ECU identifier (UDS_CAN_ECU_ID_MIN …
 * UDS_CAN_ECU_ID_MAX).
 * @return Number of filter entries written (3 on success, 0 on invalid ecu_id).
 */
unsigned int uds_can_ecu_filters(UdsCanFilter *filters, uint8_t ecu_id);

/**
 * @brief Return a human-readable string for a UdsCanError code.
 *
 * @param err Error code.
 * @return Pointer to a static string; never NULL.
 */
const char *uds_can_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CAN_H */
