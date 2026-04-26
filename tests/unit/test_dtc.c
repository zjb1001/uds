/**
 * @file test_dtc.c
 * @brief Unit tests for UDS DTC services (0x14, 0x19) and Routine Control
 *        (0x31).
 *
 * Build & run:
 *   cmake -B build && cmake --build build && cd build && ctest -V
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uds_core.h"
#include "uds_dtc.h"
#include "uds_nrc.h"
#include "uds_routine.h"

/* ── Shared test fixtures ───────────────────────────────────────────────── */

static uint8_t resp[256];
static size_t  resp_len;
static uint8_t nrc;

/**
 * Build a DTC registry with three entries:
 *   [0] 0x010203 status=0x09 (testFailed | confirmedDTC)   group=0x01
 *   [1] 0x010405 status=0x04 (pendingDTC)                  group=0x01
 *   [2] 0x020100 status=0x08 (confirmedDTC)                group=0x02
 */
static void make_dtc_registry(UdsDtcRegistry *reg, UdsDtcEntry *entries) {
  entries[0].dtc_code = 0x010203U;
  entries[0].status = 0x09U;
  memset(entries[0].snapshot_data, 0xAAU, 4U);
  entries[0].snapshot_len = 4U;

  entries[1].dtc_code = 0x010405U;
  entries[1].status = 0x04U;
  memset(entries[1].snapshot_data, 0xBBU, 2U);
  entries[1].snapshot_len = 2U;

  entries[2].dtc_code = 0x020100U;
  entries[2].status = 0x08U;
  entries[2].snapshot_len = 0U;

  uds_dtc_registry_init(reg, entries, 3U);
}

/** Build a routine registry with two entries. */
static void make_routine_registry(UdsRoutineRegistry *reg,
                                   UdsRoutineEntry *entries) {
  /* Routine 0x0200: default session, no security, starts IDLE */
  entries[0].routine_id = 0x0200U;
  entries[0].state = UDS_ROUTINE_IDLE;
  entries[0].result_len = 0U;
  entries[0].min_session = 0x01U;
  entries[0].requires_security = false;

  /* Routine 0x0300: extended session required, with result data ready */
  entries[1].routine_id = 0x0300U;
  entries[1].state = UDS_ROUTINE_RESULT_AVAILABLE;
  entries[1].result_data[0] = 0xCAU;
  entries[1].result_data[1] = 0xFEU;
  entries[1].result_len = 2U;
  entries[1].min_session = 0x03U;
  entries[1].requires_security = false;

  uds_routine_registry_init(reg, entries, 2U);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 1: DTC registry initialisation
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_dtc_registry_init_basic) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);
  ck_assert_ptr_eq(reg.entries, entries);
  ck_assert_uint_eq(reg.count, 3U);
}
END_TEST

START_TEST(test_dtc_registry_init_null_noop) {
  uds_dtc_registry_init(NULL, NULL, 0U);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 2: Service 0x14 — ClearDiagnosticInformation
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_clear_dtc_all) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  int rc = uds_svc_clear_dtc(&reg, 0xFFFFFFU, resp, sizeof(resp), &resp_len,
                              &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 1U);
  ck_assert_uint_eq(resp[0], 0x54U);

  /* All statuses should be cleared */
  for (size_t i = 0U; i < 3U; i++) {
    ck_assert_uint_eq(entries[i].status, 0x00U);
    ck_assert_uint_eq(entries[i].snapshot_len, 0U);
  }
}
END_TEST

START_TEST(test_clear_dtc_by_group) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  /* Clear group 0x01 — should clear entries[0] and entries[1] */
  int rc = uds_svc_clear_dtc(&reg, 0x010000U, resp, sizeof(resp), &resp_len,
                              &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp[0], 0x54U);

  ck_assert_uint_eq(entries[0].status, 0x00U);
  ck_assert_uint_eq(entries[1].status, 0x00U);
  /* entries[2] (group 0x02) should be untouched */
  ck_assert_uint_eq(entries[2].status, 0x08U);
}
END_TEST

