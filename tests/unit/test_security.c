/**
 * @file test_security.c
 * @brief Unit tests for uds_core Security Access — Service 0x27.
 *
 * Tests are pure-logic (no CAN I/O, no kernel dependency).
 * Timestamps in UdsCoreSecurity are manipulated directly to simulate
 * time-dependent conditions (seed expiry, lockout) without delays.
 *
 * Build & run:
 *   cmake -B build && cmake --build build && cd build && ctest -V
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "uds_core.h"
#include "uds_nrc.h"

/* ── Helpers ────────────────────────────────────────────────────────────── */

static uint8_t resp[64];
static size_t  resp_len;
static uint8_t nrc;

/** Return a fresh, zeroed security state. */
static UdsCoreSecurity make_sec(void) {
  UdsCoreSecurity sec;
  uds_core_security_init(&sec);
  return sec;
}

/**
 * Request a seed for sub_fn (odd), then extract the 4 seed bytes from resp[]
 * and compute the correct key, and return via key_out[].
 * Returns UDS_CORE_OK on success.
 */
static int do_request_seed(UdsCoreSecurity *sec, uint8_t sub_fn,
                            uint8_t *key_out) {
  int rc = uds_core_sec_request_seed(sec, sub_fn, resp, sizeof(resp),
                                     &resp_len, &nrc);
  if (rc != UDS_CORE_OK) {
    return rc;
  }
  /* Seed is in resp[2..5] */
  uds_core_sec_compute_key(&resp[2], UDS_CORE_SEED_LEN, key_out);
  return UDS_CORE_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 1: Init
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_sec_init_zeroed) {
  UdsCoreSecurity sec = make_sec();
  ck_assert(!sec.seed_valid);
  ck_assert(!sec.locked);
  ck_assert_uint_eq(sec.fail_count, 0U);
  ck_assert_uint_eq(sec.unlocked_level, 0U);
  ck_assert_uint_eq(sec.pending_level, 0U);
}
END_TEST

START_TEST(test_sec_init_null_noop) {
  /* Must not crash */
  uds_core_security_init(NULL);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 2: Key computation
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_compute_key_xor) {
  /* Key = Seed XOR {0xAB, 0xCD, 0x12, 0x34} */
  uint8_t seed[4] = {0x00U, 0x00U, 0x00U, 0x00U};
  uint8_t key[4];
  uds_core_sec_compute_key(seed, 4U, key);
  ck_assert_uint_eq(key[0], 0xABU);
  ck_assert_uint_eq(key[1], 0xCDU);
  ck_assert_uint_eq(key[2], 0x12U);
  ck_assert_uint_eq(key[3], 0x34U);
}
END_TEST

START_TEST(test_compute_key_roundtrip) {
  /* XOR is self-inverse: key(key(seed)) == seed */
  uint8_t seed[4] = {0xA1U, 0xB2U, 0xC3U, 0xD4U};
  uint8_t key[4];
  uint8_t back[4];
  uds_core_sec_compute_key(seed, 4U, key);
  uds_core_sec_compute_key(key, 4U, back);
  ck_assert_int_eq(memcmp(seed, back, 4U), 0);
}
END_TEST

START_TEST(test_compute_key_known_value) {
  /* Seed = {0x12, 0x34, 0xAB, 0xCD}, Mask = {0xAB, 0xCD, 0x12, 0x34}
   * Key  = {0x12^0xAB, 0x34^0xCD, 0xAB^0x12, 0xCD^0x34}
   *      = {0xB9,      0xF9,      0xB9,       0xF9} */
  uint8_t seed[4] = {0x12U, 0x34U, 0xABU, 0xCDU};
  uint8_t key[4];
  uds_core_sec_compute_key(seed, 4U, key);
  ck_assert_uint_eq(key[0], 0xB9U);
  ck_assert_uint_eq(key[1], 0xF9U);
  ck_assert_uint_eq(key[2], 0xB9U);
  ck_assert_uint_eq(key[3], 0xF9U);
}
END_TEST

