/**
 * @file test_session.c
 * @brief Unit tests for uds_core session management (Service 0x10 and 0x3E).
 *
 * Tests are pure-logic (no CAN I/O, no kernel dependency).
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

/** Allocate and initialise a fresh default session for ECU 1. */
static UdsCoreSession make_session(void) {
  UdsCoreSession sess;
  uds_core_session_init(&sess, 1U, NULL);
  return sess;
}

/** Response and length buffers reused across tests. */
static uint8_t resp[64];
static size_t  resp_len;
static uint8_t nrc;

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 1: Session initialisation
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_session_init_defaults) {
  UdsCoreSession sess = make_session();
  ck_assert_uint_eq((unsigned)sess.type, (unsigned)UDS_SESSION_DEFAULT);
  ck_assert_uint_eq(sess.ecu_id, 1U);
  ck_assert_uint_eq(sess.security_level, 0U);
  ck_assert_uint_eq(sess.cfg.p2_ms, 50U);
  ck_assert_uint_eq(sess.cfg.p2_star_ms, 2000U);
  ck_assert_uint_eq(sess.cfg.s3_ms, 10000U);
}
END_TEST

START_TEST(test_session_init_custom_config) {
  UdsCoreSessionConfig cfg = {.p2_ms = 100U, .p2_star_ms = 5000U,
                               .s3_ms = 30000U};
  UdsCoreSession sess;
  uds_core_session_init(&sess, 5U, &cfg);
  ck_assert_uint_eq(sess.cfg.p2_ms, 100U);
  ck_assert_uint_eq(sess.cfg.p2_star_ms, 5000U);
  ck_assert_uint_eq(sess.cfg.s3_ms, 30000U);
  ck_assert_uint_eq(sess.ecu_id, 5U);
}
END_TEST

START_TEST(test_session_init_null_noop) {
  /* Must not crash */
  uds_core_session_init(NULL, 1U, NULL);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 2: Service 0x10 — Diagnostic Session Control
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_dsc_to_default) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x01U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 6U);
  ck_assert_uint_eq(resp[0], 0x50U);
  ck_assert_uint_eq(resp[1], 0x01U);
  ck_assert_uint_eq((unsigned)sess.type, (unsigned)UDS_SESSION_DEFAULT);
}
END_TEST

START_TEST(test_dsc_to_programming) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x02U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 6U);
  ck_assert_uint_eq(resp[0], 0x50U);
  ck_assert_uint_eq(resp[1], 0x02U);
  ck_assert_uint_eq((unsigned)sess.type, (unsigned)UDS_SESSION_PROGRAMMING);
}
END_TEST

START_TEST(test_dsc_to_extended) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x03U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp[1], 0x03U);
  ck_assert_uint_eq((unsigned)sess.type, (unsigned)UDS_SESSION_EXTENDED);
}
END_TEST

START_TEST(test_dsc_to_safety) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x04U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp[1], 0x04U);
  ck_assert_uint_eq((unsigned)sess.type, (unsigned)UDS_SESSION_SAFETY);
}
END_TEST

