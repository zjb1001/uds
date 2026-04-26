/**
 * @file test_flash.c
 * @brief Unit tests for Phase 3: flash simulation and UDS flash programming
 *        services (0x34, 0x35, 0x36, 0x37).
 *
 * Build & run:
 *   cmake -B build && cmake --build build && cd build && ctest -V
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uds_core.h"
#include "uds_flash.h"
#include "uds_nrc.h"

/* ── Shared test fixtures ───────────────────────────────────────────────── */

static uint8_t resp[512];
static size_t  resp_len;
static uint8_t nrc;

/* Two non-protected regions used by most tests. */
static const UdsFlashRegion g_regions[] = {
    { .base_address = 0x00000000U, .size = 0x10000U, .protected = false },
    { .base_address = 0x00010000U, .size = 0x10000U, .protected = false },
};

/* One protected region for write-protect tests. */
static const UdsFlashRegion g_protected_regions[] = {
    { .base_address = 0x00000000U, .size = 0x10000U, .protected = true  },
    { .base_address = 0x00010000U, .size = 0x10000U, .protected = false },
};

/** Initialise a flash memory with the two non-protected regions. */
static void make_flash(UdsFlashMemory *flash) {
    uds_flash_init(flash, g_regions, 2U);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 1: Flash memory simulation
 * ════════════════════════════════════════════════════════════════════════════
 */

/* ── Flash init ─────────────────────────────────────────────────────────── */

START_TEST(test_flash_init_erased_state) {
    UdsFlashMemory flash;
    make_flash(&flash);

    ck_assert(flash.initialized);
    ck_assert_uint_eq(flash.region_count, 2U);

    /* All bytes must be the erased value */
    for (size_t i = 0U; i < UDS_FLASH_MAX_SIZE; i++) {
        ck_assert_uint_eq(flash.data[i], UDS_FLASH_ERASE_VALUE);
    }
}
END_TEST

START_TEST(test_flash_init_null_noop) {
    /* Must not crash */
    uds_flash_init(NULL, NULL, 0U);
}
END_TEST

START_TEST(test_flash_init_no_regions) {
    UdsFlashMemory flash;
    uds_flash_init(&flash, NULL, 0U);
    ck_assert_uint_eq(flash.region_count, 0U);
    ck_assert(flash.initialized);
}
END_TEST

/* ── Flash address validation ───────────────────────────────────────────── */

START_TEST(test_flash_addr_valid_in_region) {
    UdsFlashMemory flash;
    make_flash(&flash);

    ck_assert(uds_flash_address_valid(&flash, 0x0000U, 0x1000U));
}
END_TEST

START_TEST(test_flash_addr_valid_at_end_of_region) {
    UdsFlashMemory flash;
    make_flash(&flash);

    /* Last byte of first region: base=0, size=0x10000 → last valid = 0xFFFF */
    ck_assert(uds_flash_address_valid(&flash, 0xFFFFU, 1U));
}
END_TEST

START_TEST(test_flash_addr_valid_second_region) {
    UdsFlashMemory flash;
    make_flash(&flash);

    ck_assert(uds_flash_address_valid(&flash, 0x10000U, 0x1000U));
}
END_TEST

START_TEST(test_flash_addr_invalid_no_region) {
    UdsFlashMemory flash;
    make_flash(&flash);

    /* Address beyond both regions */
    ck_assert(!uds_flash_address_valid(&flash, 0x30000U, 0x100U));
}
END_TEST

START_TEST(test_flash_addr_invalid_crosses_region_boundary) {
    UdsFlashMemory flash;
    make_flash(&flash);

    /* Range starts in first region but extends past it */
    ck_assert(!uds_flash_address_valid(&flash, 0xFF00U, 0x200U));
}
END_TEST

START_TEST(test_flash_addr_invalid_zero_length) {
    UdsFlashMemory flash;
    make_flash(&flash);

    ck_assert(!uds_flash_address_valid(&flash, 0x0000U, 0U));
}
END_TEST

START_TEST(test_flash_addr_invalid_null_flash) {
    ck_assert(!uds_flash_address_valid(NULL, 0x0000U, 0x100U));
}
END_TEST

/* ── Flash erase ────────────────────────────────────────────────────────── */

START_TEST(test_flash_erase_sets_ff) {
    UdsFlashMemory flash;
    make_flash(&flash);

    /* Write something first */
    memset(flash.data, 0x00U, 0x100U);

    int rc = uds_flash_erase(&flash, 0x0000U, 0x100U);
    ck_assert_int_eq(rc, UDS_CORE_OK);

    for (size_t i = 0U; i < 0x100U; i++) {
        ck_assert_uint_eq(flash.data[i], 0xFFU);
    }
}
END_TEST

START_TEST(test_flash_erase_invalid_address) {
    UdsFlashMemory flash;
    make_flash(&flash);

    int rc = uds_flash_erase(&flash, 0x30000U, 0x100U);
    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
}
END_TEST

START_TEST(test_flash_erase_protected_region) {
    UdsFlashMemory flash;
    uds_flash_init(&flash, g_protected_regions, 2U);

    int rc = uds_flash_erase(&flash, 0x0000U, 0x100U);
    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
}
END_TEST

START_TEST(test_flash_erase_null_flash) {
    int rc = uds_flash_erase(NULL, 0x0000U, 0x100U);
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_flash_erase_zero_length) {
    UdsFlashMemory flash;
    make_flash(&flash);

    int rc = uds_flash_erase(&flash, 0x0000U, 0U);
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

/* ── Flash write ────────────────────────────────────────────────────────── */

START_TEST(test_flash_write_basic) {
    UdsFlashMemory flash;
    make_flash(&flash);

    uint8_t src[8] = {0x01U, 0x02U, 0x03U, 0x04U,
                      0x05U, 0x06U, 0x07U, 0x08U};
    int rc = uds_flash_write(&flash, 0x0100U, src, sizeof(src));
    ck_assert_int_eq(rc, UDS_CORE_OK);
    ck_assert(memcmp(&flash.data[0x0100U], src, sizeof(src)) == 0);
}
END_TEST

START_TEST(test_flash_write_invalid_address) {
    UdsFlashMemory flash;
    make_flash(&flash);

    uint8_t src[4] = {0xAAU, 0xBBU, 0xCCU, 0xDDU};
    int rc = uds_flash_write(&flash, 0x30000U, src, sizeof(src));
    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
}
END_TEST

START_TEST(test_flash_write_protected_region) {
    UdsFlashMemory flash;
    uds_flash_init(&flash, g_protected_regions, 2U);

    uint8_t src[4] = {0xAAU, 0xBBU, 0xCCU, 0xDDU};
    int rc = uds_flash_write(&flash, 0x0000U, src, sizeof(src));
    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
}
END_TEST

START_TEST(test_flash_write_null_flash) {
    uint8_t src[4] = {0};
    int rc = uds_flash_write(NULL, 0x0000U, src, sizeof(src));
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_flash_write_null_data) {
    UdsFlashMemory flash;
    make_flash(&flash);

    int rc = uds_flash_write(&flash, 0x0000U, NULL, 4U);
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

/* ── Flash read ─────────────────────────────────────────────────────────── */

START_TEST(test_flash_read_basic) {
    UdsFlashMemory flash;
    make_flash(&flash);

    uint8_t src[4] = {0xDEU, 0xADU, 0xBEU, 0xEFU};
    memcpy(&flash.data[0x0200U], src, sizeof(src));

    uint8_t dst[4] = {0};
    int rc = uds_flash_read(&flash, 0x0200U, dst, sizeof(dst));
    ck_assert_int_eq(rc, UDS_CORE_OK);
    ck_assert(memcmp(dst, src, sizeof(src)) == 0);
}
END_TEST

START_TEST(test_flash_read_invalid_address) {
    UdsFlashMemory flash;
    make_flash(&flash);

    uint8_t dst[4] = {0};
    int rc = uds_flash_read(&flash, 0x40000U, dst, sizeof(dst));
    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
}
END_TEST

START_TEST(test_flash_read_null_flash) {
    uint8_t dst[4] = {0};
    int rc = uds_flash_read(NULL, 0x0000U, dst, sizeof(dst));
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_flash_read_null_buf) {
    UdsFlashMemory flash;
    make_flash(&flash);

    int rc = uds_flash_read(&flash, 0x0000U, NULL, 4U);
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 2: Service 0x34 — Request Download
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_req_download_valid) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    /* fmt = 0x22: 2 address bytes, 2 size bytes
     * addr = 0x0000, size = 0x0100 */
    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_download(&xfer, &flash, 0x22U,
                                      params, sizeof(params),
                                      resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_OK);
    ck_assert_uint_eq(resp_len, 4U);
    ck_assert_uint_eq(resp[0], 0x74U);
    ck_assert_uint_eq(resp[1], 0x20U);
    ck_assert_uint_eq(xfer.mode, UDS_XFER_DOWNLOAD);
    ck_assert_uint_eq(xfer.address, 0x0000U);
    ck_assert_uint_eq(xfer.total_length, 0x0100U);
    ck_assert_uint_eq(xfer.transferred, 0U);
    ck_assert_uint_eq(xfer.block_seq, 0x01U);
}
END_TEST

START_TEST(test_req_download_already_in_transfer) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);
    xfer.mode = UDS_XFER_DOWNLOAD; /* simulate active transfer */

    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_download(&xfer, &flash, 0x22U,
                                      params, sizeof(params),
                                      resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
    ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT);
}
END_TEST

