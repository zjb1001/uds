/**
 * @file test_data.c
 * @brief Unit tests for UDS data services (0x22, 0x2E, 0x11, 0x28).
 *
 * Build & run:
 *   cmake -B build && cmake --build build && cd build && ctest -V
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uds_core.h"
#include "uds_data.h"
#include "uds_nrc.h"

/* ── Shared test fixtures ───────────────────────────────────────────────── */

static uint8_t resp[256];
static size_t  resp_len;
static uint8_t nrc;

/* DID data storage buffers */
static uint8_t vin_buf[17];
static uint8_t odometer_buf[4];
static uint8_t serial_buf[8];

/** Build a registry with three DIDs for reuse across tests. */
static void make_registry(UdsDidRegistry *reg, UdsDidEntry *entries) {
  /* VIN: DID 0xF190, read-only, default session */
  memcpy(vin_buf, "1HGCM82633A123456", 17);
  entries[0].did_id = 0xF190U;
  entries[0].data = vin_buf;
  entries[0].data_len = 17U;
  entries[0].writable = false;
  entries[0].min_session = 0x01U;
  entries[0].min_write_session = 0x03U;
  entries[0].requires_security = false;

  /* Odometer: DID 0xF40A, writable, default session, no security */
  odometer_buf[0] = 0x00U;
  odometer_buf[1] = 0x01U;
  odometer_buf[2] = 0x86U;
  odometer_buf[3] = 0xA0U;
  entries[1].did_id = 0xF40AU;
  entries[1].data = odometer_buf;
  entries[1].data_len = 4U;
  entries[1].writable = true;
  entries[1].min_session = 0x01U;
  entries[1].min_write_session = 0x01U;
  entries[1].requires_security = false;

  /* Serial: DID 0xF18C, writable, extended session, requires security */
  memset(serial_buf, 0xAAU, 8U);
  entries[2].did_id = 0xF18CU;
  entries[2].data = serial_buf;
  entries[2].data_len = 8U;
  entries[2].writable = true;
  entries[2].min_session = 0x01U;
  entries[2].min_write_session = 0x03U;
  entries[2].requires_security = true;

  uds_did_registry_init(reg, entries, 3U);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 1: DID registry initialisation
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_did_registry_init_basic) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);
  ck_assert_ptr_eq(reg.entries, entries);
  ck_assert_uint_eq(reg.count, 3U);
}
END_TEST

START_TEST(test_did_registry_init_null_noop) {
  /* Must not crash */
  uds_did_registry_init(NULL, NULL, 0U);
}
END_TEST

START_TEST(test_did_registry_init_empty) {
  UdsDidRegistry reg;
  uds_did_registry_init(&reg, NULL, 0U);
  ck_assert_uint_eq(reg.count, 0U);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 2: Service 0x22 — ReadDataByIdentifier
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_rdbi_read_valid_did) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint16_t dids[] = {0xF190U};
  int rc = uds_svc_read_did(&reg, dids, 1U, resp, sizeof(resp), &resp_len,
                             &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  /* Response: [0x62, 0xF1, 0x90, <17 vin bytes>] = 20 bytes */
  ck_assert_uint_eq(resp_len, 1U + 2U + 17U);
  ck_assert_uint_eq(resp[0], 0x62U);
  ck_assert_uint_eq(resp[1], 0xF1U);
  ck_assert_uint_eq(resp[2], 0x90U);
  ck_assert(memcmp(&resp[3], "1HGCM82633A123456", 17) == 0);
}
END_TEST

START_TEST(test_rdbi_read_unknown_did) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint16_t dids[] = {0xDEADU};
  int rc = uds_svc_read_did(&reg, dids, 1U, resp, sizeof(resp), &resp_len,
                             &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_rdbi_read_multiple_dids) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint16_t dids[] = {0xF190U, 0xF40AU};
  int rc = uds_svc_read_did(&reg, dids, 2U, resp, sizeof(resp), &resp_len,
                             &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  /* [0x62] + [0xF1,0x90,<17>] + [0xF4,0x0A,<4>] = 1+19+6 = 26 bytes */
  ck_assert_uint_eq(resp_len, 1U + 2U + 17U + 2U + 4U);
  ck_assert_uint_eq(resp[0], 0x62U);
  ck_assert_uint_eq(resp[1], 0xF1U);
  ck_assert_uint_eq(resp[2], 0x90U);
}
END_TEST