START_TEST(test_dsc_invalid_session_type) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0xFFU, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_dsc_invalid_session_type_zero) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x00U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_dsc_null_session) {
  int rc = uds_core_dsc(NULL, 0x01U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_dsc_null_resp) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x01U, NULL, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_dsc_null_resp_len) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x01U, resp, sizeof(resp), NULL, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_dsc_null_nrc) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_dsc(&sess, 0x01U, resp, sizeof(resp), &resp_len, NULL);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_dsc_buf_too_small) {
  UdsCoreSession sess = make_session();
  uint8_t small[4];
  int rc = uds_core_dsc(&sess, 0x01U, small, sizeof(small), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

START_TEST(test_dsc_response_contains_p2_values) {
  UdsCoreSessionConfig cfg = {.p2_ms = 0x0064U /* 100ms */,
                               .p2_star_ms = 0x07D0U /* 2000ms */,
                               .s3_ms = 10000U};
  UdsCoreSession sess;
  uds_core_session_init(&sess, 1U, &cfg);

  int rc = uds_core_dsc(&sess, 0x03U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  /* P2 big-endian: 0x00 0x64 */
  ck_assert_uint_eq(resp[2], 0x00U);
  ck_assert_uint_eq(resp[3], 0x64U);
  /* P2* big-endian: 0x07 0xD0 */
  ck_assert_uint_eq(resp[4], 0x07U);
  ck_assert_uint_eq(resp[5], 0xD0U);
}
END_TEST

START_TEST(test_dsc_resets_security_on_default) {
  UdsCoreSession sess = make_session();
  /* Simulate a previous security unlock */
  sess.security_level = 0x01U;

  /* Switch to programming then back to default */
  uds_core_dsc(&sess, 0x02U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_uint_eq(sess.security_level, 0x01U); /* not reset by programming */

  uds_core_dsc(&sess, 0x01U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_uint_eq(sess.security_level, 0U); /* reset on return to default */
}
END_TEST

START_TEST(test_dsc_does_not_reset_security_on_extended) {
  UdsCoreSession sess = make_session();
  sess.security_level = 0x01U;

  uds_core_dsc(&sess, 0x03U, resp, sizeof(resp), &resp_len, &nrc);
  /* security_level should be preserved when switching to non-default session */
  ck_assert_uint_eq(sess.security_level, 0x01U);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 3: Service 0x3E — Tester Present
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_tp_sub_fn_zero_positive_response) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_tester_present(&sess, 0x00U, resp, sizeof(resp),
                                   &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U);
  ck_assert_uint_eq(resp[0], 0x7EU);
  ck_assert_uint_eq(resp[1], 0x00U);
}
END_TEST

START_TEST(test_tp_suppress_bit_set) {
  UdsCoreSession sess = make_session();
  /* sub_fn = 0x80 → suppressPosRspMsgIndicationBit set, actual sub_fn = 0x00 */
  int rc = uds_core_tester_present(&sess, 0x80U, resp, sizeof(resp),
                                   &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 0U); /* no bytes to send */
}
END_TEST

START_TEST(test_tp_invalid_sub_fn) {
  UdsCoreSession sess = make_session();
  /* 0x3E only supports sub-function 0x00 */
  int rc = uds_core_tester_present(&sess, 0x01U, resp, sizeof(resp),
                                   &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_tp_null_session) {
  int rc = uds_core_tester_present(NULL, 0x00U, resp, sizeof(resp),
                                   &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_tp_null_resp) {
  UdsCoreSession sess = make_session();
  int rc = uds_core_tester_present(&sess, 0x00U, NULL, sizeof(resp),
                                   &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_tp_buf_too_small) {
  UdsCoreSession sess = make_session();
  uint8_t small[1];
  int rc = uds_core_tester_present(&sess, 0x00U, small, sizeof(small),
                                   &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

START_TEST(test_tp_refreshes_last_activity) {
  UdsCoreSession sess = make_session();
  struct timespec before = sess.last_activity;

  /* Small sleep to ensure time advances */
  struct timespec ts = {0, 5000000L}; /* 5 ms */
  nanosleep(&ts, NULL);

  uds_core_tester_present(&sess, 0x00U, resp, sizeof(resp), &resp_len, &nrc);

  /* last_activity should be updated (after 'before') */
  long diff_ns = (long)(sess.last_activity.tv_sec - before.tv_sec) *
                     1000000000L +
                 (long)(sess.last_activity.tv_nsec - before.tv_nsec);
  ck_assert(diff_ns >= 0);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 4: Session expiry
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_session_not_expired_fresh) {
  UdsCoreSession sess = make_session();
  ck_assert(!uds_core_session_expired(&sess));
}
END_TEST

START_TEST(test_session_expired_when_old) {
  UdsCoreSession sess = make_session();
  /* Manually backdate last_activity by more than s3_ms (10 s default) */
  sess.last_activity.tv_sec -= 15;
  ck_assert(uds_core_session_expired(&sess));
}
END_TEST

START_TEST(test_session_not_expired_just_before_timeout) {
  UdsCoreSession sess = make_session();
  /* Backdate by 9 seconds — should not yet be expired (s3 = 10 s) */
  sess.last_activity.tv_sec -= 9;
  ck_assert(!uds_core_session_expired(&sess));
}
END_TEST

START_TEST(test_session_expired_null) {
  ck_assert(uds_core_session_expired(NULL));
}
END_TEST

START_TEST(test_session_refresh_updates_timestamp) {
  UdsCoreSession sess = make_session();
  /* Backdate to simulate elapsed time */
  sess.last_activity.tv_sec -= 20;
  ck_assert(uds_core_session_expired(&sess));

  uds_core_session_refresh(&sess);
  ck_assert(!uds_core_session_expired(&sess));
}
END_TEST

START_TEST(test_session_refresh_null_noop) {
  /* Must not crash */
  uds_core_session_refresh(NULL);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 5: Utility
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_strerror_ok) {
  const char *s = uds_core_strerror(UDS_CORE_OK);
  ck_assert_ptr_nonnull(s);
}
END_TEST

START_TEST(test_strerror_nrc) {
  const char *s = uds_core_strerror(UDS_CORE_ERR_NRC);
  ck_assert_ptr_nonnull(s);
}
END_TEST

START_TEST(test_strerror_unknown) {
  const char *s = uds_core_strerror(-999);
  ck_assert_ptr_nonnull(s);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Test suite / runner wiring
 * ════════════════════════════════════════════════════════════════════════════
 */

static Suite *session_suite(void) {
  Suite *s = suite_create("uds_core_session");

  /* ── Session init ──────────────────────────────────────────── */
  TCase *tc_init = tcase_create("init");
  tcase_add_test(tc_init, test_session_init_defaults);
  tcase_add_test(tc_init, test_session_init_custom_config);
  tcase_add_test(tc_init, test_session_init_null_noop);
  suite_add_tcase(s, tc_init);

  /* ── DSC (0x10) ────────────────────────────────────────────── */
  TCase *tc_dsc = tcase_create("dsc");
  tcase_add_test(tc_dsc, test_dsc_to_default);
  tcase_add_test(tc_dsc, test_dsc_to_programming);
  tcase_add_test(tc_dsc, test_dsc_to_extended);
  tcase_add_test(tc_dsc, test_dsc_to_safety);
  tcase_add_test(tc_dsc, test_dsc_invalid_session_type);
  tcase_add_test(tc_dsc, test_dsc_invalid_session_type_zero);
  tcase_add_test(tc_dsc, test_dsc_null_session);
  tcase_add_test(tc_dsc, test_dsc_null_resp);
  tcase_add_test(tc_dsc, test_dsc_null_resp_len);
  tcase_add_test(tc_dsc, test_dsc_null_nrc);
  tcase_add_test(tc_dsc, test_dsc_buf_too_small);
  tcase_add_test(tc_dsc, test_dsc_response_contains_p2_values);
  tcase_add_test(tc_dsc, test_dsc_resets_security_on_default);
  tcase_add_test(tc_dsc, test_dsc_does_not_reset_security_on_extended);
  suite_add_tcase(s, tc_dsc);

  /* ── Tester Present (0x3E) ─────────────────────────────────── */
  TCase *tc_tp = tcase_create("tester_present");
  tcase_add_test(tc_tp, test_tp_sub_fn_zero_positive_response);
  tcase_add_test(tc_tp, test_tp_suppress_bit_set);
  tcase_add_test(tc_tp, test_tp_invalid_sub_fn);
  tcase_add_test(tc_tp, test_tp_null_session);
  tcase_add_test(tc_tp, test_tp_null_resp);
  tcase_add_test(tc_tp, test_tp_buf_too_small);
  tcase_add_test(tc_tp, test_tp_refreshes_last_activity);
  suite_add_tcase(s, tc_tp);

  /* ── Session expiry ────────────────────────────────────────── */
  TCase *tc_expiry = tcase_create("expiry");
  tcase_add_test(tc_expiry, test_session_not_expired_fresh);
  tcase_add_test(tc_expiry, test_session_expired_when_old);
  tcase_add_test(tc_expiry, test_session_not_expired_just_before_timeout);
  tcase_add_test(tc_expiry, test_session_expired_null);
  tcase_add_test(tc_expiry, test_session_refresh_updates_timestamp);
  tcase_add_test(tc_expiry, test_session_refresh_null_noop);
  suite_add_tcase(s, tc_expiry);

  /* ── Utility ───────────────────────────────────────────────── */
  TCase *tc_util = tcase_create("utility");
  tcase_add_test(tc_util, test_strerror_ok);
  tcase_add_test(tc_util, test_strerror_nrc);
  tcase_add_test(tc_util, test_strerror_unknown);
  suite_add_tcase(s, tc_util);

  return s;
}

int main(void) {
  Suite *s = session_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