START_TEST(test_req_download_invalid_address) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    /* fmt=0x42: 4 address bytes, 2 size bytes.
     * Address 0x00030000 is beyond both regions (each 0x10000 bytes). */
    uint8_t params[] = {0x00U, 0x03U, 0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_download(&xfer, &flash, 0x42U,
                                      params, sizeof(params),
                                      resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
    ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

START_TEST(test_req_download_fmt_parsing_4byte_addr) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    /* fmt = 0x42: 4 address bytes, 2 size bytes
     * addr = 0x00001000, size = 0x0020 */
    uint8_t params[] = {0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x20U};
    int rc = uds_svc_request_download(&xfer, &flash, 0x42U,
                                      params, sizeof(params),
                                      resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_OK);
    ck_assert_uint_eq(xfer.address, 0x00001000U);
    ck_assert_uint_eq(xfer.total_length, 0x0020U);
}
END_TEST

START_TEST(test_req_download_null_xfer) {
    UdsFlashMemory flash;
    make_flash(&flash);
    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_download(NULL, &flash, 0x22U,
                                      params, sizeof(params),
                                      resp, sizeof(resp), &resp_len, &nrc);
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

START_TEST(test_req_download_buffer_too_small) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    uint8_t tiny[3];
    size_t tiny_len;
    int rc = uds_svc_request_download(&xfer, &flash, 0x22U,
                                      params, sizeof(params),
                                      tiny, sizeof(tiny), &tiny_len, &nrc);
    ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

/* ── Flash is erased after a successful 0x34 ─────────────────────────── */

START_TEST(test_req_download_erases_flash) {
    UdsFlashMemory flash;
    make_flash(&flash);

    /* Pre-fill the target region with non-FF data */
    memset(&flash.data[0x0000U], 0x55U, 0x0100U);

    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_download(&xfer, &flash, 0x22U,
                                      params, sizeof(params),
                                      resp, sizeof(resp), &resp_len, &nrc);
    ck_assert_int_eq(rc, UDS_CORE_OK);

    for (size_t i = 0U; i < 0x100U; i++) {
        ck_assert_uint_eq(flash.data[i], 0xFFU);
    }
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 3: Service 0x35 — Request Upload
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_req_upload_valid) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_upload(&xfer, &flash, 0x22U,
                                    params, sizeof(params),
                                    resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_OK);
    ck_assert_uint_eq(resp_len, 4U);
    ck_assert_uint_eq(resp[0], 0x75U);
    ck_assert_uint_eq(xfer.mode, UDS_XFER_UPLOAD);
    ck_assert_uint_eq(xfer.address, 0x0000U);
    ck_assert_uint_eq(xfer.total_length, 0x0100U);
    ck_assert_uint_eq(xfer.block_seq, 0x01U);
}
END_TEST

START_TEST(test_req_upload_already_in_transfer) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);
    xfer.mode = UDS_XFER_UPLOAD;

    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_upload(&xfer, &flash, 0x22U,
                                    params, sizeof(params),
                                    resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
    ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT);
}
END_TEST

