/**
 * @file uds_core.h
 * @brief UDS core diagnostic services — Session Control and Security Access.
 *
 * Implements:
 *   - Service 0x10: Diagnostic Session Control (DSC)
 *   - Service 0x3E: Tester Present (TP)
 *   - Service 0x27: Security Access (seed / key)
 *
 * Design:
 *   - No dynamic memory allocation; all state is in caller-supplied structs.
 *   - Service functions produce raw UDS response bytes in the caller's buffer.
 *   - On success (UDS_CORE_OK) the response buffer holds the positive response.
 *   - On NRC failure (UDS_CORE_ERR_NRC) *nrc_out is set to the NRC byte;
 *     the caller is responsible for building the 0x7F negative response frame.
 *   - Thread-safety: each UdsCoreSession / UdsCoreSecurity instance is
 *     independent.  Concurrent access to the same instance requires external
 *     locking.
 *
 * Timing:
 *   - All internal timing uses CLOCK_MONOTONIC via clock_gettime().
 *   - UdsCoreSession.last_activity and UdsCoreSecurity.seed_ts /
 *     locked_ts are public so tests can manipulate them directly.
 */

#ifndef UDS_CORE_H
#define UDS_CORE_H

#include "uds_nrc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Return codes ───────────────────────────────────────────────────────── */

/**
 * @brief Return codes for all uds_core_* functions.
 *
 * Zero indicates success; negative values indicate errors.
 */
typedef enum {
  UDS_CORE_OK = 0,        /**< Success; response bytes written to caller buf. */
  UDS_CORE_ERR_PARAM = -1, /**< Invalid parameter (NULL pointer, bad range). */
  UDS_CORE_ERR_NRC = -2,  /**< UDS NRC condition; *nrc_out holds the NRC byte. */
  UDS_CORE_ERR_BUF = -3,  /**< Response buffer too small. */
} UdsCoreError;

/* ── Session layer ──────────────────────────────────────────────────────── */

/**
 * @brief UDS diagnostic session types (Service 0x10 sub-function).
 */
typedef enum {
  UDS_SESSION_DEFAULT      = 0x01, /**< defaultSession */
  UDS_SESSION_PROGRAMMING  = 0x02, /**< programmingSession */
  UDS_SESSION_EXTENDED     = 0x03, /**< extendedDiagnosticSession */
  UDS_SESSION_SAFETY       = 0x04, /**< safetySystemDiagnosticSession */
} UdsSessionType;

/**
 * @brief Per-ECU session timing configuration.
 *
 * All timeout values are in milliseconds unless noted.
 */
typedef struct {
  uint16_t p2_ms;      /**< P2 server response timeout (default 50 ms). */
  uint16_t p2_star_ms; /**< P2* server extended timeout (default 2000 ms). */
  uint32_t s3_ms;      /**< S3 session keep-alive timeout (default 10000 ms). */
} UdsCoreSessionConfig;

/** Default session config convenience initialiser. */
#define UDS_CORE_SESSION_DEFAULT_CONFIG                                         \
  { .p2_ms = 50U, .p2_star_ms = 2000U, .s3_ms = 10000U }

/**
 * @brief Diagnostic session state for one ECU.
 *
 * Initialise with uds_core_session_init() before use.
 * All fields are public to allow direct inspection in tests.
 */
typedef struct {
  uint8_t           ecu_id;          /**< ECU identifier. */
  UdsSessionType    type;            /**< Current session type. */
  UdsCoreSessionConfig cfg;          /**< Timing configuration. */
  uint8_t           security_level;  /**< Unlocked security level (0 = none). */
  struct timespec   last_activity;   /**< Monotonic timestamp of last activity. */
} UdsCoreSession;

/**
 * @brief Initialise a UdsCoreSession to the default session state.
 *
 * @param[out] sess   Session to initialise.
 * @param[in]  ecu_id ECU identifier.
 * @param[in]  cfg    Timing config; if NULL, defaults are used.
 */
void uds_core_session_init(UdsCoreSession *sess, uint8_t ecu_id,
                           const UdsCoreSessionConfig *cfg);

/**
 * @brief Process a Service 0x10 (Diagnostic Session Control) request.
 *
 * Updates the session type and resets S3 timer.
 * Resets security_level to 0 when transitioning to defaultSession.
 *
 * @param[in]  sess         Session state.
 * @param[in]  session_type Requested session type byte (0x01–0x04).
 * @param[out] resp         Buffer for positive response bytes.
 * @param[in]  resp_size    Capacity of @p resp (must be ≥ 6).
 * @param[out] resp_len     Number of response bytes written on success.
 * @param[out] nrc_out      NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_core_dsc(UdsCoreSession *sess, uint8_t session_type,
                 uint8_t *resp, size_t resp_size, size_t *resp_len,
                 uint8_t *nrc_out);

/**
 * @brief Process a Service 0x3E (Tester Present) request.
 *
 * Refreshes the S3 session keep-alive timer.
 * If the suppressPosRspMsgIndicationBit (bit 7 of sub_fn) is set,
 * *resp_len is set to 0 and the caller should not transmit a response.
 *
 * @param[in]  sess      Session state.
 * @param[in]  sub_fn    Sub-function byte (0x00 or 0x80 to suppress response).
 * @param[out] resp      Buffer for positive response bytes.
 * @param[in]  resp_size Capacity of @p resp (must be ≥ 2 when not suppressed).
 * @param[out] resp_len  Number of bytes to send (0 if suppressed).
 * @param[out] nrc_out   NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_core_tester_present(UdsCoreSession *sess, uint8_t sub_fn,
                             uint8_t *resp, size_t resp_size, size_t *resp_len,
                             uint8_t *nrc_out);

/**
 * @brief Return true if the S3 session keep-alive timer has expired.
 *
 * @param[in] sess Session state (may be NULL — returns true).
 * @return true if the session has timed out, false otherwise.
 */