START_TEST(test_clear_dtc_unknown_group) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  /* Group 0x99 does not exist */
  int rc = uds_svc_clear_dtc(&reg, 0x990000U, resp, sizeof(resp), &resp_len,
                              &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_clear_dtc_null_registry) {
  int rc = uds_svc_clear_dtc(NULL, 0xFFFFFFU, resp, sizeof(resp), &resp_len,
                              &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_clear_dtc_buffer_too_small) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  int rc = uds_svc_clear_dtc(&reg, 0xFFFFFFU, resp, 0U, &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 3: Service 0x19 — ReadDTCInformation
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_read_dtc_01_count_all) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  /* Status mask 0xFF matches all non-zero statuses */
  uint8_t req[] = {0xFFU};
  int rc = uds_svc_read_dtc(&reg, 0x01U, req, 1U, resp, sizeof(resp),
                             &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 6U);
  ck_assert_uint_eq(resp[0], 0x59U);
  ck_assert_uint_eq(resp[1], 0x01U);
  ck_assert_uint_eq(resp[2], 0xFFU); /* statusAvailabilityMask */
  ck_assert_uint_eq(resp[3], 0x01U); /* formatIdentifier */
  /* count = 3 (all have non-zero status) */
  uint16_t count = ((uint16_t)resp[4] << 8) | resp[5];
  ck_assert_uint_eq(count, 3U);
}
END_TEST

START_TEST(test_read_dtc_01_count_filtered) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  /* Status mask 0x08 (confirmedDTC) — entries[0] (0x09) and [2] (0x08) match */
  uint8_t req[] = {0x08U};
  int rc = uds_svc_read_dtc(&reg, 0x01U, req, 1U, resp, sizeof(resp),
                             &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  uint16_t count = ((uint16_t)resp[4] << 8) | resp[5];
  ck_assert_uint_eq(count, 2U);
}
END_TEST

START_TEST(test_read_dtc_02_list_all) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  uint8_t req[] = {0xFFU};
  int rc = uds_svc_read_dtc(&reg, 0x02U, req, 1U, resp, sizeof(resp),
                             &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  /* [0x59, 0x02, mask] + 3*(3+1 bytes) = 3 + 12 = 15 bytes */
  ck_assert_uint_eq(resp_len, 3U + 4U * 3U);
  ck_assert_uint_eq(resp[0], 0x59U);
  ck_assert_uint_eq(resp[1], 0x02U);
  ck_assert_uint_eq(resp[2], 0xFFU);
  /* First DTC: 0x010203 status=0x09 */
  ck_assert_uint_eq(resp[3], 0x01U);
  ck_assert_uint_eq(resp[4], 0x02U);
  ck_assert_uint_eq(resp[5], 0x03U);
  ck_assert_uint_eq(resp[6], 0x09U);
}
END_TEST

START_TEST(test_read_dtc_02_list_filtered) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  /* Only pendingDTC (bit2 = 0x04) — entries[1] (0x04) matches */
  uint8_t req[] = {0x04U};
  int rc = uds_svc_read_dtc(&reg, 0x02U, req, 1U, resp, sizeof(resp),
                             &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  /* [0x59, 0x02, 0x04] + 1*(3+1) = 7 bytes */
  ck_assert_uint_eq(resp_len, 7U);
  ck_assert_uint_eq(resp[3], 0x01U);
  ck_assert_uint_eq(resp[4], 0x04U);
  ck_assert_uint_eq(resp[5], 0x05U);
  ck_assert_uint_eq(resp[6], 0x04U);
}
END_TEST