START_TEST(test_req_upload_invalid_address) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    /* fmt=0x42: 4 address bytes, 2 size bytes.
     * Address 0x00030000 is beyond both regions (each 0x10000 bytes). */
    uint8_t params[] = {0x00U, 0x03U, 0x00U, 0x00U, 0x01U, 0x00U};
    int rc = uds_svc_request_upload(&xfer, &flash, 0x42U,
                                    params, sizeof(params),
                                    resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
    ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
}
END_TEST

/* Upload must NOT erase flash */
START_TEST(test_req_upload_does_not_erase) {
    UdsFlashMemory flash;
    make_flash(&flash);
    memset(&flash.data[0x0000U], 0x55U, 0x0100U);

    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    uint8_t params[] = {0x00U, 0x00U, 0x01U, 0x00U};
    uds_svc_request_upload(&xfer, &flash, 0x22U,
                           params, sizeof(params),
                           resp, sizeof(resp), &resp_len, &nrc);

    /* First byte must still be 0x55 */
    ck_assert_uint_eq(flash.data[0], 0x55U);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 4: Service 0x36 — Transfer Data
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_xfer_data_download_correct_seq) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    /* Set up a download session */
    uint8_t setup[] = {0x00U, 0x00U, 0x10U, 0x00U};
    uds_svc_request_download(&xfer, &flash, 0x22U,
                             setup, sizeof(setup),
                             resp, sizeof(resp), &resp_len, &nrc);

    uint8_t payload[4] = {0xCAU, 0xFEU, 0xBAU, 0xBEU};
    int rc = uds_svc_transfer_data(&xfer, &flash, 0x01U,
                                   payload, sizeof(payload),
                                   resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_OK);
    ck_assert_uint_eq(resp_len, 2U);
    ck_assert_uint_eq(resp[0], 0x76U);
    ck_assert_uint_eq(resp[1], 0x01U);
    ck_assert_uint_eq(xfer.transferred, 4U);
    ck_assert_uint_eq(xfer.block_seq, 0x02U);
    ck_assert(memcmp(&flash.data[0x0000U], payload, sizeof(payload)) == 0);
}
END_TEST