bool uds_core_session_expired(const UdsCoreSession *sess);

/**
 * @brief Refresh the S3 keep-alive timer (update last_activity to now).
 *
 * @param[in,out] sess Session state.
 */
void uds_core_session_refresh(UdsCoreSession *sess);

/* ── Security access layer ──────────────────────────────────────────────── */

/** Seed and key length in bytes (fixed 4-byte XOR scheme). */
#define UDS_CORE_SEED_LEN 4U

/** Maximum failed key attempts before lockout. */
#define UDS_CORE_MAX_ATTEMPTS 3U

/** Lockout duration after exceeding max attempts (seconds). */
#define UDS_CORE_LOCKOUT_SECS 300U

/** Seed validity window (seconds); send key must arrive within this time. */
#define UDS_CORE_SEED_TIMEOUT_SECS 10U

/**
 * @brief Security access state for one ECU.
 *
 * Initialise with uds_core_security_init() before use.
 * Fields are public so tests can manipulate timestamps directly.
 */
typedef struct {
  uint8_t         seed[UDS_CORE_SEED_LEN]; /**< Most recently generated seed. */
  bool            seed_valid;    /**< True while seed awaits key verification. */
  uint8_t         pending_level; /**< Sub-fn (odd) used in requestSeed. */
  struct timespec seed_ts;       /**< Monotonic time when seed was generated. */
  uint8_t         fail_count;    /**< Failed key attempts for pending_level. */
  bool            locked;        /**< True while ECU is in lockout. */
  struct timespec locked_ts;     /**< Monotonic time when lockout started. */
  uint8_t         unlocked_level;/**< Odd sub_fn of currently unlocked level
                                      (0 = no level unlocked). */
} UdsCoreSecurity;

/**
 * @brief Initialise a UdsCoreSecurity struct (all fields zeroed).
 *
 * @param[out] sec Security state to initialise.
 */
void uds_core_security_init(UdsCoreSecurity *sec);

/**
 * @brief Process a Service 0x27 requestSeed (odd sub-function) request.
 *
 * Generates a random 4-byte seed and stores it with a validity timestamp.
 * If the level is already unlocked, returns a seed of all-zeros (per
 * ISO 14229-1 behaviour indicating "already authenticated").
 *
 * @param[in]  sec       Security state.
 * @param[in]  sub_fn    Sub-function byte (must be odd: 0x01, 0x03, …).
 * @param[out] resp      Buffer for positive response bytes (≥ 2+SEED_LEN).
 * @param[in]  resp_size Capacity of @p resp.
 * @param[out] resp_len  Number of bytes written on success.
 * @param[out] nrc_out   NRC byte when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_core_sec_request_seed(UdsCoreSecurity *sec, uint8_t sub_fn,
                               uint8_t *resp, size_t resp_size,
                               size_t *resp_len, uint8_t *nrc_out);

/**
 * @brief Process a Service 0x27 sendKey (even sub-function) request.
 *
 * Verifies the key against the previously issued seed.
 * On failure increments fail_count; on the third failure, locks the ECU
 * for UDS_CORE_LOCKOUT_SECS seconds (NRC 0x36).
 *
 * @param[in]  sec      Security state.
 * @param[in]  sub_fn   Sub-function byte (must be even: 0x02, 0x04, …).
 * @param[in]  key      Key bytes from the tester.
 * @param[in]  key_len  Length of @p key (must equal UDS_CORE_SEED_LEN).
 * @param[out] resp     Buffer for positive response bytes (≥ 2).
 * @param[in]  resp_size Capacity of @p resp.
 * @param[out] resp_len Number of bytes written on success.
 * @param[out] nrc_out  NRC byte when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_core_sec_send_key(UdsCoreSecurity *sec, uint8_t sub_fn,
                          const uint8_t *key, size_t key_len,
                          uint8_t *resp, size_t resp_size, size_t *resp_len,
                          uint8_t *nrc_out);

/**
 * @brief Return true if the specified security level is currently unlocked.
 *
 * @param[in] sec   Security state (may be NULL — returns false).
 * @param[in] level Odd sub-function value identifying the level (0x01, …).
 * @return true if unlocked, false otherwise.
 */
bool uds_core_sec_is_unlocked(const UdsCoreSecurity *sec, uint8_t level);

/**
 * @brief Reset security state (e.g., on session change).
 *
 * Clears seed, pending level, fail count, lock state, and unlocked level.
 *
 * @param[out] sec Security state to reset.
 */
void uds_core_sec_reset(UdsCoreSecurity *sec);

/**
 * @brief Compute the expected key for a given seed using the XOR algorithm.
 *
 * Algorithm: key[i] = seed[i] XOR mask[i % 4]
 * where mask = {0xAB, 0xCD, 0x12, 0x34}  (i.e., 0xABCD1234 big-endian).
 *
 * This function is public so testers can compute expected keys independently.
 *
 * @param[in]  seed     Seed bytes.
 * @param[in]  seed_len Number of seed bytes.
 * @param[out] key_out  Output buffer for computed key (must be ≥ seed_len).
 */
void uds_core_sec_compute_key(const uint8_t *seed, size_t seed_len,
                               uint8_t *key_out);

/* ── Utility ────────────────────────────────────────────────────────────── */

/**
 * @brief Return a human-readable string for a UdsCoreError code.
 *
 * @param err Error code.
 * @return Pointer to a static string; never NULL.
 */
const char *uds_core_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CORE_H */