START_TEST(test_rdbi_second_did_not_found) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  /* First DID valid, second unknown — should fail on second */
  uint16_t dids[] = {0xF190U, 0xBADDU};
  int rc = uds_svc_read_did(&reg, dids, 2U, resp, sizeof(resp), &resp_len,
                             &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_rdbi_null_registry) {
  uint16_t dids[] = {0xF190U};
  int rc = uds_svc_read_did(NULL, dids, 1U, resp, sizeof(resp), &resp_len,
                             &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_rdbi_null_did_list) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  int rc = uds_svc_read_did(&reg, NULL, 1U, resp, sizeof(resp), &resp_len,
                             &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_rdbi_zero_count) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint16_t dids[] = {0xF190U};
  int rc = uds_svc_read_did(&reg, dids, 0U, resp, sizeof(resp), &resp_len,
                             &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_rdbi_buffer_too_small) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  /* VIN needs 20 bytes; give only 5 */
  uint8_t small[5];
  uint16_t dids[] = {0xF190U};
  int rc = uds_svc_read_did(&reg, dids, 1U, small, sizeof(small), &resp_len,
                             &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 3: Service 0x2E — WriteDataByIdentifier
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_wdbi_write_valid_did) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint8_t new_odo[] = {0x00U, 0x00U, 0x01U, 0x00U};
  int rc = uds_svc_write_did(&reg, 0xF40AU, new_odo, 4U,
                              0x01U /* default session */,
                              false /* no security needed */,
                              resp, sizeof(resp), &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 3U);
  ck_assert_uint_eq(resp[0], 0x6EU);
  ck_assert_uint_eq(resp[1], 0xF4U);
  ck_assert_uint_eq(resp[2], 0x0AU);
  /* Verify data was actually written */
  ck_assert(memcmp(odometer_buf, new_odo, 4U) == 0);
}
END_TEST

START_TEST(test_wdbi_write_read_only_did) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint8_t dummy[17];
  memset(dummy, 0U, 17U);
  int rc = uds_svc_write_did(&reg, 0xF190U, dummy, 17U,
                              0x03U, true, resp, sizeof(resp), &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_wdbi_write_wrong_length) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  /* Odometer expects 4 bytes, send 3 */
  uint8_t bad_data[] = {0x01U, 0x02U, 0x03U};
  int rc = uds_svc_write_did(&reg, 0xF40AU, bad_data, 3U,
                              0x01U, false, resp, sizeof(resp), &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
}
END_TEST

START_TEST(test_wdbi_write_session_too_low) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  /* Serial requires extended session (0x03), send default (0x01) */
  uint8_t new_serial[8];
  memset(new_serial, 0x55U, 8U);
  int rc = uds_svc_write_did(&reg, 0xF18CU, new_serial, 8U,
                              0x01U /* default, too low */,
                              true, resp, sizeof(resp), &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_wdbi_write_no_security) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint8_t new_serial[8];
  memset(new_serial, 0x55U, 8U);
  int rc = uds_svc_write_did(&reg, 0xF18CU, new_serial, 8U,
                              0x03U /* extended session */,
                              false /* security NOT unlocked */,
                              resp, sizeof(resp), &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SECURITY_ACCESS_DENIED);
}
END_TEST

START_TEST(test_wdbi_write_with_security_ok) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint8_t new_serial[8];
  memset(new_serial, 0x55U, 8U);
  int rc = uds_svc_write_did(&reg, 0xF18CU, new_serial, 8U,
                              0x03U, true, resp, sizeof(resp), &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 3U);
  ck_assert_uint_eq(resp[0], 0x6EU);
  ck_assert_uint_eq(resp[1], 0xF1U);
  ck_assert_uint_eq(resp[2], 0x8CU);
}
END_TEST

