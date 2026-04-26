/**
 * @file test_iso_tp.c
 * @brief Unit tests for the uds_tp ISO-TP transport layer.
 *
 * Tests are partitioned into two groups:
 *   - Pure-logic tests: encode/decode helpers, no kernel/hardware dependency.
 *   - vCAN integration tests: full send/recv PDU over a virtual CAN bus.
 *     Skipped automatically when vcan0 is unavailable.
 *
 * Build & run:
 *   cmake -B build && cmake --build build && cd build && ctest -V
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uds_can.h"
#include "uds_tp.h"

/* ── Helpers ────────────────────────────────────────────────────────────── */

/** Returns 1 if vcan0 is accessible, 0 otherwise. */
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
 * Suite 1: Frame type classification
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_frame_type_sf) {
  struct can_frame f = {0};
  f.can_dlc = 4;
  f.data[0] = 0x03; /* PCI: SF, length 3 */
  ck_assert_int_eq(uds_tp_frame_type(&f), UDS_TP_FRAME_SF);
}
END_TEST

START_TEST(test_frame_type_ff) {
  struct can_frame f = {0};
  f.can_dlc = 8;
  f.data[0] = 0x10; /* PCI: FF, high nibble = 1 */
  f.data[1] = 0x0A; /* length LSB = 10 */
  ck_assert_int_eq(uds_tp_frame_type(&f), UDS_TP_FRAME_FF);
}
END_TEST

START_TEST(test_frame_type_cf) {
  struct can_frame f = {0};
  f.can_dlc = 8;
  f.data[0] = 0x21; /* PCI: CF, SN=1 */
  ck_assert_int_eq(uds_tp_frame_type(&f), UDS_TP_FRAME_CF);
}
END_TEST

START_TEST(test_frame_type_fc) {
  struct can_frame f = {0};
  f.can_dlc = 3;
  f.data[0] = 0x30; /* PCI: FC, CTS */
  ck_assert_int_eq(uds_tp_frame_type(&f), UDS_TP_FRAME_FC);
}
END_TEST

START_TEST(test_frame_type_null) {
  ck_assert_int_eq(uds_tp_frame_type(NULL), UDS_TP_FRAME_UNKNOWN);
}
END_TEST

START_TEST(test_frame_type_zero_dlc) {
  struct can_frame f = {0};
  f.can_dlc = 0;
  f.data[0] = 0x01;
  ck_assert_int_eq(uds_tp_frame_type(&f), UDS_TP_FRAME_UNKNOWN);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 2: Single Frame encode / decode
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_encode_sf_basic) {
  uint8_t data[] = {0x22, 0xF1, 0x90};
  struct can_frame f;
  int rc = uds_tp_encode_sf(&f, 0x601U, data, 3);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.can_id, 0x601U);
  ck_assert_uint_eq(f.can_dlc, 4U);
  ck_assert_uint_eq(f.data[0], 0x03U); /* PCI: length 3 */
  ck_assert_int_eq(memcmp(&f.data[1], data, 3), 0);
}
END_TEST

START_TEST(test_encode_sf_max_len) {
  uint8_t data[7] = {1, 2, 3, 4, 5, 6, 7};
  struct can_frame f;
  int rc = uds_tp_encode_sf(&f, 0x601U, data, 7);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.can_dlc, 8U);
  ck_assert_uint_eq(f.data[0], 0x07U);
  ck_assert_int_eq(memcmp(&f.data[1], data, 7), 0);
}
END_TEST

