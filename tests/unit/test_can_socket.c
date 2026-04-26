/**
 * @file test_can_socket.c
 * @brief Unit tests for the uds_can SocketCAN adapter layer.
 *
 * Tests are partitioned into two groups:
 *   - Pure-logic tests: test helper functions that require no hardware/vCAN.
 *   - Socket lifecycle tests: test open/close/filter/send/recv against vCAN.
 *     These are skipped automatically when vcan0 is unavailable.
 *
 * Build & run:
 *   cmake -B build && cmake --build build && cd build && ctest -V
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Access the internal header via the library's public include path */
#include "uds_can.h"

/* ── Helpers ────────────────────────────────────────────────────────────── */

/** Returns 1 if vcan0 is available (reachable via ioctl), 0 otherwise. */
static int vcan0_available(void) {
  UdsCanSocket s;
  int rc = uds_can_open(&s, "vcan0");
  if (rc == UDS_CAN_OK) {
    uds_can_close(&s);
    return 1;
  }
  return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 1: Pure-logic — no kernel/hardware dependency
 * ════════════════════════════════════════════════════════════════════════════
 */

/* ── uds_can_req_id ─────────────────────────────────────────────────────── */

START_TEST(test_req_id_ecu1) { ck_assert_uint_eq(uds_can_req_id(1), 0x601U); }
END_TEST

START_TEST(test_req_id_ecu_max) {
  ck_assert_uint_eq(uds_can_req_id(UDS_CAN_ECU_ID_MAX),
                    UDS_CAN_REQ_BASE + UDS_CAN_ECU_ID_MAX);
}
END_TEST

START_TEST(test_req_id_invalid_zero) {
  ck_assert_uint_eq(uds_can_req_id(0), 0U);
}
END_TEST

START_TEST(test_req_id_invalid_overflow) {
  ck_assert_uint_eq(uds_can_req_id(0x80), 0U);
}
END_TEST

/* ── uds_can_resp_id ────────────────────────────────────────────────────── */

START_TEST(test_resp_id_ecu1) { ck_assert_uint_eq(uds_can_resp_id(1), 0x681U); }
END_TEST

START_TEST(test_resp_id_ecu_max) {
  ck_assert_uint_eq(uds_can_resp_id(UDS_CAN_ECU_ID_MAX),
                    UDS_CAN_RESP_BASE + UDS_CAN_ECU_ID_MAX);
}
END_TEST

START_TEST(test_resp_id_invalid_zero) {
  ck_assert_uint_eq(uds_can_resp_id(0), 0U);
}
END_TEST

START_TEST(test_resp_id_invalid_overflow) {
  ck_assert_uint_eq(uds_can_resp_id(0xFF), 0U);
}
END_TEST

/* ── uds_can_ecu_id_from_resp ───────────────────────────────────────────── */

START_TEST(test_ecu_id_from_resp_ecu1) {
  ck_assert_uint_eq(uds_can_ecu_id_from_resp(0x681U), 1U);
}
END_TEST

START_TEST(test_ecu_id_from_resp_ecu_max) {
  uint32_t resp = UDS_CAN_RESP_BASE + UDS_CAN_ECU_ID_MAX;
  ck_assert_uint_eq(uds_can_ecu_id_from_resp(resp), UDS_CAN_ECU_ID_MAX);
}
END_TEST

START_TEST(test_ecu_id_from_resp_base_invalid) {
  /* Exactly the base (offset 0) is not a valid ECU ID */
  ck_assert_uint_eq(uds_can_ecu_id_from_resp(UDS_CAN_RESP_BASE), 0U);
}
END_TEST

START_TEST(test_ecu_id_from_resp_below_base) {
  ck_assert_uint_eq(uds_can_ecu_id_from_resp(0x100U), 0U);
}
END_TEST

START_TEST(test_ecu_id_from_resp_out_of_range) {
  /* offset 0x80 > ECU_ID_MAX */
  ck_assert_uint_eq(uds_can_ecu_id_from_resp(UDS_CAN_RESP_BASE + 0x80U), 0U);
}
END_TEST

/* ── round-trip req/resp symmetry ──────────────────────────────────────────*/

START_TEST(test_roundtrip_req_resp) {
  for (uint8_t id = UDS_CAN_ECU_ID_MIN; id <= UDS_CAN_ECU_ID_MAX; id++) {
    uint32_t req = uds_can_req_id(id);
    uint32_t resp = uds_can_resp_id(id);

    /* Must differ (request base != response base) */
    ck_assert_uint_ne(req, resp);
    /* Response must round-trip back to the same ECU id */
    ck_assert_uint_eq(uds_can_ecu_id_from_resp(resp), id);
  }
}
END_TEST

/* ── uds_can_ecu_filters ────────────────────────────────────────────────── */

START_TEST(test_ecu_filters_count) {
  UdsCanFilter f[3];
  unsigned int n = uds_can_ecu_filters(f, 1);
  ck_assert_uint_eq(n, 3U);
}
END_TEST

START_TEST(test_ecu_filters_req_entry) {
  UdsCanFilter f[3];
  uds_can_ecu_filters(f, 1);
  ck_assert_uint_eq(f[0].can_id, 0x601U);
  ck_assert_uint_eq(f[0].can_mask, 0x7FFU);
}
END_TEST

START_TEST(test_ecu_filters_functional_entry) {
  UdsCanFilter f[3];
  uds_can_ecu_filters(f, 1);
  ck_assert_uint_eq(f[1].can_id, UDS_CAN_FUNCTIONAL_ID);
  ck_assert_uint_eq(f[1].can_mask, 0x7FFU);
}
END_TEST

START_TEST(test_ecu_filters_resp_entry) {
  UdsCanFilter f[3];
  uds_can_ecu_filters(f, 1);
  ck_assert_uint_eq(f[2].can_id, 0x681U);
  ck_assert_uint_eq(f[2].can_mask, 0x7FFU);
}
END_TEST

START_TEST(test_ecu_filters_invalid_id) {
  UdsCanFilter f[3];
  ck_assert_uint_eq(uds_can_ecu_filters(f, 0), 0U);
  ck_assert_uint_eq(uds_can_ecu_filters(f, 0x80), 0U);
}
END_TEST

START_TEST(test_ecu_filters_null_buf) {
  ck_assert_uint_eq(uds_can_ecu_filters(NULL, 1), 0U);
}
END_TEST

/* ── uds_can_strerror ───────────────────────────────────────────────────── */

START_TEST(test_strerror_ok) {
  const char *s = uds_can_strerror(UDS_CAN_OK);
  ck_assert_ptr_nonnull(s);
  ck_assert_str_eq(s, "Success");
}
END_TEST

START_TEST(test_strerror_unknown) {
  const char *s = uds_can_strerror(-999);
  ck_assert_ptr_nonnull(s);
}
END_TEST

/* ── Null-safety for open/close ─────────────────────────────────────────── */

START_TEST(test_open_null_sock) {
  ck_assert_int_eq(uds_can_open(NULL, "vcan0"), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_open_null_ifname) {
  UdsCanSocket s;
  ck_assert_int_eq(uds_can_open(&s, NULL), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_open_empty_ifname) {
  UdsCanSocket s;
  ck_assert_int_eq(uds_can_open(&s, ""), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_open_nonexistent_if) {
  UdsCanSocket s;
  int rc = uds_can_open(&s, "nosuchif99");
  /* Must fail — either IFINDEX error or SOCKET error depending on kernel */
  ck_assert_int_lt(rc, 0);
}
END_TEST

START_TEST(test_close_null) {
  /* Must not crash */
  uds_can_close(NULL);
}
END_TEST

START_TEST(test_close_invalid_fd) {
  UdsCanSocket s = {.fd = -1};
  /* Must not crash or double-close */
  uds_can_close(&s);
  ck_assert_int_eq(s.fd, -1);
}
END_TEST

/* ── Null-safety for send/recv/filter ───────────────────────────────────── */

/* Clearly invalid fd used in null-safety tests that check parameter guards.
 * These tests must ONLY reach the parameter-validation path (which fires
 * before any fd use), so no actual syscall on fd -1 ever occurs. */
#define INVALID_FD_FOR_PARAM_TEST (-1)

START_TEST(test_send_null_sock) {
  struct can_frame frame = {0};
  ck_assert_int_eq(uds_can_send(NULL, &frame), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_send_null_frame) {
  UdsCanSocket s = {.fd = INVALID_FD_FOR_PARAM_TEST};
  ck_assert_int_eq(uds_can_send(&s, NULL), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_recv_null_sock) {
  struct can_frame frame = {0};
  ck_assert_int_eq(uds_can_recv(NULL, &frame, 0), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_recv_null_frame) {
  UdsCanSocket s = {.fd = INVALID_FD_FOR_PARAM_TEST};
  ck_assert_int_eq(uds_can_recv(&s, NULL, 0), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_filter_null_sock) {
  ck_assert_int_eq(uds_can_set_filter(NULL, NULL, 0), UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_filter_count_exceeds_max) {
  UdsCanSocket s = {.fd = INVALID_FD_FOR_PARAM_TEST};
  UdsCanFilter f[UDS_CAN_MAX_FILTERS + 1];
  memset(f, 0, sizeof(f));
  ck_assert_int_eq(uds_can_set_filter(&s, f, UDS_CAN_MAX_FILTERS + 1),
                   UDS_CAN_ERR_PARAM);
}
END_TEST

START_TEST(test_filter_null_array_nonzero_count) {
  UdsCanSocket s = {.fd = INVALID_FD_FOR_PARAM_TEST};
  ck_assert_int_eq(uds_can_set_filter(&s, NULL, 1), UDS_CAN_ERR_PARAM);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 2: vCAN integration — skipped when vcan0 unavailable
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_vcan_open_close) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  UdsCanSocket s;
  int rc = uds_can_open(&s, "vcan0");
  ck_assert_int_eq(rc, UDS_CAN_OK);
  ck_assert_int_ge(s.fd, 0);
  uds_can_close(&s);
  ck_assert_int_eq(s.fd, -1);
}
END_TEST

START_TEST(test_vcan_recv_timeout) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  UdsCanSocket s;
  ck_assert_int_eq(uds_can_open(&s, "vcan0"), UDS_CAN_OK);

  /* Block all incoming frames so we are guaranteed to time out */
  UdsCanFilter deny = {.can_id = 0x000U, .can_mask = 0x7FFU};
  uds_can_set_filter(&s, &deny, 1);

  struct can_frame frame;
  int rc = uds_can_recv(&s, &frame, 50 /* ms */);
  ck_assert_int_eq(rc, UDS_CAN_ERR_TIMEOUT);

  uds_can_close(&s);
}
END_TEST

START_TEST(test_vcan_send_recv_loopback) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  /* Open two sockets: one to send, one to receive */
  UdsCanSocket tx, rx;
  ck_assert_int_eq(uds_can_open(&tx, "vcan0"), UDS_CAN_OK);
  ck_assert_int_eq(uds_can_open(&rx, "vcan0"), UDS_CAN_OK);

  /* Filter on receiver to accept only our test frame */
  UdsCanFilter f;
  f.can_id = 0x601U;
  f.can_mask = 0x7FFU;
  ck_assert_int_eq(uds_can_set_filter(&rx, &f, 1), UDS_CAN_OK);

  /* Build a minimal UDS read-DID frame (SID 0x22 F1 90) */
  struct can_frame out;
  memset(&out, 0, sizeof(out));
  out.can_id = 0x601U;
  out.can_dlc = 4;
  out.data[0] = 0x03; /* SF, length 3 */
  out.data[1] = 0x22; /* SID ReadDataByIdentifier */
  out.data[2] = 0xF1;
  out.data[3] = 0x90;

  ck_assert_int_eq(uds_can_send(&tx, &out), UDS_CAN_OK);

  struct can_frame in;
  memset(&in, 0, sizeof(in));
  ck_assert_int_eq(uds_can_recv(&rx, &in, 200 /* ms */), UDS_CAN_OK);

  ck_assert_uint_eq(in.can_id, out.can_id);
  ck_assert_uint_eq(in.can_dlc, out.can_dlc);
  ck_assert_int_eq(memcmp(in.data, out.data, out.can_dlc), 0);

  uds_can_close(&tx);
  uds_can_close(&rx);
}
END_TEST

START_TEST(test_vcan_ecu_filters_applied) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  UdsCanSocket tx, rx;
  ck_assert_int_eq(uds_can_open(&tx, "vcan0"), UDS_CAN_OK);
  ck_assert_int_eq(uds_can_open(&rx, "vcan0"), UDS_CAN_OK);

  /* Apply ECU 1 filters on receiver */
  UdsCanFilter filters[3];
  unsigned int n = uds_can_ecu_filters(filters, 1);
  ck_assert_uint_eq(n, 3U);
  ck_assert_int_eq(uds_can_set_filter(&rx, filters, n), UDS_CAN_OK);

  /* Send a functional broadcast — should be accepted */
  struct can_frame functional;
  memset(&functional, 0, sizeof(functional));
  functional.can_id = UDS_CAN_FUNCTIONAL_ID;
  functional.can_dlc = 2;
  functional.data[0] = 0x02;
  functional.data[1] = 0x3E; /* Tester Present */

  ck_assert_int_eq(uds_can_send(&tx, &functional), UDS_CAN_OK);

  struct can_frame received;
  memset(&received, 0, sizeof(received));
  ck_assert_int_eq(uds_can_recv(&rx, &received, 200), UDS_CAN_OK);
  ck_assert_uint_eq(received.can_id, UDS_CAN_FUNCTIONAL_ID);

  uds_can_close(&tx);
  uds_can_close(&rx);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Test suite / runner wiring
 * ════════════════════════════════════════════════════════════════════════════
 */

static Suite *can_socket_suite(void) {
  Suite *s = suite_create("uds_can");

  /* ── Pure-logic (no vCAN) ─────────────────────────────────── */
  TCase *tc_ids = tcase_create("can_id_helpers");
  tcase_add_test(tc_ids, test_req_id_ecu1);
  tcase_add_test(tc_ids, test_req_id_ecu_max);
  tcase_add_test(tc_ids, test_req_id_invalid_zero);
  tcase_add_test(tc_ids, test_req_id_invalid_overflow);
  tcase_add_test(tc_ids, test_resp_id_ecu1);
  tcase_add_test(tc_ids, test_resp_id_ecu_max);
  tcase_add_test(tc_ids, test_resp_id_invalid_zero);
  tcase_add_test(tc_ids, test_resp_id_invalid_overflow);
  tcase_add_test(tc_ids, test_ecu_id_from_resp_ecu1);
  tcase_add_test(tc_ids, test_ecu_id_from_resp_ecu_max);
  tcase_add_test(tc_ids, test_ecu_id_from_resp_base_invalid);
  tcase_add_test(tc_ids, test_ecu_id_from_resp_below_base);
  tcase_add_test(tc_ids, test_ecu_id_from_resp_out_of_range);
  tcase_add_test(tc_ids, test_roundtrip_req_resp);
  suite_add_tcase(s, tc_ids);

  TCase *tc_filt = tcase_create("ecu_filters");
  tcase_add_test(tc_filt, test_ecu_filters_count);
  tcase_add_test(tc_filt, test_ecu_filters_req_entry);
  tcase_add_test(tc_filt, test_ecu_filters_functional_entry);
  tcase_add_test(tc_filt, test_ecu_filters_resp_entry);
  tcase_add_test(tc_filt, test_ecu_filters_invalid_id);
  tcase_add_test(tc_filt, test_ecu_filters_null_buf);
  suite_add_tcase(s, tc_filt);

  TCase *tc_err = tcase_create("error_strings");
  tcase_add_test(tc_err, test_strerror_ok);
  tcase_add_test(tc_err, test_strerror_unknown);
  suite_add_tcase(s, tc_err);

  TCase *tc_null = tcase_create("null_safety");
  tcase_add_test(tc_null, test_open_null_sock);
  tcase_add_test(tc_null, test_open_null_ifname);
  tcase_add_test(tc_null, test_open_empty_ifname);
  tcase_add_test(tc_null, test_open_nonexistent_if);
  tcase_add_test(tc_null, test_close_null);
  tcase_add_test(tc_null, test_close_invalid_fd);
  tcase_add_test(tc_null, test_send_null_sock);
  tcase_add_test(tc_null, test_send_null_frame);
  tcase_add_test(tc_null, test_recv_null_sock);
  tcase_add_test(tc_null, test_recv_null_frame);
  tcase_add_test(tc_null, test_filter_null_sock);
  tcase_add_test(tc_null, test_filter_count_exceeds_max);
  tcase_add_test(tc_null, test_filter_null_array_nonzero_count);
  suite_add_tcase(s, tc_null);

  /* ── vCAN integration (auto-skipped when unavailable) ─────── */
  TCase *tc_vcan = tcase_create("vcan_integration");
  tcase_set_timeout(tc_vcan, 5);
  tcase_add_test(tc_vcan, test_vcan_open_close);
  tcase_add_test(tc_vcan, test_vcan_recv_timeout);
  tcase_add_test(tc_vcan, test_vcan_send_recv_loopback);
  tcase_add_test(tc_vcan, test_vcan_ecu_filters_applied);
  suite_add_tcase(s, tc_vcan);

  return s;
}

int main(void) {
  Suite *s = can_socket_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