START_TEST(test_wdbi_unknown_did) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint8_t data[] = {0x01U};
  int rc = uds_svc_write_did(&reg, 0xDEADU, data, 1U, 0x01U, false, resp,
                              sizeof(resp), &resp_len, &nrc);

  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_wdbi_null_registry) {
  uint8_t data[] = {0x01U};
  int rc = uds_svc_write_did(NULL, 0xF40AU, data, 1U, 0x01U, false, resp,
                              sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_wdbi_null_data) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  int rc = uds_svc_write_did(&reg, 0xF40AU, NULL, 4U, 0x01U, false, resp,
                              sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_wdbi_buffer_too_small) {
  UdsDidRegistry reg;
  UdsDidEntry entries[3];
  make_registry(&reg, entries);

  uint8_t new_odo[] = {0x00U, 0x00U, 0x01U, 0x00U};
  uint8_t small[2];
  int rc = uds_svc_write_did(&reg, 0xF40AU, new_odo, 4U, 0x01U, false,
                              small, sizeof(small), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 4: Service 0x11 — ECUReset
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_ecu_reset_hard) {
  int rc = uds_svc_ecu_reset(0x01U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U);
  ck_assert_uint_eq(resp[0], 0x51U);
  ck_assert_uint_eq(resp[1], 0x01U);
}
END_TEST

START_TEST(test_ecu_reset_key_off_on) {
  int rc = uds_svc_ecu_reset(0x02U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U);
  ck_assert_uint_eq(resp[0], 0x51U);
  ck_assert_uint_eq(resp[1], 0x02U);
}
END_TEST

START_TEST(test_ecu_reset_soft) {
  int rc = uds_svc_ecu_reset(0x03U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U);
  ck_assert_uint_eq(resp[0], 0x51U);
  ck_assert_uint_eq(resp[1], 0x03U);
}
END_TEST

START_TEST(test_ecu_reset_invalid_type) {
  int rc = uds_svc_ecu_reset(0x04U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_ecu_reset_invalid_type_zero) {
  int rc = uds_svc_ecu_reset(0x00U, resp, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_ecu_reset_null_resp) {
  int rc = uds_svc_ecu_reset(0x01U, NULL, sizeof(resp), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_ecu_reset_null_nrc) {
  int rc = uds_svc_ecu_reset(0x01U, resp, sizeof(resp), &resp_len, NULL);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_ecu_reset_buffer_too_small) {
  uint8_t small[1];
  int rc = uds_svc_ecu_reset(0x01U, small, sizeof(small), &resp_len, &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 5: Service 0x28 — CommunicationControl
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_comm_ctrl_enable_rx_tx) {
  int rc = uds_svc_comm_control(0x00U, 0x01U, resp, sizeof(resp), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp_len, 2U);
  ck_assert_uint_eq(resp[0], 0x68U);
  ck_assert_uint_eq(resp[1], 0x00U);
}
END_TEST

START_TEST(test_comm_ctrl_enable_rx_disable_tx) {
  int rc = uds_svc_comm_control(0x01U, 0x01U, resp, sizeof(resp), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp[0], 0x68U);
  ck_assert_uint_eq(resp[1], 0x01U);
}
END_TEST

START_TEST(test_comm_ctrl_disable_rx_enable_tx) {
  int rc = uds_svc_comm_control(0x02U, 0x01U, resp, sizeof(resp), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp[1], 0x02U);
}
END_TEST

START_TEST(test_comm_ctrl_disable_rx_tx) {
  int rc = uds_svc_comm_control(0x03U, 0x01U, resp, sizeof(resp), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_OK);
  ck_assert_uint_eq(resp[1], 0x03U);
}
END_TEST

START_TEST(test_comm_ctrl_invalid_control_type) {
  int rc = uds_svc_comm_control(0x04U, 0x01U, resp, sizeof(resp), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED);
}
END_TEST

START_TEST(test_comm_ctrl_invalid_comm_type) {
  int rc = uds_svc_comm_control(0x00U, 0x00U, resp, sizeof(resp), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
  ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_comm_ctrl_null_resp) {
  int rc = uds_svc_comm_control(0x00U, 0x01U, NULL, sizeof(resp), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_comm_ctrl_buffer_too_small) {
  uint8_t small[1];
  int rc = uds_svc_comm_control(0x00U, 0x01U, small, sizeof(small), &resp_len,
                                 &nrc);
  ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Test suite / runner wiring
 * ════════════════════════════════════════════════════════════════════════════
 */

static Suite *data_suite(void) {
  Suite *s = suite_create("uds_data");

  /* ── DID registry init ─────────────────────────────────────────── */
  TCase *tc_init = tcase_create("did_registry_init");
  tcase_add_test(tc_init, test_did_registry_init_basic);
  tcase_add_test(tc_init, test_did_registry_init_null_noop);
  tcase_add_test(tc_init, test_did_registry_init_empty);
  suite_add_tcase(s, tc_init);

  /* ── Service 0x22 ──────────────────────────────────────────────── */
  TCase *tc_rdbi = tcase_create("read_did");
  tcase_add_test(tc_rdbi, test_rdbi_read_valid_did);
  tcase_add_test(tc_rdbi, test_rdbi_read_unknown_did);
  tcase_add_test(tc_rdbi, test_rdbi_read_multiple_dids);
  tcase_add_test(tc_rdbi, test_rdbi_second_did_not_found);
  tcase_add_test(tc_rdbi, test_rdbi_null_registry);
  tcase_add_test(tc_rdbi, test_rdbi_null_did_list);
  tcase_add_test(tc_rdbi, test_rdbi_zero_count);
  tcase_add_test(tc_rdbi, test_rdbi_buffer_too_small);
  suite_add_tcase(s, tc_rdbi);

  /* ── Service 0x2E ──────────────────────────────────────────────── */
  TCase *tc_wdbi = tcase_create("write_did");
  tcase_add_test(tc_wdbi, test_wdbi_write_valid_did);
  tcase_add_test(tc_wdbi, test_wdbi_write_read_only_did);
  tcase_add_test(tc_wdbi, test_wdbi_write_wrong_length);
  tcase_add_test(tc_wdbi, test_wdbi_write_session_too_low);
  tcase_add_test(tc_wdbi, test_wdbi_write_no_security);
  tcase_add_test(tc_wdbi, test_wdbi_write_with_security_ok);
  tcase_add_test(tc_wdbi, test_wdbi_unknown_did);
  tcase_add_test(tc_wdbi, test_wdbi_null_registry);
  tcase_add_test(tc_wdbi, test_wdbi_null_data);
  tcase_add_test(tc_wdbi, test_wdbi_buffer_too_small);
  suite_add_tcase(s, tc_wdbi);

  /* ── Service 0x11 ──────────────────────────────────────────────── */
  TCase *tc_reset = tcase_create("ecu_reset");
  tcase_add_test(tc_reset, test_ecu_reset_hard);
  tcase_add_test(tc_reset, test_ecu_reset_key_off_on);
  tcase_add_test(tc_reset, test_ecu_reset_soft);
  tcase_add_test(tc_reset, test_ecu_reset_invalid_type);
  tcase_add_test(tc_reset, test_ecu_reset_invalid_type_zero);
  tcase_add_test(tc_reset, test_ecu_reset_null_resp);
  tcase_add_test(tc_reset, test_ecu_reset_null_nrc);
  tcase_add_test(tc_reset, test_ecu_reset_buffer_too_small);
  suite_add_tcase(s, tc_reset);

  /* ── Service 0x28 ──────────────────────────────────────────────── */
  TCase *tc_comm = tcase_create("comm_control");
  tcase_add_test(tc_comm, test_comm_ctrl_enable_rx_tx);
  tcase_add_test(tc_comm, test_comm_ctrl_enable_rx_disable_tx);
  tcase_add_test(tc_comm, test_comm_ctrl_disable_rx_enable_tx);
  tcase_add_test(tc_comm, test_comm_ctrl_disable_rx_tx);
  tcase_add_test(tc_comm, test_comm_ctrl_invalid_control_type);
  tcase_add_test(tc_comm, test_comm_ctrl_invalid_comm_type);
  tcase_add_test(tc_comm, test_comm_ctrl_null_resp);
  tcase_add_test(tc_comm, test_comm_ctrl_buffer_too_small);
  suite_add_tcase(s, tc_comm);

  return s;
}

int main(void) {
  Suite *s = data_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