START_TEST(test_xfer_data_download_wrong_seq) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    uint8_t setup[] = {0x00U, 0x00U, 0x10U, 0x00U};
    uds_svc_request_download(&xfer, &flash, 0x22U,
                             setup, sizeof(setup),
                             resp, sizeof(resp), &resp_len, &nrc);

    /* Wrong block sequence: expected 0x01, send 0x05 */
    uint8_t payload[4] = {0x00U, 0x00U, 0x00U, 0x00U};
    int rc = uds_svc_transfer_data(&xfer, &flash, 0x05U,
                                   payload, sizeof(payload),
                                   resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
    ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER);
}
END_TEST

START_TEST(test_xfer_data_no_active_transfer) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer); /* mode = IDLE */

    uint8_t payload[4] = {0x00U};
    int rc = uds_svc_transfer_data(&xfer, &flash, 0x01U,
                                   payload, sizeof(payload),
                                   resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
    ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT);
}
END_TEST

START_TEST(test_xfer_data_upload_reads_flash) {
    UdsFlashMemory flash;
    make_flash(&flash);

    /* Pre-fill flash with known data */
    for (size_t i = 0U; i < 15U; i++) {
        flash.data[i] = (uint8_t)i;
    }

    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    uint8_t setup[] = {0x00U, 0x00U, 0x00U, 0x0FU};
    uds_svc_request_upload(&xfer, &flash, 0x22U,
                           setup, sizeof(setup),
                           resp, sizeof(resp), &resp_len, &nrc);

    int rc = uds_svc_transfer_data(&xfer, &flash, 0x01U,
                                   NULL, 0U,
                                   resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_OK);
    /* Response: [0x76, 0x01, data...] */
    ck_assert_uint_eq(resp[0], 0x76U);
    ck_assert_uint_eq(resp[1], 0x01U);
    ck_assert_uint_eq(resp_len, 2U + 15U);

    for (size_t i = 0U; i < 15U; i++) {
        ck_assert_uint_eq(resp[2U + i], (uint8_t)i);
    }
}
END_TEST

