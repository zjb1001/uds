/**
 * @file security.c
 * @brief UDS Security Access — Service 0x27.
 *
 * Implements the seed/key challenge-response authentication per ISO 14229-1.
 *
 * Key algorithm: key[i] = seed[i] XOR mask[i % 4]
 * where mask = {0xAB, 0xCD, 0x12, 0x34}  (XOR with 0xABCD1234, big-endian).
 *
 * Lockout policy:
 *   - 3 failed key attempts → lock for UDS_CORE_LOCKOUT_SECS (300 s).
 *   - Seed is invalidated after each failed attempt.
 *   - Lockout status is checked on both requestSeed and sendKey.
 *
 * Seed validity: 10 seconds from generation (UDS_CORE_SEED_TIMEOUT_SECS).
 *
 * Design notes:
 * - No dynamic memory; all state is in the caller-supplied UdsCoreSecurity.
 * - Seed generation uses rand(); callers may srand() before first use.
 * - Key comparison is constant-time (XOR all bytes) to resist timing attacks.
 * - Seed and lockout timestamps are in UdsCoreSecurity.seed_ts / locked_ts
 *   (public) so tests can manipulate them without a separate injection API.
 */

#define _POSIX_C_SOURCE 200809L

#include "uds_core.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── XOR key mask (big-endian bytes of 0xABCD1234) ─────────────────────── */

static const uint8_t KEY_MASK[UDS_CORE_SEED_LEN] = {0xABU, 0xCDU, 0x12U,
                                                      0x34U};

/* ── Internal helpers ───────────────────────────────────────────────────── */

/**
 * Return true if the security lockout is currently active.
 * As a side-effect, if the lockout period has elapsed it is cleared.
 */