START_TEST(test_compute_key_null_noop) {
  uint8_t out[4] = {0};
  uds_core_sec_compute_key(NULL, 4U, out);  /* must not crash */
  uds_core_sec_compute_key((uint8_t *)"\x01", 4U, NULL); /* must not crash */
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 3: requestSeed (0x27 odd sub-function)
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_seed_level1_response_format) {
  UdsCoreSecurity sec = make_sec();
  int rc = uds_core_sec_request_seed(&sec, 0x01U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U + UDS_CORE_SEED_LEN);
  ck_assert_uint_eq(resp[0], 0x67U);
  ck_assert_uint_eq(resp[1], 0x01U);
}
END_TEST

START_TEST(test_seed_level2_response_format) {
  UdsCoreSecurity sec = make_sec();
  int rc = uds_core_sec_request_seed(&sec, 0x03U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp[0], 0x67U);
  ck_assert_uint_eq(resp[1], 0x03U);
}
END_TEST

START_TEST(test_seed_stores_pending_level) {
  UdsCoreSecurity sec = make_sec();
  uds_core_sec_request_seed(&sec, 0x05U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_uint_eq(sec.pending_level, 0x05U);
  ck_assert(sec.seed_valid);
}
END_TEST

START_TEST(test_seed_even_sub_fn_rejected) {
  UdsCoreSecurity sec = make_sec();
  int rc = uds_core_sec_request_seed(&sec, 0x02U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_seed_zero_sub_fn_rejected) {
  UdsCoreSecurity sec = make_sec();
  int rc = uds_core_sec_request_seed(&sec, 0x00U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_seed_null_sec) {
  int rc = uds_core_sec_request_seed(NULL, 0x01U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_seed_null_resp) {
  UdsCoreSecurity sec = make_sec();
  int rc = uds_core_sec_request_seed(&sec, 0x01U, NULL, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_seed_buf_too_small) {
  UdsCoreSecurity sec = make_sec();
  uint8_t small[4]; /* need at least 2 + SEED_LEN = 6 */
  int rc = uds_core_sec_request_seed(&sec, 0x01U, small, sizeof(small),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

START_TEST(test_seed_when_locked_returns_nrc36) {
  UdsCoreSecurity sec = make_sec();
  sec.locked = true;
  clock_gettime(CLOCK_MONOTONIC, &sec.locked_ts); /* lock started now */

  int rc = uds_core_sec_request_seed(&sec, 0x01U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS);
}
END_TEST

START_TEST(test_seed_when_already_unlocked_returns_zeros) {
  UdsCoreSecurity sec = make_sec();
  sec.unlocked_level = 0x01U; /* pretend level 1 is already unlocked */

  int rc = uds_core_sec_request_seed(&sec, 0x01U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U + UDS_CORE_SEED_LEN);
  /* All seed bytes should be 0x00 */
  for (size_t i = 2U; i < resp_len; i++) {
    ck_assert_uint_eq(resp[i], 0x00U);
  }
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 4: sendKey (0x27 even sub-function)
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_key_correct_unlocks_level) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key[UDS_CORE_SEED_LEN];

  int rc = do_request_seed(&sec, 0x01U, key);
  ck_assert_int_eq(rc, UDS_CORE_OK);

  rc = uds_core_sec_send_key(&sec, 0x02U, key, sizeof(key),
                              resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U);
  ck_assert_uint_eq(resp[0], 0x67U);
  ck_assert_uint_eq(resp[1], 0x02U);
  ck_assert(uds_core_sec_is_unlocked(&sec, 0x01U));
}
END_TEST

START_TEST(test_key_wrong_increments_fail_count) {
  UdsCoreSecurity sec = make_sec();
  uint8_t bad_key[UDS_CORE_SEED_LEN] = {0xDEU, 0xADU, 0xBEU, 0xEFU};

  uds_core_sec_request_seed(&sec, 0x01U, resp, sizeof(resp), &resp_len, &nrc);

  int rc = uds_core_sec_send_key(&sec, 0x02U, bad_key, sizeof(bad_key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_INVALID_KEY);
  ck_assert_uint_eq(sec.fail_count, 1U);
}
END_TEST

START_TEST(test_key_three_failures_trigger_lockout) {
  UdsCoreSecurity sec = make_sec();
  uint8_t bad_key[UDS_CORE_SEED_LEN] = {0xFFU, 0xFFU, 0xFFU, 0xFFU};

  for (unsigned i = 0U; i < UDS_CORE_MAX_ATTEMPTS; i++) {
    /* Re-request seed before each attempt (previous was invalidated) */
    int src = uds_core_sec_request_seed(&sec, 0x01U, resp, sizeof(resp),
                                        &resp_len, &nrc);
    ck_assert_int_eq(src, UDS_CORE_OK);

    uds_core_sec_send_key(&sec, 0x02U, bad_key, sizeof(bad_key),
                           resp, sizeof(resp), &resp_len, &nrc);
  }

  /* After 3 failures, ECU must be locked */
  ck_assert(sec.locked);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS);
}
END_TEST

START_TEST(test_key_locked_returns_nrc36) {
  UdsCoreSecurity sec = make_sec();
  sec.locked = true;
  clock_gettime(CLOCK_MONOTONIC, &sec.locked_ts);

  uint8_t key[UDS_CORE_SEED_LEN] = {0};
  int rc = uds_core_sec_send_key(&sec, 0x02U, key, sizeof(key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS);
}
END_TEST

START_TEST(test_key_without_prior_seed_returns_nrc24) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key[UDS_CORE_SEED_LEN] = {0};

  int rc = uds_core_sec_send_key(&sec, 0x02U, key, sizeof(key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_SEQUENCE_ERROR);
}
END_TEST

START_TEST(test_key_expired_seed_returns_nrc37) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key[UDS_CORE_SEED_LEN];
  do_request_seed(&sec, 0x01U, key);

  /* Backdate seed timestamp to simulate expiry */
  sec.seed_ts.tv_sec -= (long)(UDS_CORE_SEED_TIMEOUT_SECS + 1U);

  int rc = uds_core_sec_send_key(&sec, 0x02U, key, sizeof(key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED);
}
END_TEST

START_TEST(test_key_wrong_level_returns_nrc24) {
  UdsCoreSecurity sec = make_sec();
  /* Request seed for level 1 (sub_fn 0x01) but send key for level 2 (0x04) */
  do_request_seed(&sec, 0x01U, NULL);

  uint8_t key[UDS_CORE_SEED_LEN] = {0};
  int rc = uds_core_sec_send_key(&sec, 0x04U, key, sizeof(key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_SEQUENCE_ERROR);
}
END_TEST

START_TEST(test_key_odd_sub_fn_rejected) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key[UDS_CORE_SEED_LEN] = {0};
  int rc = uds_core_sec_send_key(&sec, 0x01U, key, sizeof(key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_key_wrong_length_returns_nrc13) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key_buf[UDS_CORE_SEED_LEN];
  do_request_seed(&sec, 0x01U, key_buf);

  /* Send a key of length 3 instead of 4 */
  int rc = uds_core_sec_send_key(&sec, 0x02U, key_buf, 3U,
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
}
END_TEST

START_TEST(test_key_null_sec) {
  uint8_t key[UDS_CORE_SEED_LEN] = {0};
  int rc = uds_core_sec_send_key(NULL, 0x02U, key, sizeof(key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_key_null_key_data) {
  UdsCoreSecurity sec = make_sec();
  int rc = uds_core_sec_send_key(&sec, 0x02U, NULL, UDS_CORE_SEED_LEN,
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_key_buf_too_small) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key[UDS_CORE_SEED_LEN];
  do_request_seed(&sec, 0x01U, key);

  uint8_t small[1];
  int rc = uds_core_sec_send_key(&sec, 0x02U, key, sizeof(key),
                                  small, sizeof(small), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

START_TEST(test_key_correct_resets_fail_count) {
  UdsCoreSecurity sec = make_sec();
  uint8_t bad_key[UDS_CORE_SEED_LEN] = {0xFFU, 0xFFU, 0xFFU, 0xFFU};

  /* One failed attempt */
  uds_core_sec_request_seed(&sec, 0x01U, resp, sizeof(resp), &resp_len, &nrc);
  uds_core_sec_send_key(&sec, 0x02U, bad_key, sizeof(bad_key),
                         resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_uint_eq(sec.fail_count, 1U);

  /* Now succeed */
  uint8_t good_key[UDS_CORE_SEED_LEN];
  do_request_seed(&sec, 0x01U, good_key);
  int rc = uds_core_sec_send_key(&sec, 0x02U, good_key, sizeof(good_key),
                                  resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(sec.fail_count, 0U);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 5: is_unlocked / reset
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_is_unlocked_initially_false) {
  UdsCoreSecurity sec = make_sec();
  ck_assert(!uds_core_sec_is_unlocked(&sec, 0x01U));
}
END_TEST

START_TEST(test_is_unlocked_after_correct_key) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key[UDS_CORE_SEED_LEN];
  do_request_seed(&sec, 0x01U, key);
  uds_core_sec_send_key(&sec, 0x02U, key, sizeof(key),
                         resp, sizeof(resp), &resp_len, &nrc);
  ck_assert(uds_core_sec_is_unlocked(&sec, 0x01U));
  ck_assert(!uds_core_sec_is_unlocked(&sec, 0x03U)); /* different level */
}
END_TEST

START_TEST(test_is_unlocked_null_returns_false) {
  ck_assert(!uds_core_sec_is_unlocked(NULL, 0x01U));
}
END_TEST

START_TEST(test_is_unlocked_level_zero_false) {
  UdsCoreSecurity sec = make_sec();
  ck_assert(!uds_core_sec_is_unlocked(&sec, 0x00U));
}
END_TEST

START_TEST(test_sec_reset_clears_state) {
  UdsCoreSecurity sec = make_sec();
  uint8_t key[UDS_CORE_SEED_LEN];
  do_request_seed(&sec, 0x01U, key);
  uds_core_sec_send_key(&sec, 0x02U, key, sizeof(key),
                         resp, sizeof(resp), &resp_len, &nrc);
  ck_assert(uds_core_sec_is_unlocked(&sec, 0x01U));

  uds_core_sec_reset(&sec);

  ck_assert(!uds_core_sec_is_unlocked(&sec, 0x01U));
  ck_assert(!sec.seed_valid);
  ck_assert(!sec.locked);
  ck_assert_uint_eq(sec.fail_count, 0U);
}
END_TEST

START_TEST(test_sec_reset_null_noop) {
  uds_core_sec_reset(NULL); /* must not crash */
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 6: Lockout auto-expiry
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_lockout_auto_clears_after_timeout) {
  UdsCoreSecurity sec = make_sec();
  sec.locked = true;
  clock_gettime(CLOCK_MONOTONIC, &sec.locked_ts);
  /* Backdate lock timestamp to simulate expiry */
  sec.locked_ts.tv_sec -= (long)(UDS_CORE_LOCKOUT_SECS + 1U);

  /* requestSeed should succeed now (lock has expired) */
  int rc = uds_core_sec_request_seed(&sec, 0x01U, resp, sizeof(resp),
                                     &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert(!sec.locked);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Test suite / runner wiring
 * ════════════════════════════════════════════════════════════════════════════
 */

static Suite *security_suite(void) {
  Suite *s = suite_create("uds_core_security");

  /* ── Init ──────────────────────────────────────────────────── */
  TCase *tc_init = tcase_create("init");
  tcase_add_test(tc_init, test_sec_init_zeroed);
  tcase_add_test(tc_init, test_sec_init_null_noop);
  suite_add_tcase(s, tc_init);

  /* ── Key computation ───────────────────────────────────────── */
  TCase *tc_key_comp = tcase_create("key_computation");
  tcase_add_test(tc_key_comp, test_compute_key_xor);
  tcase_add_test(tc_key_comp, test_compute_key_roundtrip);
  tcase_add_test(tc_key_comp, test_compute_key_known_value);
  tcase_add_test(tc_key_comp, test_compute_key_null_noop);
  suite_add_tcase(s, tc_key_comp);

  /* ── requestSeed ───────────────────────────────────────────── */
  TCase *tc_seed = tcase_create("request_seed");
  tcase_add_test(tc_seed, test_seed_level1_response_format);
  tcase_add_test(tc_seed, test_seed_level2_response_format);
  tcase_add_test(tc_seed, test_seed_stores_pending_level);
  tcase_add_test(tc_seed, test_seed_even_sub_fn_rejected);
  tcase_add_test(tc_seed, test_seed_zero_sub_fn_rejected);
  tcase_add_test(tc_seed, test_seed_null_sec);
  tcase_add_test(tc_seed, test_seed_null_resp);
  tcase_add_test(tc_seed, test_seed_buf_too_small);
  tcase_add_test(tc_seed, test_seed_when_locked_returns_nrc36);
  tcase_add_test(tc_seed, test_seed_when_already_unlocked_returns_zeros);
  suite_add_tcase(s, tc_seed);

  /* ── sendKey ───────────────────────────────────────────────── */
  TCase *tc_key = tcase_create("send_key");
  tcase_add_test(tc_key, test_key_correct_unlocks_level);
  tcase_add_test(tc_key, test_key_wrong_increments_fail_count);
  tcase_add_test(tc_key, test_key_three_failures_trigger_lockout);
  tcase_add_test(tc_key, test_key_locked_returns_nrc36);
  tcase_add_test(tc_key, test_key_without_prior_seed_returns_nrc24);
  tcase_add_test(tc_key, test_key_expired_seed_returns_nrc37);
  tcase_add_test(tc_key, test_key_wrong_level_returns_nrc24);
  tcase_add_test(tc_key, test_key_odd_sub_fn_rejected);
  tcase_add_test(tc_key, test_key_wrong_length_returns_nrc13);
  tcase_add_test(tc_key, test_key_null_sec);
  tcase_add_test(tc_key, test_key_null_key_data);
  tcase_add_test(tc_key, test_key_buf_too_small);
  tcase_add_test(tc_key, test_key_correct_resets_fail_count);
  suite_add_tcase(s, tc_key);

  /* ── is_unlocked / reset ───────────────────────────────────── */
  TCase *tc_unlock = tcase_create("unlock_reset");
  tcase_add_test(tc_unlock, test_is_unlocked_initially_false);
  tcase_add_test(tc_unlock, test_is_unlocked_after_correct_key);
  tcase_add_test(tc_unlock, test_is_unlocked_null_returns_false);
  tcase_add_test(tc_unlock, test_is_unlocked_level_zero_false);
  tcase_add_test(tc_unlock, test_sec_reset_clears_state);
  tcase_add_test(tc_unlock, test_sec_reset_null_noop);
  suite_add_tcase(s, tc_unlock);

  /* ── Lockout auto-expiry ───────────────────────────────────── */
  TCase *tc_lock = tcase_create("lockout");
  tcase_add_test(tc_lock, test_lockout_auto_clears_after_timeout);
  suite_add_tcase(s, tc_lock);

  return s;
}

int main(void) {
  Suite *s = security_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