START_TEST(test_xfer_data_block_seq_wraps) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    /* Force block_seq to 0xFF to test wrap */
    xfer.mode           = UDS_XFER_DOWNLOAD;
    xfer.address        = 0x0000U;
    xfer.total_length   = 0x0100U;
    xfer.transferred    = 0x00U;
    xfer.block_seq      = 0xFFU;
    xfer.max_block_size = 15U;

    uint8_t payload[1] = {0xAAU};
    int rc = uds_svc_transfer_data(&xfer, &flash, 0xFFU,
                                   payload, sizeof(payload),
                                   resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_OK);
    /* After wrap: next expected = 0x01, not 0x00 */
    ck_assert_uint_eq(xfer.block_seq, 0x01U);
}
END_TEST

START_TEST(test_xfer_data_null_xfer) {
    UdsFlashMemory flash;
    make_flash(&flash);

    uint8_t payload[4] = {0};
    int rc = uds_svc_transfer_data(NULL, &flash, 0x01U,
                                   payload, sizeof(payload),
                                   resp, sizeof(resp), &resp_len, &nrc);
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Suite 5: Service 0x37 — Request Transfer Exit
 * ════════════════════════════════════════════════════════════════════════════
 */

START_TEST(test_xfer_exit_resets_to_idle) {
    UdsFlashMemory flash;
    make_flash(&flash);
    UdsXferSession xfer;
    uds_xfer_init(&xfer);

    /* Establish a download session */
    uint8_t setup[] = {0x00U, 0x00U, 0x01U, 0x00U};
    uds_svc_request_download(&xfer, &flash, 0x22U,
                             setup, sizeof(setup),
                             resp, sizeof(resp), &resp_len, &nrc);
    ck_assert_uint_eq(xfer.mode, UDS_XFER_DOWNLOAD);

    int rc = uds_svc_transfer_exit(&xfer, resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_OK);
    ck_assert_uint_eq(resp_len, 1U);
    ck_assert_uint_eq(resp[0], 0x77U);
    ck_assert_uint_eq(xfer.mode, UDS_XFER_IDLE);
}
END_TEST

START_TEST(test_xfer_exit_not_in_transfer) {
    UdsXferSession xfer;
    uds_xfer_init(&xfer); /* mode = IDLE */

    int rc = uds_svc_transfer_exit(&xfer, resp, sizeof(resp), &resp_len, &nrc);

    ck_assert_int_eq(rc, UDS_CORE_ERR_NRC);
    ck_assert_uint_eq(nrc, (uint8_t)UDS_NRC_CONDITIONS_NOT_CORRECT);
}
END_TEST

START_TEST(test_xfer_exit_buffer_too_small) {
    UdsXferSession xfer;
    uds_xfer_init(&xfer);
    xfer.mode = UDS_XFER_DOWNLOAD;

    uint8_t tiny[1];
    size_t tiny_len;
    int rc = uds_svc_transfer_exit(&xfer, tiny, 0U, &tiny_len, &nrc);
    ck_assert_int_eq(rc, UDS_CORE_ERR_BUF);
}
END_TEST

START_TEST(test_xfer_exit_null_xfer) {
    int rc = uds_svc_transfer_exit(NULL, resp, sizeof(resp), &resp_len, &nrc);
    ck_assert_int_eq(rc, UDS_CORE_ERR_PARAM);
}
END_TEST

/* ════════════════════════════════════════════════════════════════════════════
 * Test suite / runner wiring
 * ════════════════════════════════════════════════════════════════════════════
 */

static Suite *flash_suite(void) {
    Suite *s = suite_create("uds_flash");

    /* ── Flash memory simulation ─────────────────────────────────── */
    TCase *tc_init = tcase_create("flash_init");
    tcase_add_test(tc_init, test_flash_init_erased_state);
    tcase_add_test(tc_init, test_flash_init_null_noop);
    tcase_add_test(tc_init, test_flash_init_no_regions);
    suite_add_tcase(s, tc_init);

    TCase *tc_addr = tcase_create("flash_address_valid");
    tcase_add_test(tc_addr, test_flash_addr_valid_in_region);
    tcase_add_test(tc_addr, test_flash_addr_valid_at_end_of_region);
    tcase_add_test(tc_addr, test_flash_addr_valid_second_region);
    tcase_add_test(tc_addr, test_flash_addr_invalid_no_region);
    tcase_add_test(tc_addr, test_flash_addr_invalid_crosses_region_boundary);
    tcase_add_test(tc_addr, test_flash_addr_invalid_zero_length);
    tcase_add_test(tc_addr, test_flash_addr_invalid_null_flash);
    suite_add_tcase(s, tc_addr);

    TCase *tc_erase = tcase_create("flash_erase");
    tcase_add_test(tc_erase, test_flash_erase_sets_ff);
    tcase_add_test(tc_erase, test_flash_erase_invalid_address);
    tcase_add_test(tc_erase, test_flash_erase_protected_region);
    tcase_add_test(tc_erase, test_flash_erase_null_flash);
    tcase_add_test(tc_erase, test_flash_erase_zero_length);
    suite_add_tcase(s, tc_erase);

    TCase *tc_write = tcase_create("flash_write");
    tcase_add_test(tc_write, test_flash_write_basic);
    tcase_add_test(tc_write, test_flash_write_invalid_address);
    tcase_add_test(tc_write, test_flash_write_protected_region);
    tcase_add_test(tc_write, test_flash_write_null_flash);
    tcase_add_test(tc_write, test_flash_write_null_data);
    suite_add_tcase(s, tc_write);

    TCase *tc_read = tcase_create("flash_read");
    tcase_add_test(tc_read, test_flash_read_basic);
    tcase_add_test(tc_read, test_flash_read_invalid_address);
    tcase_add_test(tc_read, test_flash_read_null_flash);
    tcase_add_test(tc_read, test_flash_read_null_buf);
    suite_add_tcase(s, tc_read);

    /* ── Service 0x34 ────────────────────────────────────────────── */
    TCase *tc_dl = tcase_create("request_download");
    tcase_add_test(tc_dl, test_req_download_valid);
    tcase_add_test(tc_dl, test_req_download_already_in_transfer);
    tcase_add_test(tc_dl, test_req_download_invalid_address);
    tcase_add_test(tc_dl, test_req_download_fmt_parsing_4byte_addr);
    tcase_add_test(tc_dl, test_req_download_null_xfer);
    tcase_add_test(tc_dl, test_req_download_buffer_too_small);
    tcase_add_test(tc_dl, test_req_download_erases_flash);
    suite_add_tcase(s, tc_dl);

    /* ── Service 0x35 ────────────────────────────────────────────── */
    TCase *tc_ul = tcase_create("request_upload");
    tcase_add_test(tc_ul, test_req_upload_valid);
    tcase_add_test(tc_ul, test_req_upload_already_in_transfer);
    tcase_add_test(tc_ul, test_req_upload_invalid_address);
    tcase_add_test(tc_ul, test_req_upload_does_not_erase);
    suite_add_tcase(s, tc_ul);

    /* ── Service 0x36 ────────────────────────────────────────────── */
    TCase *tc_xd = tcase_create("transfer_data");
    tcase_add_test(tc_xd, test_xfer_data_download_correct_seq);
    tcase_add_test(tc_xd, test_xfer_data_download_wrong_seq);
    tcase_add_test(tc_xd, test_xfer_data_no_active_transfer);
    tcase_add_test(tc_xd, test_xfer_data_upload_reads_flash);
    tcase_add_test(tc_xd, test_xfer_data_block_seq_wraps);
    tcase_add_test(tc_xd, test_xfer_data_null_xfer);
    suite_add_tcase(s, tc_xd);

    /* ── Service 0x37 ────────────────────────────────────────────── */
    TCase *tc_exit = tcase_create("transfer_exit");
    tcase_add_test(tc_exit, test_xfer_exit_resets_to_idle);
    tcase_add_test(tc_exit, test_xfer_exit_not_in_transfer);
    tcase_add_test(tc_exit, test_xfer_exit_buffer_too_small);
    tcase_add_test(tc_exit, test_xfer_exit_null_xfer);
    suite_add_tcase(s, tc_exit);

    return s;
}

int main(void) {
    Suite *s = flash_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_VERBOSE);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