START_TEST(test_read_dtc_0a_all_supported) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  int rc = uds_svc_read_dtc(&reg, 0x0AU, NULL, 0U, resp, sizeof(resp),
                             &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  /* [0x59, 0x0A, 0xFF] + 3*(3+1) = 3 + 12 = 15 bytes */
  ck_assert_uint_eq(resp_len, 15U);
  ck_assert_uint_eq(resp[0], 0x59U);
  ck_assert_uint_eq(resp[1], 0x0AU);
  ck_assert_uint_eq(resp[2], 0xFFU);
}
END_TEST

START_TEST(test_read_dtc_unsupported_subfn) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  int rc = uds_svc_read_dtc(&reg, 0x05U, NULL, 0U, resp, sizeof(resp),
                             &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_read_dtc_null_registry) {
  int rc = uds_svc_read_dtc(NULL, 0x01U, NULL, 0U, resp, sizeof(resp),
                             &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_read_dtc_buffer_too_small_01) {
  UdsDtcRegistry reg;
  UdsDtcEntry entries[3];
  make_dtc_registry(&reg, entries);

  uint8_t small[4]; /* Need 6 for sub-fn 0x01 */
  uint8_t req[] = {0xFFU};
  int rc = uds_svc_read_dtc(&reg, 0x01U, req, 1U, small, sizeof(small),
                             &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 4: Service 0x31 — RoutineControl
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_routine_registry_init) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);
  ck_assert_ptr_eq(reg.entries, entries);
  ck_assert_uint_eq(reg.count, 2U);
}
END_TEST

START_TEST(test_routine_registry_init_null_noop) {
  uds_routine_registry_init(NULL, NULL, 0U);
}
END_TEST

START_TEST(test_routine_start) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  int rc = uds_svc_routine_control(&reg, 0x01U, 0x0200U,
                                    0x01U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 4U);
  ck_assert_uint_eq(resp[0], 0x71U);
  ck_assert_uint_eq(resp[1], 0x01U);
  ck_assert_uint_eq(resp[2], 0x02U);
  ck_assert_uint_eq(resp[3], 0x00U);
  ck_assert_int_eq((int)entries[0].state, (int)UDS_ROUTINE_RUNNING);
}
END_TEST

START_TEST(test_routine_stop) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  /* Start first */
  uds_svc_routine_control(&reg, 0x01U, 0x0200U, 0x01U, false, NULL, 0U,
                           resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq((int)entries[0].state, (int)UDS_ROUTINE_RUNNING);

  /* Now stop */
  int rc = uds_svc_routine_control(&reg, 0x02U, 0x0200U,
                                    0x01U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 4U);
  ck_assert_uint_eq(resp[1], 0x02U);
  ck_assert_int_eq((int)entries[0].state, (int)UDS_ROUTINE_STOPPED);
}
END_TEST

START_TEST(test_routine_stop_not_running) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  /* Try to stop a routine that is still IDLE */
  int rc = uds_svc_routine_control(&reg, 0x02U, 0x0200U,
                                    0x01U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT);
}
END_TEST

START_TEST(test_routine_request_results) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  /* entries[1] = 0x0300 already has RESULT_AVAILABLE and 2 result bytes */
  int rc = uds_svc_routine_control(&reg, 0x03U, 0x0300U,
                                    0x03U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  /* [0x71, 0x03, 0x03, 0x00, 0xCA, 0xFE] = 6 bytes */
  ck_assert_uint_eq(resp_len, 6U);
  ck_assert_uint_eq(resp[0], 0x71U);
  ck_assert_uint_eq(resp[1], 0x03U);
  ck_assert_uint_eq(resp[4], 0xCAU);
  ck_assert_uint_eq(resp[5], 0xFEU);
}
END_TEST

START_TEST(test_routine_request_results_not_available) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  /* entries[0] = 0x0200 is IDLE, no results yet */
  int rc = uds_svc_routine_control(&reg, 0x03U, 0x0200U,
                                    0x01U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT);
}
END_TEST