static bool check_locked(UdsCoreSecurity *sec) {
  if (!sec->locked) {
    return false;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long elapsed = (long)(now.tv_sec - sec->locked_ts.tv_sec);
  if (elapsed >= (long)UDS_CORE_LOCKOUT_SECS) {
    /* Lockout period has expired — auto-clear */
    sec->locked = false;
    sec->fail_count = 0U;
    return false;
  }

  return true;
}

/**
 * Return true if the previously issued seed has expired (> SEED_TIMEOUT_SECS).
 */
static bool seed_expired(const UdsCoreSecurity *sec) {
  if (!sec->seed_valid) {
    return true;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long elapsed = (long)(now.tv_sec - sec->seed_ts.tv_sec);
  return elapsed >= (long)UDS_CORE_SEED_TIMEOUT_SECS;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void uds_core_security_init(UdsCoreSecurity *sec) {
  if (!sec) {
    return;
  }
  memset(sec, 0, sizeof(*sec));
}

void uds_core_sec_compute_key(const uint8_t *seed, size_t seed_len,
                               uint8_t *key_out) {
  if (!seed || !key_out || seed_len == 0U) {
    return;
  }
  for (size_t i = 0U; i < seed_len; i++) {
    key_out[i] = seed[i] ^ KEY_MASK[i % UDS_CORE_SEED_LEN];
  }
}

/* ── Service 0x27 requestSeed (odd sub-function) ───────────────────────── */

int uds_core_sec_request_seed(UdsCoreSecurity *sec, uint8_t sub_fn,
                               uint8_t *resp, size_t resp_size,
                               size_t *resp_len, uint8_t *nrc_out) {
  if (!sec || !resp || !resp_len || !nrc_out) {
    return UDS_CORE_ERR_PARAM;
  }

  /* sub_fn must be odd and non-zero (0x01, 0x03, 0x05, …) */
  if (sub_fn == 0U || (sub_fn & 0x01U) == 0U) {
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }

  /* Minimum response size: [0x67, sub_fn, seed...] */
  if (resp_size < 2U + UDS_CORE_SEED_LEN) {
    return UDS_CORE_ERR_BUF;
  }

  /* Check for active lockout */
  if (check_locked(sec)) {
    *nrc_out = (uint8_t)UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS;
    return UDS_CORE_ERR_NRC;
  }

  /* If already unlocked at this level, return all-zeros seed per ISO 14229-1 */
  if (sec->unlocked_level == sub_fn) {
    resp[0] = 0x67U;
    resp[1] = sub_fn;
    memset(&resp[2], 0x00U, UDS_CORE_SEED_LEN);
    *resp_len = 2U + UDS_CORE_SEED_LEN;
    return UDS_CORE_OK;
  }

  /* Generate fresh random seed */
  for (size_t i = 0U; i < UDS_CORE_SEED_LEN; i++) {
    sec->seed[i] = (uint8_t)rand();
  }
  sec->seed_valid = true;
  sec->pending_level = sub_fn;
  clock_gettime(CLOCK_MONOTONIC, &sec->seed_ts);

  resp[0] = 0x67U;
  resp[1] = sub_fn;
  memcpy(&resp[2], sec->seed, UDS_CORE_SEED_LEN);
  *resp_len = 2U + UDS_CORE_SEED_LEN;

  return UDS_CORE_OK;
}

/* ── Service 0x27 sendKey (even sub-function) ───────────────────────────── */

int uds_core_sec_send_key(UdsCoreSecurity *sec, uint8_t sub_fn,
                          const uint8_t *key, size_t key_len,
                          uint8_t *resp, size_t resp_size, size_t *resp_len,
                          uint8_t *nrc_out) {
  if (!sec || !key || !resp || !resp_len || !nrc_out || key_len == 0U) {
    return UDS_CORE_ERR_PARAM;
  }

  /* sub_fn must be even and non-zero (0x02, 0x04, 0x06, …) */
  if (sub_fn == 0U || (sub_fn & 0x01U) != 0U) {
    *nrc_out = (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED;
    return UDS_CORE_ERR_NRC;
  }

  /* Minimum response size: [0x67, sub_fn] */
  if (resp_size < 2U) {
    return UDS_CORE_ERR_BUF;
  }

  /* Check for active lockout */
  if (check_locked(sec)) {
    *nrc_out = (uint8_t)UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS;
    return UDS_CORE_ERR_NRC;
  }

  /* The requestSeed sub_fn must be sub_fn - 1 (odd) */
  uint8_t expected_seed_level = sub_fn - 1U;

  /* Verify that a seed was requested for the correct level */
  if (!sec->seed_valid || sec->pending_level != expected_seed_level) {
    *nrc_out = (uint8_t)UDS_NRC_REQUEST_SEQUENCE_ERROR;
    return UDS_CORE_ERR_NRC;
  }

  /* Verify seed has not expired */
  if (seed_expired(sec)) {
    sec->seed_valid = false;
    *nrc_out = (uint8_t)UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED;
    return UDS_CORE_ERR_NRC;
  }

  /* Key length must match seed length */
  if (key_len != UDS_CORE_SEED_LEN) {
    *nrc_out = (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT;
    return UDS_CORE_ERR_NRC;
  }

  /* Compute expected key and compare (constant-time to resist timing attacks) */
  uint8_t expected_key[UDS_CORE_SEED_LEN];
  uds_core_sec_compute_key(sec->seed, UDS_CORE_SEED_LEN, expected_key);

  uint8_t mismatch = 0U;
  for (size_t i = 0U; i < UDS_CORE_SEED_LEN; i++) {
    mismatch |= (key[i] ^ expected_key[i]);
  }

  if (mismatch != 0U) {
    sec->fail_count++;
    sec->seed_valid = false; /* Invalidate seed regardless */

    if (sec->fail_count >= UDS_CORE_MAX_ATTEMPTS) {
      sec->locked = true;
      clock_gettime(CLOCK_MONOTONIC, &sec->locked_ts);
      *nrc_out = (uint8_t)UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS;
    } else {
      *nrc_out = (uint8_t)UDS_NRC_INVALID_KEY;
    }
    return UDS_CORE_ERR_NRC;
  }

  /* Key is correct — unlock this security level */
  sec->seed_valid = false;
  sec->fail_count = 0U;
  sec->unlocked_level = expected_seed_level; /* store the odd requestSeed level */

  resp[0] = 0x67U;
  resp[1] = sub_fn;
  *resp_len = 2U;

  return UDS_CORE_OK;
}

/* ── Level query / reset ────────────────────────────────────────────────── */

bool uds_core_sec_is_unlocked(const UdsCoreSecurity *sec, uint8_t level) {
  if (!sec) {
    return false;
  }
  return sec->unlocked_level == level && level != 0U;
}

void uds_core_sec_reset(UdsCoreSecurity *sec) {
  if (!sec) {
    return;
  }
  memset(sec, 0, sizeof(*sec));
}
