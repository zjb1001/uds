/**
 * @file session.c
 * @brief UDS session management — Service 0x10 and Service 0x3E.
 *
 * Implements Diagnostic Session Control (DSC, 0x10) and Tester Present
 * (0x3E) per ISO 14229-1.
 *
 * Design notes:
 * - All timing uses CLOCK_MONOTONIC for monotonicity guarantees.
 * - No dynamic memory; all state is in the caller-supplied UdsCoreSession.
 * - The UdsCoreSession struct is non-opaque so tests can inspect/modify it.
 */

#define _POSIX_C_SOURCE 200809L

#include "uds_core.h"

#include <string.h>
#include <time.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

/**
 * Compute elapsed time between two timespec values in milliseconds.
 * Returns (a - b) in ms; safe for monotonic clock (a >= b always).
 */
static long timespec_diff_ms(const struct timespec *a,
                             const struct timespec *b) {
  long sec_diff = (long)(a->tv_sec - b->tv_sec);
  long nsec_diff = (long)(a->tv_nsec - b->tv_nsec);
  return sec_diff * 1000L + nsec_diff / 1000000L;
}

/* ── Session lifecycle ──────────────────────────────────────────────────── */

void uds_core_session_init(UdsCoreSession *sess, uint8_t ecu_id,
                           const UdsCoreSessionConfig *cfg) {
  if (!sess) {
    return;
  }

  memset(sess, 0, sizeof(*sess));
  sess->ecu_id = ecu_id;
  sess->type = UDS_SESSION_DEFAULT;
  sess->security_level = 0U;

  if (cfg) {
    sess->cfg = *cfg;
  } else {
    sess->cfg.p2_ms = 50U;
    sess->cfg.p2_star_ms = 2000U;
    sess->cfg.s3_ms = 10000U;
  }

  clock_gettime(CLOCK_MONOTONIC, &sess->last_activity);
}

/* ── Service 0x10: Diagnostic Session Control ───────────────────────────── */

int uds_core_dsc(UdsCoreSession *sess, uint8_t session_type, uint8_t *resp,
                 size_t resp_size, size_t *resp_len, uint8_t *nrc_out) {
  if (!sess || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* Validate session type */
  switch (session_type) {
  case UDS_SESSION_DEFAULT:
  case UDS_SESSION_PROGRAMMING:
  case UDS_SESSION_EXTENDED:
  case UDS_SESSION_SAFETY:
    break;
  default:
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }

  /* Positive response: [0x50, sessionType, P2_hi, P2_lo, P2star_hi, P2star_lo]
   */
  if (resp_size < 6U) {
    return UDS_CORE_ERR_BUF;
  }

  /* Update session state */
  sess->type = (UdsSessionType)session_type;

  /* Return to default session clears security access */
  if (session_type == (uint8_t)UDS_SESSION_DEFAULT) {
    sess->security_level = 0U;
  }

  clock_gettime(CLOCK_MONOTONIC, &sess->last_activity);

  /* Build response */
  resp[0] = 0x50U;
  resp[1] = session_type;
  resp[2] = (uint8_t)(sess->cfg.p2_ms >> 8);
  resp[3] = (uint8_t)(sess->cfg.p2_ms & 0xFFU);
  /* ISO 14229-1: P2* field is in units of 10 ms */
  uint16_t p2_star_10ms = (uint16_t)(sess->cfg.p2_star_ms / 10U);
  resp[4] = (uint8_t)(p2_star_10ms >> 8);
  resp[5] = (uint8_t)(p2_star_10ms & 0xFFU);
  *resp_len = 6U;

  return UDS_CORE_OK;
}

/* ── Service 0x3E: Tester Present ──────────────────────────────────────── */

int uds_core_tester_present(UdsCoreSession *sess, uint8_t sub_fn, uint8_t *resp,
                            size_t resp_size, size_t *resp_len,
                            uint8_t *nrc_out) {
  if (!sess || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  bool suppress = (sub_fn & 0x80U) != 0U;
  uint8_t actual_sub_fn = sub_fn & 0x7FU;

  /* 0x3E only supports sub-function 0x00 */
  if (actual_sub_fn != 0x00U) {
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }

  /* Refresh S3 keep-alive timer regardless of suppress bit */
  clock_gettime(CLOCK_MONOTONIC, &sess->last_activity);

  if (suppress) {
    /* Positive response suppressed — execute but send nothing */
    *resp_len = 0U;
    return UDS_CORE_OK;
  }

  /* Response: [0x7E, sub_fn] */
  if (resp_size < 2U) {
    return UDS_CORE_ERR_BUF;
  }

  resp[0] = 0x7EU;
  resp[1] = actual_sub_fn;
  *resp_len = 2U;

  return UDS_CORE_OK;
}

/* ── Session expiry helpers ─────────────────────────────────────────────── */

bool uds_core_session_expired(const UdsCoreSession *sess) {
  if (!sess) {
    return true;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long elapsed_ms = timespec_diff_ms(&now, &sess->last_activity);
  return elapsed_ms > (long)sess->cfg.s3_ms;
}

void uds_core_session_refresh(UdsCoreSession *sess) {
  if (!sess) {
    return;
  }
  clock_gettime(CLOCK_MONOTONIC, &sess->last_activity);
}

/* ── Error strings ──────────────────────────────────────────────────────── */

const char *uds_core_strerror(int err) {
  switch ((UdsCoreError)err) {
  case UDS_CORE_OK:
    return "Success";
  case UDS_CORE_ERR_PARAM:
    return "Invalid parameter";
  case UDS_CORE_ERR_NRC:
    return "UDS negative response condition";
  case UDS_CORE_ERR_BUF:
    return "Response buffer too small";
  default:
    return "Unknown error";
  }
}