START_TEST(test_encode_sf_len_zero) {
  uint8_t data = 0xAA;
  struct can_frame f;
  ck_assert_int_eq(uds_tp_encode_sf(&f, 0x601U, &data, 0), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_encode_sf_len_too_large) {
  uint8_t data[8] = {0};
  struct can_frame f;
  ck_assert_int_eq(uds_tp_encode_sf(&f, 0x601U, data, 8), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_encode_sf_null_out) {
  uint8_t data[] = {0x01};
  ck_assert_int_eq(uds_tp_encode_sf(NULL, 0x601U, data, 1), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_encode_sf_null_data) {
  struct can_frame f;
  ck_assert_int_eq(uds_tp_encode_sf(&f, 0x601U, NULL, 1), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_decode_sf_basic) {
  uint8_t payload[] = {0xAB, 0xCD, 0xEF};
  struct can_frame f;
  uds_tp_encode_sf(&f, 0x601U, payload, 3);

  uint8_t buf[8];
  size_t out_len = 0;
  int rc = uds_tp_decode_sf(&f, buf, sizeof(buf), &out_len);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(out_len, 3U);
  ck_assert_int_eq(memcmp(buf, payload, 3), 0);
}
END_TEST

START_TEST(test_decode_sf_buf_too_small) {
  uint8_t payload[] = {1, 2, 3, 4, 5};
  struct can_frame f;
  uds_tp_encode_sf(&f, 0x601U, payload, 5);

  uint8_t buf[3];
  size_t out_len = 0;
  ck_assert_int_eq(uds_tp_decode_sf(&f, buf, 3, &out_len), UDS_TP_ERR_BUF_FULL);
}
END_TEST

START_TEST(test_decode_sf_wrong_type) {
  struct can_frame f = {0};
  f.can_dlc = 8;
  f.data[0] = 0x10; /* FF, not SF */
  uint8_t buf[8];
  size_t out_len = 0;
  ck_assert_int_eq(uds_tp_decode_sf(&f, buf, 8, &out_len),
                   UDS_TP_ERR_FRAME_TYPE);
}
END_TEST

START_TEST(test_sf_roundtrip) {
  uint8_t orig[] = {0x10, 0x02}; /* DSC: programming session */
  struct can_frame f;
  uds_tp_encode_sf(&f, 0x700U, orig, sizeof(orig));

  uint8_t recovered[8];
  size_t recovered_len = 0;
  int rc = uds_tp_decode_sf(&f, recovered, sizeof(recovered), &recovered_len);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(recovered_len, sizeof(orig));
  ck_assert_int_eq(memcmp(recovered, orig, sizeof(orig)), 0);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 3: First Frame encode / decode
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_encode_ff_basic) {
  uint8_t data[20];
  for (int i = 0; i < 20; i++) {
    data[i] = (uint8_t)i;
  }
  struct can_frame f;
  int rc = uds_tp_encode_ff(&f, 0x601U, data, 20U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.can_dlc, 8U);
  /* PCI: 0x10 | (20 >> 8) = 0x10, 20 & 0xFF = 0x14 */
  ck_assert_uint_eq(f.data[0], 0x10U);
  ck_assert_uint_eq(f.data[1], 0x14U);
  /* First 6 data bytes */
  ck_assert_int_eq(memcmp(&f.data[2], data, UDS_TP_FF_DATA_BYTES), 0);
}
END_TEST

START_TEST(test_encode_ff_max_len) {
  uint8_t data[UDS_TP_MAX_PDU_LEN];
  memset(data, 0x5A, sizeof(data));
  struct can_frame f;
  int rc = uds_tp_encode_ff(&f, 0x601U, data, UDS_TP_MAX_PDU_LEN);
  ck_assert_int_eq(rc, UDS_TP_OK);
  /* PCI high nibble: 0x1F = 0x10 | (4095 >> 8 = 0x0F) */
  ck_assert_uint_eq(f.data[0], 0x1FU);
  ck_assert_uint_eq(f.data[1], 0xFFU);
}
END_TEST

START_TEST(test_encode_ff_too_short) {
  uint8_t data[8] = {0};
  struct can_frame f;
  /* total_len = 7 < 8 → PDU_LEN error */
  ck_assert_int_eq(uds_tp_encode_ff(&f, 0x601U, data, 7U), UDS_TP_ERR_PDU_LEN);
}
END_TEST

START_TEST(test_encode_ff_too_long) {
  uint8_t data[8] = {0};
  struct can_frame f;
  ck_assert_int_eq(uds_tp_encode_ff(&f, 0x601U, data, 4096U),
                   UDS_TP_ERR_PDU_LEN);
}
END_TEST

START_TEST(test_decode_ff_basic) {
  uint8_t data[20];
  for (int i = 0; i < 20; i++) {
    data[i] = (uint8_t)(i + 1);
  }
  struct can_frame f;
  uds_tp_encode_ff(&f, 0x601U, data, 20U);

  uint8_t buf[UDS_TP_FF_DATA_BYTES];
  uint32_t total_len = 0U;
  int rc = uds_tp_decode_ff(&f, buf, sizeof(buf), &total_len);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(total_len, 20U);
  ck_assert_int_eq(memcmp(buf, data, UDS_TP_FF_DATA_BYTES), 0);
}
END_TEST

START_TEST(test_decode_ff_wrong_type) {
  struct can_frame f = {0};
  f.can_dlc = 4;
  f.data[0] = 0x03; /* SF, not FF */
  uint8_t buf[8];
  uint32_t total_len = 0U;
  ck_assert_int_eq(uds_tp_decode_ff(&f, buf, sizeof(buf), &total_len),
                   UDS_TP_ERR_FRAME_TYPE);
}
END_TEST

START_TEST(test_ff_roundtrip) {
  /* Build a 50-byte PDU, check FF header and first 6 bytes round-trip */
  uint8_t data[50];
  for (int i = 0; i < 50; i++) {
    data[i] = (uint8_t)(0x10 + i);
  }
  struct can_frame f;
  uds_tp_encode_ff(&f, 0x681U, data, 50U);

  uint8_t recovered[UDS_TP_FF_DATA_BYTES];
  uint32_t total_len = 0U;
  int rc = uds_tp_decode_ff(&f, recovered, sizeof(recovered), &total_len);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(total_len, 50U);
  ck_assert_int_eq(memcmp(recovered, data, UDS_TP_FF_DATA_BYTES), 0);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 4: Consecutive Frame encode / decode
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_encode_cf_basic) {
  uint8_t chunk[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11};
  struct can_frame f;
  int rc = uds_tp_encode_cf(&f, 0x601U, chunk, 7U, 1U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.can_dlc, 8U);
  ck_assert_uint_eq(f.data[0], 0x21U); /* PCI: CF, SN=1 */
  ck_assert_int_eq(memcmp(&f.data[1], chunk, 7), 0);
}
END_TEST

START_TEST(test_encode_cf_sn_wrap) {
  uint8_t chunk[7] = {0};
  struct can_frame f;
  /* SN = 15 → PCI = 0x2F */
  uds_tp_encode_cf(&f, 0x601U, chunk, 7U, 15U);
  ck_assert_uint_eq(f.data[0], 0x2FU);
  /* SN = 16: lower nibble = 0 → PCI = 0x20 */
  uds_tp_encode_cf(&f, 0x601U, chunk, 7U, 16U);
  ck_assert_uint_eq(f.data[0], 0x20U);
}
END_TEST

START_TEST(test_encode_cf_partial_chunk) {
  uint8_t chunk[] = {0x11, 0x22}; /* only 2 bytes */
  struct can_frame f;
  int rc = uds_tp_encode_cf(&f, 0x601U, chunk, 2U, 3U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.data[1], 0x11U);
  ck_assert_uint_eq(f.data[2], 0x22U);
  /* Remaining bytes are zero-padded */
  ck_assert_uint_eq(f.data[3], 0x00U);
}
END_TEST

START_TEST(test_encode_cf_len_zero) {
  uint8_t chunk[7] = {0};
  struct can_frame f;
  ck_assert_int_eq(uds_tp_encode_cf(&f, 0x601U, chunk, 0U, 1U),
                   UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_decode_cf_basic) {
  uint8_t chunk[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
  struct can_frame f;
  uds_tp_encode_cf(&f, 0x601U, chunk, 7U, 2U);

  uint8_t buf[7];
  uint8_t sn = 0U;
  int rc = uds_tp_decode_cf(&f, &sn, buf, sizeof(buf), 7U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(sn, 2U);
  ck_assert_int_eq(memcmp(buf, chunk, 7), 0);
}
END_TEST

START_TEST(test_decode_cf_sn_extraction) {
  uint8_t chunk[7] = {0};
  struct can_frame f;
  uint8_t sn_out = 0xFF;

  for (uint8_t sn = 0; sn <= 15; sn++) {
    uds_tp_encode_cf(&f, 0x601U, chunk, 7U, sn);
    uds_tp_decode_cf(&f, &sn_out, chunk, sizeof(chunk), 7U);
    ck_assert_uint_eq(sn_out, sn);
  }
}
END_TEST

START_TEST(test_decode_cf_wrong_type) {
  struct can_frame f = {0};
  f.can_dlc = 3;
  f.data[0] = 0x30; /* FC, not CF */
  uint8_t buf[7];
  uint8_t sn = 0;
  ck_assert_int_eq(uds_tp_decode_cf(&f, &sn, buf, sizeof(buf), 7U),
                   UDS_TP_ERR_FRAME_TYPE);
}
END_TEST

START_TEST(test_cf_roundtrip) {
  uint8_t orig[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0x01};
  struct can_frame f;
  uds_tp_encode_cf(&f, 0x681U, orig, 7U, 5U);

  uint8_t recovered[7];
  uint8_t sn = 0U;
  int rc = uds_tp_decode_cf(&f, &sn, recovered, sizeof(recovered), 7U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(sn, 5U);
  ck_assert_int_eq(memcmp(recovered, orig, 7), 0);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 5: Flow Control encode / decode
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_encode_fc_cts) {
  struct can_frame f;
  int rc = uds_tp_encode_fc(&f, 0x681U, UDS_TP_FC_CTS, 0U, 0U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.can_dlc, 3U);
  ck_assert_uint_eq(f.data[0], 0x30U); /* 0x30 | 0 = CTS */
  ck_assert_uint_eq(f.data[1], 0x00U);
  ck_assert_uint_eq(f.data[2], 0x00U);
}
END_TEST

START_TEST(test_encode_fc_wait) {
  struct can_frame f;
  uds_tp_encode_fc(&f, 0x681U, UDS_TP_FC_WAIT, 0U, 0U);
  ck_assert_uint_eq(f.data[0], 0x31U); /* 0x30 | 1 = Wait */
}
END_TEST

START_TEST(test_encode_fc_overflow) {
  struct can_frame f;
  uds_tp_encode_fc(&f, 0x681U, UDS_TP_FC_OVERFLOW, 0U, 0U);
  ck_assert_uint_eq(f.data[0], 0x32U); /* 0x30 | 2 = Overflow */
}
END_TEST

START_TEST(test_encode_fc_bs_stmin) {
  struct can_frame f;
  uds_tp_encode_fc(&f, 0x681U, UDS_TP_FC_CTS, 8U, 25U);
  ck_assert_uint_eq(f.data[1], 8U);
  ck_assert_uint_eq(f.data[2], 25U);
}
END_TEST

START_TEST(test_decode_fc_cts) {
  struct can_frame f;
  uds_tp_encode_fc(&f, 0x681U, UDS_TP_FC_CTS, 10U, 5U);

  UdsTpFlowStatus fs;
  uint8_t bs, stmin;
  int rc = uds_tp_decode_fc(&f, &fs, &bs, &stmin);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_int_eq((int)fs, (int)UDS_TP_FC_CTS);
  ck_assert_uint_eq(bs, 10U);
  ck_assert_uint_eq(stmin, 5U);
}
END_TEST

START_TEST(test_decode_fc_wait) {
  struct can_frame f;
  uds_tp_encode_fc(&f, 0x681U, UDS_TP_FC_WAIT, 0U, 0U);
  UdsTpFlowStatus fs;
  uint8_t bs, stmin;
  uds_tp_decode_fc(&f, &fs, &bs, &stmin);
  ck_assert_int_eq((int)fs, (int)UDS_TP_FC_WAIT);
}
END_TEST

START_TEST(test_decode_fc_wrong_type) {
  struct can_frame f = {0};
  f.can_dlc = 4;
  f.data[0] = 0x03; /* SF, not FC */
  UdsTpFlowStatus fs;
  uint8_t bs, stmin;
  ck_assert_int_eq(uds_tp_decode_fc(&f, &fs, &bs, &stmin),
                   UDS_TP_ERR_FRAME_TYPE);
}
END_TEST

START_TEST(test_fc_roundtrip) {
  struct can_frame f;
  uds_tp_encode_fc(&f, 0x681U, UDS_TP_FC_CTS, 15U, 0x0AU);

  UdsTpFlowStatus fs;
  uint8_t bs, stmin;
  int rc = uds_tp_decode_fc(&f, &fs, &bs, &stmin);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_int_eq((int)fs, (int)UDS_TP_FC_CTS);
  ck_assert_uint_eq(bs, 15U);
  ck_assert_uint_eq(stmin, 0x0AU);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 6: Null-safety & parameter validation
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_rx_init_null_ch) {
  uint8_t buf[64];
  ck_assert_int_eq(uds_tp_rx_init(NULL, buf, sizeof(buf)), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_rx_init_null_buf) {
  UdsTpRxChannel ch;
  ck_assert_int_eq(uds_tp_rx_init(&ch, NULL, 64U), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_rx_init_zero_size) {
  UdsTpRxChannel ch;
  uint8_t buf[1];
  ck_assert_int_eq(uds_tp_rx_init(&ch, buf, 0U), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_rx_init_ok) {
  UdsTpRxChannel ch;
  uint8_t buf[128];
  int rc = uds_tp_rx_init(&ch, buf, sizeof(buf));
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_ptr_eq(ch.buf, buf);
  ck_assert_uint_eq(ch.buf_size, sizeof(buf));
  ck_assert(!ch.in_progress);
}
END_TEST

START_TEST(test_strerror_ok) {
  const char *s = uds_tp_strerror(UDS_TP_OK);
  ck_assert_ptr_nonnull(s);
  ck_assert_str_eq(s, "Success");
}
END_TEST

START_TEST(test_strerror_unknown) {
  const char *s = uds_tp_strerror(-999);
  ck_assert_ptr_nonnull(s);
}
END_TEST

START_TEST(test_send_null_sock) {
  UdsTpConfig cfg = UDS_TP_DEFAULT_CONFIG;
  uint8_t data[] = {0x01};
  ck_assert_int_eq(uds_tp_send(NULL, 0x601U, data, 1U, &cfg), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_send_null_data) {
  UdsCanSocket s = {.fd = -1};
  UdsTpConfig cfg = UDS_TP_DEFAULT_CONFIG;
  ck_assert_int_eq(uds_tp_send(&s, 0x601U, NULL, 1U, &cfg), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_send_null_cfg) {
  UdsCanSocket s = {.fd = -1};
  uint8_t data[] = {0x01};
  ck_assert_int_eq(uds_tp_send(&s, 0x601U, data, 1U, NULL), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_send_zero_len) {
  UdsCanSocket s = {.fd = -1};
  UdsTpConfig cfg = UDS_TP_DEFAULT_CONFIG;
  uint8_t data[] = {0x00};
  ck_assert_int_eq(uds_tp_send(&s, 0x601U, data, 0U, &cfg), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_recv_null_sock) {
  UdsTpConfig cfg = UDS_TP_DEFAULT_CONFIG;
  uint8_t buf[64];
  size_t len = 0;
  ck_assert_int_eq(uds_tp_recv(NULL, 0x681U, buf, sizeof(buf), &len, &cfg),
                   UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_recv_null_buf) {
  UdsCanSocket s = {.fd = -1};
  UdsTpConfig cfg = UDS_TP_DEFAULT_CONFIG;
  size_t len = 0;
  ck_assert_int_eq(uds_tp_recv(&s, 0x681U, NULL, 64U, &len, &cfg),
                   UDS_TP_ERR_PARAM);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 7: vCAN integration — skipped when vcan0 unavailable
 * ════════════════════════════════════════════════════════════════════════════
 */

/**
 * Helper: open two sockets (one for TX, one for RX) on vcan0.
 * Sets a CAN ID filter on the RX socket to accept only rx_can_id.
 */
static int open_tx_rx(UdsCanSocket *tx, UdsCanSocket *rx,
                      uint32_t tx_can_id, uint32_t rx_can_id) {
  if (uds_can_open(tx, "vcan0") != UDS_CAN_OK) {
    return -1;
  }
  if (uds_can_open(rx, "vcan0") != UDS_CAN_OK) {
    uds_can_close(tx);
    return -1;
  }
  /* Filter RX socket to only see the expected CAN ID */
  UdsCanFilter f = {.can_id = rx_can_id, .can_mask = 0x7FFU};
  (void)tx_can_id;
  uds_can_set_filter(rx, &f, 1U);
  return 0;
}

START_TEST(test_vcan_send_recv_sf) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  UdsCanSocket tx_sock, rx_sock;
  if (open_tx_rx(&tx_sock, &rx_sock, 0x601U, 0x601U) != 0) {
    ck_abort_msg("Failed to open vCAN sockets");
  }

  UdsTpConfig cfg = UDS_TP_DEFAULT_CONFIG;

  uint8_t send_data[] = {0x22, 0xF1, 0x90}; /* ReadDataByIdentifier VIN */
  int rc = uds_tp_send(&tx_sock, 0x601U, send_data, sizeof(send_data), &cfg);
  ck_assert_int_eq(rc, UDS_TP_OK);

  uint8_t recv_buf[64];
  size_t recv_len = 0;
  rc = uds_tp_recv(&rx_sock, 0x681U, recv_buf, sizeof(recv_buf), &recv_len,
                   &cfg);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(recv_len, sizeof(send_data));
  ck_assert_int_eq(memcmp(recv_buf, send_data, sizeof(send_data)), 0);

  uds_can_close(&tx_sock);
  uds_can_close(&rx_sock);
}
END_TEST

START_TEST(test_vcan_send_recv_sf_max) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  UdsCanSocket tx_sock, rx_sock;
  if (open_tx_rx(&tx_sock, &rx_sock, 0x601U, 0x601U) != 0) {
    ck_abort_msg("Failed to open vCAN sockets");
  }

  UdsTpConfig cfg = UDS_TP_DEFAULT_CONFIG;
  uint8_t send_data[7];
  for (int i = 0; i < 7; i++) {
    send_data[i] = (uint8_t)(0x10 + i);
  }

  ck_assert_int_eq(
      uds_tp_send(&tx_sock, 0x601U, send_data, sizeof(send_data), &cfg),
      UDS_TP_OK);

  uint8_t recv_buf[64];
  size_t recv_len = 0;
  ck_assert_int_eq(uds_tp_recv(&rx_sock, 0x681U, recv_buf, sizeof(recv_buf),
                                &recv_len, &cfg),
                   UDS_TP_OK);
  ck_assert_uint_eq(recv_len, 7U);
  ck_assert_int_eq(memcmp(recv_buf, send_data, 7), 0);

  uds_can_close(&tx_sock);
  uds_can_close(&rx_sock);
}
END_TEST

START_TEST(test_vcan_send_recv_multiframe) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  /*
   * Multi-frame test: tester sends a 20-byte PDU (FF + 3 CFs) to ECU.
   * We need two pairs of sockets:
   *   tester_tx → sends data on 0x601, listens for FC on 0x681
   *   ecu_rx    → listens for data on 0x601, sends FC on 0x681
   */
  UdsCanSocket tester_tx, ecu_rx;
  if (uds_can_open(&tester_tx, "vcan0") != UDS_CAN_OK) {
    ck_abort_msg("Could not open tester_tx socket");
  }
  if (uds_can_open(&ecu_rx, "vcan0") != UDS_CAN_OK) {
    uds_can_close(&tester_tx);
    ck_abort_msg("Could not open ecu_rx socket");
  }

  /* tester_tx listens for FC on 0x681 */
  UdsCanFilter tester_filter = {.can_id = 0x681U, .can_mask = 0x7FFU};
  uds_can_set_filter(&tester_tx, &tester_filter, 1U);

  /* ecu_rx listens for data on 0x601 */
  UdsCanFilter ecu_filter = {.can_id = 0x601U, .can_mask = 0x7FFU};
  uds_can_set_filter(&ecu_rx, &ecu_filter, 1U);

  /* Build a 20-byte payload */
  uint8_t send_data[20];
  for (int i = 0; i < 20; i++) {
    send_data[i] = (uint8_t)(i + 1);
  }

  /* Launch "ECU" recv in the same thread by interleaving with tester send.
   * Since send (FF) and recv (FC) happen synchronously, we run them
   * concurrently by using separate sockets and relying on SocketCAN
   * loopback.  For a single-threaded test, we must send FF first, then
   * recv on ecu_rx (which gets FF), send FC on ecu_rx, then tester_tx
   * reads FC and sends CFs.  uds_tp_send() and uds_tp_recv() are blocking,
   * so we cannot call them simultaneously in one thread.
   *
   * Instead we exercise the encode/decode helpers directly for the
   * multi-frame logic in this unit test, and rely on the integration
   * test suite for end-to-end multi-frame verification with threads.
   *
   * Here we verify: FF sent by tester_tx is received by ecu_rx, and
   * FC sent by ecu_rx is received by tester_tx.
   */

  /* Step 1: Tester sends FF */
  struct can_frame ff;
  ck_assert_int_eq(uds_tp_encode_ff(&ff, 0x601U, send_data, 20U), UDS_TP_OK);
  ck_assert_int_eq(uds_can_send(&tester_tx, &ff), UDS_CAN_OK);

  /* Step 2: ECU receives FF */
  struct can_frame recv_ff;
  ck_assert_int_eq(uds_can_recv(&ecu_rx, &recv_ff, 500U), UDS_CAN_OK);
  ck_assert_int_eq(uds_tp_frame_type(&recv_ff), UDS_TP_FRAME_FF);

  uint8_t ecu_buf[64];
  uint32_t total_len = 0U;
  ck_assert_int_eq(
      uds_tp_decode_ff(&recv_ff, ecu_buf, sizeof(ecu_buf), &total_len),
      UDS_TP_OK);
  ck_assert_uint_eq(total_len, 20U);
  ck_assert_int_eq(memcmp(ecu_buf, send_data, UDS_TP_FF_DATA_BYTES), 0);

  /* Step 3: ECU sends FC(CTS) */
  struct can_frame fc_frame;
  ck_assert_int_eq(
      uds_tp_encode_fc(&fc_frame, 0x681U, UDS_TP_FC_CTS, 0U, 0U), UDS_TP_OK);
  ck_assert_int_eq(uds_can_send(&ecu_rx, &fc_frame), UDS_CAN_OK);

  /* Step 4: Tester receives FC */
  struct can_frame recv_fc;
  ck_assert_int_eq(uds_can_recv(&tester_tx, &recv_fc, 500U), UDS_CAN_OK);
  ck_assert_int_eq(uds_tp_frame_type(&recv_fc), UDS_TP_FRAME_FC);

  UdsTpFlowStatus fs;
  uint8_t bs, stmin;
  ck_assert_int_eq(uds_tp_decode_fc(&recv_fc, &fs, &bs, &stmin), UDS_TP_OK);
  ck_assert_int_eq((int)fs, (int)UDS_TP_FC_CTS);

  /* Step 5: Tester sends 3 CFs (6 + 7 + 7 = 20 bytes) */
  uint32_t offset = UDS_TP_FF_DATA_BYTES; /* 6 bytes already in FF */
  uint8_t sn = 1U;
  while (offset < 20U) {
    size_t remaining = 20U - offset;
    size_t chunk =
        (remaining > UDS_TP_CF_DATA_BYTES) ? UDS_TP_CF_DATA_BYTES : remaining;
    struct can_frame cf;
    ck_assert_int_eq(
        uds_tp_encode_cf(&cf, 0x601U, &send_data[offset], chunk, sn),
        UDS_TP_OK);
    ck_assert_int_eq(uds_can_send(&tester_tx, &cf), UDS_CAN_OK);
    sn = (uint8_t)((sn + 1U) & 0x0FU);
    offset += chunk;
  }

  /* Step 6: ECU receives all 3 CFs */
  uint32_t ecu_received = UDS_TP_FF_DATA_BYTES;
  uint8_t expected_sn = 1U;
  while (ecu_received < 20U) {
    struct can_frame cf;
    ck_assert_int_eq(uds_can_recv(&ecu_rx, &cf, 500U), UDS_CAN_OK);
    ck_assert_int_eq(uds_tp_frame_type(&cf), UDS_TP_FRAME_CF);

    uint32_t remaining = 20U - ecu_received;
    size_t chunk = (remaining > UDS_TP_CF_DATA_BYTES) ? UDS_TP_CF_DATA_BYTES
                                                       : (size_t)remaining;
    uint8_t cf_sn = 0U;
    ck_assert_int_eq(
        uds_tp_decode_cf(&cf, &cf_sn, &ecu_buf[ecu_received],
                         sizeof(ecu_buf) - ecu_received, chunk),
        UDS_TP_OK);
    ck_assert_uint_eq(cf_sn, expected_sn);
    ecu_received += (uint32_t)chunk;
    expected_sn = (uint8_t)((expected_sn + 1U) & 0x0FU);
  }

  /* Verify full payload was reassembled correctly */
  ck_assert_int_eq(memcmp(ecu_buf, send_data, 20U), 0);

  uds_can_close(&tester_tx);
  uds_can_close(&ecu_rx);
}
END_TEST

START_TEST(test_vcan_recv_timeout_sf) {
  if (!vcan0_available()) {
    printf("  [SKIP] vcan0 not available\n");
    return;
  }

  UdsCanSocket s;
  ck_assert_int_eq(uds_can_open(&s, "vcan0"), UDS_CAN_OK);

  /* Filter to a CAN ID that nobody will send */
  UdsCanFilter deny = {.can_id = 0x7FFU, .can_mask = 0x7FFU};
  uds_can_set_filter(&s, &deny, 1U);

  UdsTpConfig cfg = {.timeout_ms = 50U, .block_size = 0U, .st_min = 0U};
  uint8_t buf[64];
  size_t len = 0;
  int rc = uds_tp_recv(&s, 0x681U, buf, sizeof(buf), &len, &cfg);
  ck_assert_int_eq(rc, UDS_TP_ERR_TIMEOUT);

  uds_can_close(&s);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 8: CAN FD encode / decode (compiled only when CAN_FD_ENABLED)
 * ════════════════════════════════════════════════════════════════════════════
 */

#ifdef CAN_FD_ENABLED

/* ── CAN FD frame type classification ───────────────────────────── */

START_TEST(test_fd_frame_type_sf_classic) {
  struct canfd_frame f = {0};
  f.len = 4;
  f.data[0] = 0x03; /* SF, classic 1-byte PCI */
  ck_assert_int_eq(uds_tp_frame_type_fd(&f), UDS_TP_FRAME_SF);
}
END_TEST

START_TEST(test_fd_frame_type_sf_escape) {
  struct canfd_frame f = {0};
  f.len = 12;
  f.data[0] = 0x00; /* SF escape: data[0]=0, data[1]=SF_DL */
  f.data[1] = 0x0A;
  ck_assert_int_eq(uds_tp_frame_type_fd(&f), UDS_TP_FRAME_SF);
}
END_TEST

START_TEST(test_fd_frame_type_ff) {
  struct canfd_frame f = {0};
  f.len = 64;
  f.data[0] = 0x10;
  f.data[1] = 0x50; /* FF_DL = 80 */
  ck_assert_int_eq(uds_tp_frame_type_fd(&f), UDS_TP_FRAME_FF);
}
END_TEST

START_TEST(test_fd_frame_type_cf) {
  struct canfd_frame f = {0};
  f.len = 64;
  f.data[0] = 0x21; /* CF, SN=1 */
  ck_assert_int_eq(uds_tp_frame_type_fd(&f), UDS_TP_FRAME_CF);
}
END_TEST

START_TEST(test_fd_frame_type_null) {
  ck_assert_int_eq(uds_tp_frame_type_fd(NULL), UDS_TP_FRAME_UNKNOWN);
}
END_TEST

START_TEST(test_fd_frame_type_zero_len) {
  struct canfd_frame f = {0};
  f.len = 0;
  f.data[0] = 0x01;
  ck_assert_int_eq(uds_tp_frame_type_fd(&f), UDS_TP_FRAME_UNKNOWN);
}
END_TEST

/* ── CAN FD Single Frame encode / decode ───────────────────────── */

START_TEST(test_fd_encode_sf_classic_len) {
  /* Payload ≤ 7 bytes: uses the classic 1-byte PCI */
  uint8_t data[] = {0x22, 0xF1, 0x90};
  struct canfd_frame f;
  int rc = uds_tp_encode_sf_fd(&f, 0x601U, data, 3);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.can_id, 0x601U);
  ck_assert_uint_eq(f.len, 4U);         /* 1 PCI + 3 data */
  ck_assert_uint_eq(f.data[0], 0x03U);  /* classic SF PCI */
  ck_assert_int_eq(memcmp(&f.data[1], data, 3), 0);
}
END_TEST

START_TEST(test_fd_encode_sf_escape_8_bytes) {
  /* Payload = 8 bytes: must use the CAN FD escape sequence */
  uint8_t data[8];
  for (int i = 0; i < 8; i++) {
    data[i] = (uint8_t)i;
  }
  struct canfd_frame f;
  int rc = uds_tp_encode_sf_fd(&f, 0x601U, data, 8);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.len, 10U);        /* 2 PCI + 8 data */
  ck_assert_uint_eq(f.data[0], 0x00U);  /* escape byte */
  ck_assert_uint_eq(f.data[1], 0x08U);  /* SF_DL = 8 */
  ck_assert_int_eq(memcmp(&f.data[2], data, 8), 0);
}
END_TEST

START_TEST(test_fd_encode_sf_escape_max) {
  /* Payload = 62 bytes (UDS_TP_CANFD_SF_MAX_DATA) */
  uint8_t data[UDS_TP_CANFD_SF_MAX_DATA];
  memset(data, 0x5A, sizeof(data));
  struct canfd_frame f;
  int rc = uds_tp_encode_sf_fd(&f, 0x601U, data, UDS_TP_CANFD_SF_MAX_DATA);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.len, 64U);        /* 2 PCI + 62 data */
  ck_assert_uint_eq(f.data[0], 0x00U);
  ck_assert_uint_eq(f.data[1], UDS_TP_CANFD_SF_MAX_DATA);
  ck_assert_int_eq(memcmp(&f.data[2], data, UDS_TP_CANFD_SF_MAX_DATA), 0);
}
END_TEST

START_TEST(test_fd_encode_sf_len_zero) {
  uint8_t data = 0xAA;
  struct canfd_frame f;
  ck_assert_int_eq(uds_tp_encode_sf_fd(&f, 0x601U, &data, 0), UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_fd_encode_sf_len_too_large) {
  uint8_t data[63] = {0};
  struct canfd_frame f;
  ck_assert_int_eq(uds_tp_encode_sf_fd(&f, 0x601U, data, 63),
                   UDS_TP_ERR_PARAM);
}
END_TEST

START_TEST(test_fd_decode_sf_classic) {
  uint8_t payload[] = {0xAB, 0xCD, 0xEF};
  struct canfd_frame f;
  uds_tp_encode_sf_fd(&f, 0x601U, payload, 3);

  uint8_t buf[64];
  size_t out_len = 0;
  int rc = uds_tp_decode_sf_fd(&f, buf, sizeof(buf), &out_len);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(out_len, 3U);
  ck_assert_int_eq(memcmp(buf, payload, 3), 0);
}
END_TEST

START_TEST(test_fd_decode_sf_escape) {
  uint8_t payload[10];
  for (int i = 0; i < 10; i++) {
    payload[i] = (uint8_t)(0x10 + i);
  }
  struct canfd_frame f;
  uds_tp_encode_sf_fd(&f, 0x601U, payload, 10);

  uint8_t buf[64];
  size_t out_len = 0;
  int rc = uds_tp_decode_sf_fd(&f, buf, sizeof(buf), &out_len);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(out_len, 10U);
  ck_assert_int_eq(memcmp(buf, payload, 10), 0);
}
END_TEST

START_TEST(test_fd_sf_roundtrip_escape_max) {
  uint8_t orig[UDS_TP_CANFD_SF_MAX_DATA];
  for (size_t i = 0; i < UDS_TP_CANFD_SF_MAX_DATA; i++) {
    orig[i] = (uint8_t)(i & 0xFFU);
  }
  struct canfd_frame f;
  uds_tp_encode_sf_fd(&f, 0x700U, orig, UDS_TP_CANFD_SF_MAX_DATA);

  uint8_t recovered[UDS_TP_CANFD_SF_MAX_DATA];
  size_t recovered_len = 0;
  int rc = uds_tp_decode_sf_fd(&f, recovered, sizeof(recovered), &recovered_len);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(recovered_len, UDS_TP_CANFD_SF_MAX_DATA);
  ck_assert_int_eq(memcmp(recovered, orig, UDS_TP_CANFD_SF_MAX_DATA), 0);
}
END_TEST

/* ── CAN FD First Frame encode / decode ────────────────────────── */

START_TEST(test_fd_encode_ff_regular) {
  /* Regular FF: FF_DL = 100 (≤ 4095) */
  uint8_t data[100];
  for (int i = 0; i < 100; i++) {
    data[i] = (uint8_t)i;
  }
  struct canfd_frame f;
  int rc = uds_tp_encode_ff_fd(&f, 0x601U, data, 100U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  /* PCI: 0x10 | (100 >> 8) = 0x10, 100 & 0xFF = 0x64 */
  ck_assert_uint_eq(f.data[0], 0x10U);
  ck_assert_uint_eq(f.data[1], 0x64U);
  /* First 62 data bytes embedded */
  ck_assert_int_eq(memcmp(&f.data[2], data, UDS_TP_CANFD_FF_DATA_BYTES), 0);
  ck_assert_uint_eq(f.len, 2U + UDS_TP_CANFD_FF_DATA_BYTES);
}
END_TEST

START_TEST(test_fd_encode_ff_extended) {
  /* Extended FF: FF_DL = 5000 (> 4095) */
  uint8_t data[100];
  memset(data, 0xAA, sizeof(data));
  struct canfd_frame f;
  int rc = uds_tp_encode_ff_fd(&f, 0x601U, data, 5000U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  /* Extended PCI: data[0]=0x10, data[1]=0x00, data[2..5]=FF_DL big-endian */
  ck_assert_uint_eq(f.data[0], 0x10U);
  ck_assert_uint_eq(f.data[1], 0x00U);
  ck_assert_uint_eq(f.data[2], 0x00U);
  ck_assert_uint_eq(f.data[3], 0x00U);
  ck_assert_uint_eq(f.data[4], 0x13U); /* 5000 >> 8 = 0x13 */
  ck_assert_uint_eq(f.data[5], 0x88U); /* 5000 & 0xFF = 0x88 */
  /* First 58 data bytes embedded */
  ck_assert_int_eq(memcmp(&f.data[6], data, UDS_TP_CANFD_FF_EXT_DATA_BYTES), 0);
  ck_assert_uint_eq(f.len, 6U + UDS_TP_CANFD_FF_EXT_DATA_BYTES);
}
END_TEST

START_TEST(test_fd_decode_ff_regular) {
  uint8_t data[100];
  for (int i = 0; i < 100; i++) {
    data[i] = (uint8_t)(i + 1);
  }
  struct canfd_frame f;
  uds_tp_encode_ff_fd(&f, 0x601U, data, 100U);

  uint8_t buf[UDS_TP_CANFD_FF_DATA_BYTES];
  uint32_t total_len = 0U;
  size_t ff_data_bytes = 0U;
  int rc = uds_tp_decode_ff_fd(&f, buf, sizeof(buf), &total_len, &ff_data_bytes);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(total_len, 100U);
  ck_assert_uint_eq(ff_data_bytes, UDS_TP_CANFD_FF_DATA_BYTES);
  ck_assert_int_eq(memcmp(buf, data, UDS_TP_CANFD_FF_DATA_BYTES), 0);
}
END_TEST

START_TEST(test_fd_decode_ff_extended) {
  uint8_t data[100];
  memset(data, 0xBB, sizeof(data));
  struct canfd_frame f;
  uds_tp_encode_ff_fd(&f, 0x601U, data, 5000U);

  uint8_t buf[UDS_TP_CANFD_FF_EXT_DATA_BYTES];
  uint32_t total_len = 0U;
  size_t ff_data_bytes = 0U;
  int rc = uds_tp_decode_ff_fd(&f, buf, sizeof(buf), &total_len, &ff_data_bytes);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(total_len, 5000U);
  ck_assert_uint_eq(ff_data_bytes, UDS_TP_CANFD_FF_EXT_DATA_BYTES);
  ck_assert_int_eq(memcmp(buf, data, UDS_TP_CANFD_FF_EXT_DATA_BYTES), 0);
}
END_TEST

/* ── CAN FD Consecutive Frame encode / decode ──────────────────── */

START_TEST(test_fd_encode_cf_max) {
  /* 63-byte CF chunk */
  uint8_t chunk[UDS_TP_CANFD_CF_DATA_BYTES];
  memset(chunk, 0x77, sizeof(chunk));
  struct canfd_frame f;
  int rc = uds_tp_encode_cf_fd(&f, 0x601U, chunk, UDS_TP_CANFD_CF_DATA_BYTES, 1U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.len, 64U);
  ck_assert_uint_eq(f.data[0], 0x21U); /* CF PCI: SN=1 */
  ck_assert_int_eq(memcmp(&f.data[1], chunk, UDS_TP_CANFD_CF_DATA_BYTES), 0);
}
END_TEST

START_TEST(test_fd_decode_cf_max) {
  uint8_t chunk[UDS_TP_CANFD_CF_DATA_BYTES];
  for (size_t i = 0; i < UDS_TP_CANFD_CF_DATA_BYTES; i++) {
    chunk[i] = (uint8_t)(i & 0xFFU);
  }
  struct canfd_frame f;
  uds_tp_encode_cf_fd(&f, 0x601U, chunk, UDS_TP_CANFD_CF_DATA_BYTES, 3U);

  uint8_t buf[UDS_TP_CANFD_CF_DATA_BYTES];
  uint8_t sn = 0U;
  int rc = uds_tp_decode_cf_fd(&f, &sn, buf, sizeof(buf),
                                UDS_TP_CANFD_CF_DATA_BYTES);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(sn, 3U);
  ck_assert_int_eq(memcmp(buf, chunk, UDS_TP_CANFD_CF_DATA_BYTES), 0);
}
END_TEST

START_TEST(test_fd_cf_sn_wrap) {
  uint8_t chunk[1] = {0xAA};
  struct canfd_frame f;
  uds_tp_encode_cf_fd(&f, 0x601U, chunk, 1U, 15U);
  ck_assert_uint_eq(f.data[0], 0x2FU); /* SN=15 */
  uds_tp_encode_cf_fd(&f, 0x601U, chunk, 1U, 16U);
  ck_assert_uint_eq(f.data[0], 0x20U); /* SN=16 lower nibble = 0 */
}
END_TEST

/* ── CAN FD Flow Control encode / decode ───────────────────────── */

START_TEST(test_fd_encode_fc_cts) {
  struct canfd_frame f;
  int rc = uds_tp_encode_fc_fd(&f, 0x681U, UDS_TP_FC_CTS, 0U, 0U);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_uint_eq(f.len, 3U);
  ck_assert_uint_eq(f.data[0], 0x30U);
  ck_assert_uint_eq(f.data[1], 0x00U);
  ck_assert_uint_eq(f.data[2], 0x00U);
}
END_TEST

START_TEST(test_fd_decode_fc_cts) {
  struct canfd_frame f;
  uds_tp_encode_fc_fd(&f, 0x681U, UDS_TP_FC_CTS, 5U, 0x0AU);

  UdsTpFlowStatus fs;
  uint8_t bs, stmin;
  int rc = uds_tp_decode_fc_fd(&f, &fs, &bs, &stmin);
  ck_assert_int_eq(rc, UDS_TP_OK);
  ck_assert_int_eq((int)fs, (int)UDS_TP_FC_CTS);
  ck_assert_uint_eq(bs, 5U);
  ck_assert_uint_eq(stmin, 0x0AU);
}
END_TEST

#endif /* CAN_FD_ENABLED */

/* ════════════════════════════════════════════════════════════════════════════
 * Test suite / runner wiring
 * ════════════════════════════════════════════════════════════════════════════
 */

static Suite *iso_tp_suite(void) {
  Suite *s = suite_create("uds_tp");

  /* ── Frame type ────────────────────────────────────────────── */
  TCase *tc_type = tcase_create("frame_type");
  tcase_add_test(tc_type, test_frame_type_sf);
  tcase_add_test(tc_type, test_frame_type_ff);
  tcase_add_test(tc_type, test_frame_type_cf);
  tcase_add_test(tc_type, test_frame_type_fc);
  tcase_add_test(tc_type, test_frame_type_null);
  tcase_add_test(tc_type, test_frame_type_zero_dlc);
  suite_add_tcase(s, tc_type);

  /* ── Single Frame ──────────────────────────────────────────── */
  TCase *tc_sf = tcase_create("single_frame");
  tcase_add_test(tc_sf, test_encode_sf_basic);
  tcase_add_test(tc_sf, test_encode_sf_max_len);
  tcase_add_test(tc_sf, test_encode_sf_len_zero);
  tcase_add_test(tc_sf, test_encode_sf_len_too_large);
  tcase_add_test(tc_sf, test_encode_sf_null_out);
  tcase_add_test(tc_sf, test_encode_sf_null_data);
  tcase_add_test(tc_sf, test_decode_sf_basic);
  tcase_add_test(tc_sf, test_decode_sf_buf_too_small);
  tcase_add_test(tc_sf, test_decode_sf_wrong_type);
  tcase_add_test(tc_sf, test_sf_roundtrip);
  suite_add_tcase(s, tc_sf);

  /* ── First Frame ───────────────────────────────────────────── */
  TCase *tc_ff = tcase_create("first_frame");
  tcase_add_test(tc_ff, test_encode_ff_basic);
  tcase_add_test(tc_ff, test_encode_ff_max_len);
  tcase_add_test(tc_ff, test_encode_ff_too_short);
  tcase_add_test(tc_ff, test_encode_ff_too_long);
  tcase_add_test(tc_ff, test_decode_ff_basic);
  tcase_add_test(tc_ff, test_decode_ff_wrong_type);
  tcase_add_test(tc_ff, test_ff_roundtrip);
  suite_add_tcase(s, tc_ff);

  /* ── Consecutive Frame ─────────────────────────────────────── */
  TCase *tc_cf = tcase_create("consecutive_frame");
  tcase_add_test(tc_cf, test_encode_cf_basic);
  tcase_add_test(tc_cf, test_encode_cf_sn_wrap);
  tcase_add_test(tc_cf, test_encode_cf_partial_chunk);
  tcase_add_test(tc_cf, test_encode_cf_len_zero);
  tcase_add_test(tc_cf, test_decode_cf_basic);
  tcase_add_test(tc_cf, test_decode_cf_sn_extraction);
  tcase_add_test(tc_cf, test_decode_cf_wrong_type);
  tcase_add_test(tc_cf, test_cf_roundtrip);
  suite_add_tcase(s, tc_cf);

  /* ── Flow Control ──────────────────────────────────────────── */
  TCase *tc_fc = tcase_create("flow_control");
  tcase_add_test(tc_fc, test_encode_fc_cts);
  tcase_add_test(tc_fc, test_encode_fc_wait);
  tcase_add_test(tc_fc, test_encode_fc_overflow);
  tcase_add_test(tc_fc, test_encode_fc_bs_stmin);
  tcase_add_test(tc_fc, test_decode_fc_cts);
  tcase_add_test(tc_fc, test_decode_fc_wait);
  tcase_add_test(tc_fc, test_decode_fc_wrong_type);
  tcase_add_test(tc_fc, test_fc_roundtrip);
  suite_add_tcase(s, tc_fc);

  /* ── Null-safety ───────────────────────────────────────────── */
  TCase *tc_null = tcase_create("null_safety");
  tcase_add_test(tc_null, test_rx_init_null_ch);
  tcase_add_test(tc_null, test_rx_init_null_buf);
  tcase_add_test(tc_null, test_rx_init_zero_size);
  tcase_add_test(tc_null, test_rx_init_ok);
  tcase_add_test(tc_null, test_strerror_ok);
  tcase_add_test(tc_null, test_strerror_unknown);
  tcase_add_test(tc_null, test_send_null_sock);
  tcase_add_test(tc_null, test_send_null_data);
  tcase_add_test(tc_null, test_send_null_cfg);
  tcase_add_test(tc_null, test_send_zero_len);
  tcase_add_test(tc_null, test_recv_null_sock);
  tcase_add_test(tc_null, test_recv_null_buf);
  suite_add_tcase(s, tc_null);

  /* ── vCAN integration (auto-skipped when unavailable) ─────── */
  TCase *tc_vcan = tcase_create("vcan_integration");
  tcase_set_timeout(tc_vcan, 10);
  tcase_add_test(tc_vcan, test_vcan_send_recv_sf);
  tcase_add_test(tc_vcan, test_vcan_send_recv_sf_max);
  tcase_add_test(tc_vcan, test_vcan_send_recv_multiframe);
  tcase_add_test(tc_vcan, test_vcan_recv_timeout_sf);
  suite_add_tcase(s, tc_vcan);

#ifdef CAN_FD_ENABLED
  /* ── CAN FD encode / decode ────────────────────────────────── */
  TCase *tc_fd = tcase_create("canfd");
  tcase_add_test(tc_fd, test_fd_frame_type_sf_classic);
  tcase_add_test(tc_fd, test_fd_frame_type_sf_escape);
  tcase_add_test(tc_fd, test_fd_frame_type_ff);
  tcase_add_test(tc_fd, test_fd_frame_type_cf);
  tcase_add_test(tc_fd, test_fd_frame_type_null);
  tcase_add_test(tc_fd, test_fd_frame_type_zero_len);
  tcase_add_test(tc_fd, test_fd_encode_sf_classic_len);
  tcase_add_test(tc_fd, test_fd_encode_sf_escape_8_bytes);
  tcase_add_test(tc_fd, test_fd_encode_sf_escape_max);
  tcase_add_test(tc_fd, test_fd_encode_sf_len_zero);
  tcase_add_test(tc_fd, test_fd_encode_sf_len_too_large);
  tcase_add_test(tc_fd, test_fd_decode_sf_classic);
  tcase_add_test(tc_fd, test_fd_decode_sf_escape);
  tcase_add_test(tc_fd, test_fd_sf_roundtrip_escape_max);
  tcase_add_test(tc_fd, test_fd_encode_ff_regular);
  tcase_add_test(tc_fd, test_fd_encode_ff_extended);
  tcase_add_test(tc_fd, test_fd_decode_ff_regular);
  tcase_add_test(tc_fd, test_fd_decode_ff_extended);
  tcase_add_test(tc_fd, test_fd_encode_cf_max);
  tcase_add_test(tc_fd, test_fd_decode_cf_max);
  tcase_add_test(tc_fd, test_fd_cf_sn_wrap);
  tcase_add_test(tc_fd, test_fd_encode_fc_cts);
  tcase_add_test(tc_fd, test_fd_decode_fc_cts);
  suite_add_tcase(s, tc_fd);
#endif /* CAN_FD_ENABLED */

  return s;
}

int main(void) {
  Suite *s = iso_tp_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