START_TEST(test_routine_not_found) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  int rc = uds_svc_routine_control(&reg, 0x01U, 0xDEADU,
                                    0x01U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_routine_invalid_subfn) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  int rc = uds_svc_routine_control(&reg, 0x05U, 0x0200U,
                                    0x01U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_routine_session_too_low) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  /* entries[1] requires session 0x03; send 0x01 */
  int rc = uds_svc_routine_control(&reg, 0x03U, 0x0300U,
                                    0x01U /* too low */, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_routine_null_registry) {
  int rc = uds_svc_routine_control(NULL, 0x01U, 0x0200U,
                                    0x01U, false, NULL, 0U,
                                    resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_routine_null_resp) {
  UdsRoutineRegistry reg;
  UdsRoutineEntry entries[2];
  make_routine_registry(&reg, entries);

  int rc = uds_svc_routine_control(&reg, 0x01U, 0x0200U,
                                    0x01U, false, NULL, 0U,
                                    NULL, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Test suite / runner wiring
 * ════════════════════════════════════════════════════════════════════════════
 */

static Suite *dtc_suite(void) {
  Suite *s = suite_create("uds_dtc_and_routine");

  /* ── DTC registry init ─────────────────────────────────────────── */
  TCase *tc_dtc_init = tcase_create("dtc_registry_init");
  tcase_add_test(tc_dtc_init, test_dtc_registry_init_basic);
  tcase_add_test(tc_dtc_init, test_dtc_registry_init_null_noop);
  suite_add_tcase(s, tc_dtc_init);

  /* ── Service 0x14 ──────────────────────────────────────────────── */
  TCase *tc_clear = tcase_create("clear_dtc");
  tcase_add_test(tc_clear, test_clear_dtc_all);
  tcase_add_test(tc_clear, test_clear_dtc_by_group);
  tcase_add_test(tc_clear, test_clear_dtc_unknown_group);
  tcase_add_test(tc_clear, test_clear_dtc_null_registry);
  tcase_add_test(tc_clear, test_clear_dtc_buffer_too_small);
  suite_add_tcase(s, tc_clear);

  /* ── Service 0x19 ──────────────────────────────────────────────── */
  TCase *tc_rdtc = tcase_create("read_dtc");
  tcase_add_test(tc_rdtc, test_read_dtc_01_count_all);
  tcase_add_test(tc_rdtc, test_read_dtc_01_count_filtered);
  tcase_add_test(tc_rdtc, test_read_dtc_02_list_all);
  tcase_add_test(tc_rdtc, test_read_dtc_02_list_filtered);
  tcase_add_test(tc_rdtc, test_read_dtc_0a_all_supported);
  tcase_add_test(tc_rdtc, test_read_dtc_unsupported_subfn);
  tcase_add_test(tc_rdtc, test_read_dtc_null_registry);
  tcase_add_test(tc_rdtc, test_read_dtc_buffer_too_small_01);
  suite_add_tcase(s, tc_rdtc);

  /* ── Service 0x31 ──────────────────────────────────────────────── */
  TCase *tc_rout = tcase_create("routine_control");
  tcase_add_test(tc_rout, test_routine_registry_init);
  tcase_add_test(tc_rout, test_routine_registry_init_null_noop);
  tcase_add_test(tc_rout, test_routine_start);
  tcase_add_test(tc_rout, test_routine_stop);
  tcase_add_test(tc_rout, test_routine_stop_not_running);
  tcase_add_test(tc_rout, test_routine_request_results);
  tcase_add_test(tc_rout, test_routine_request_results_not_available);
  tcase_add_test(tc_rout, test_routine_not_found);
  tcase_add_test(tc_rout, test_routine_invalid_subfn);
  tcase_add_test(tc_rout, test_routine_session_too_low);
  tcase_add_test(tc_rout, test_routine_null_registry);
  tcase_add_test(tc_rout, test_routine_null_resp);
  suite_add_tcase(s, tc_rout);

  return s;
}

int main(void) {
  Suite *s = dtc_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
