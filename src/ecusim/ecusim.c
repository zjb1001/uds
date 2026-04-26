/**
 * @file ecusim.c
 * @brief ECU Simulator implementation — UDS server over SocketCAN / ISO-TP.
 *
 * Implements the ECU simulator dispatch loop and all supported UDS services.
 * Uses the existing uds_can, uds_tp, uds_core, uds_data, uds_dtc, and
 * uds_bootloader libraries.
 */

#define _POSIX_C_SOURCE 200809L

#include "ecusim.h"

#include "uds_can.h"
#include "uds_core.h"
#include "uds_data.h"
#include "uds_dtc.h"
#include "uds_flash.h"
#include "uds_nrc.h"
#include "uds_routine.h"
#include "uds_tp.h"

#include <stdio.h>
#include <string.h>

/* ── Well-known DID values for the default registry ─────────────────────── */

#define DID_ECU_PART_NUMBER 0xF187U /**< ECU part number (ASCII, writable). */
#define DID_HW_VERSION 0xF191U      /**< Hardware version (read-only). */
#define DID_SW_VERSION 0xF189U      /**< Software version (read-only). */
#define DID_VIN 0xF190U             /**< Vehicle Identification Number. */
#define DID_ODOMETER 0x0101U        /**< Odometer (4 bytes, writable). */

/* ── Well-known DTC codes for the default registry ──────────────────────── */

#define DTC_BATTERY_VOLTAGE 0x010001UL /**< Battery voltage out of range. */
#define DTC_COMM_TIMEOUT 0x010002UL    /**< Communication timeout. */

/* ── Internal helpers ───────────────────────────────────────────────────── */

/**
 * Build a 3-byte negative response frame: [0x7F, sid, nrc].
 */
static void build_nrc(uint8_t *resp, size_t *resp_len, uint8_t sid,
                      uint8_t nrc) {
  resp[0] = 0x7FU;
  resp[1] = sid;
  resp[2] = nrc;
  *resp_len = 3U;
}

/* ── Service dispatcher ─────────────────────────────────────────────────── */

/**
 * Dispatch a UDS request to the appropriate service handler and write the
 * response into resp[].
 *
 * @param sim      Simulator state.
 * @param req      Raw request bytes (SID first).
 * @param req_len  Length of req in bytes.
 * @param resp     Buffer to write the response into.
 * @param resp_sz  Capacity of resp.
 * @param resp_len [out] Length of the response (0 = no response to send).
 */
static void ecusim_dispatch(EcuSimulator *sim, const uint8_t *req,
                            size_t req_len, uint8_t *resp, size_t resp_sz,
                            size_t *resp_len) {
  *resp_len = 0U;

  if (req_len == 0U) {
    return;
  }

  uint8_t sid = req[0];
  uint8_t nrc = 0U;
  int rc = UDS_CORE_OK;

  switch (sid) {

  /* ── 0x10: Diagnostic Session Control ──────────────────────────────── */
  case 0x10U:
    if (req_len < 2U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    rc = uds_core_dsc(&sim->session, req[1], resp, resp_sz, resp_len, &nrc);
    break;

  /* ── 0x3E: Tester Present ───────────────────────────────────────────── */
  case 0x3EU:
    if (req_len < 2U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    rc = uds_core_tester_present(&sim->session, req[1], resp, resp_sz, resp_len,
                                 &nrc);
    break;

  /* ── 0x27: Security Access ──────────────────────────────────────────── */
  case 0x27U:
    if (req_len < 2U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    if (req[1] & 0x01U) {
      /* Odd sub-function: requestSeed */
      rc = uds_core_sec_request_seed(&sim->security, req[1], resp, resp_sz,
                                     resp_len, &nrc);
    } else {
      /* Even sub-function: sendKey */
      if (req_len < 2U + UDS_CORE_SEED_LEN) {
        build_nrc(resp, resp_len, sid,
                  (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
        return;
      }
      rc = uds_core_sec_send_key(&sim->security, req[1], req + 2U, req_len - 2U,
                                 resp, resp_sz, resp_len, &nrc);
      /* Propagate unlocked level to session */
      if (rc == UDS_CORE_OK) {
        sim->session.security_level = sim->security.unlocked_level;
      }
    }
    break;

  /* ── 0x22: ReadDataByIdentifier ─────────────────────────────────────── */
  case 0x22U:
    if (req_len < 3U || (req_len - 1U) % 2U != 0U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    {
      size_t did_count = (req_len - 1U) / 2U;
      uint16_t did_list[16];
      if (did_count > 16U) {
        build_nrc(resp, resp_len, sid, (uint8_t)UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
      }
      for (size_t i = 0U; i < did_count; i++) {
        did_list[i] = ((uint16_t)req[1U + i * 2U] << 8U) | req[2U + i * 2U];
      }
      rc = uds_svc_read_did(&sim->did_reg, did_list, did_count, resp, resp_sz,
                            resp_len, &nrc);
    }
    break;

  /* ── 0x2E: WriteDataByIdentifier ────────────────────────────────────── */
  case 0x2EU:
    if (req_len < 4U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    {
      uint16_t did_id = ((uint16_t)req[1] << 8U) | req[2];
      bool sec_ok = uds_core_sec_is_unlocked(&sim->security, 0x01U);
      rc = uds_svc_write_did(&sim->did_reg, did_id, req + 3U, req_len - 3U,
                             (uint8_t)sim->session.type, sec_ok, resp, resp_sz,
                             resp_len, &nrc);
    }
    break;

  /* ── 0x11: ECUReset ─────────────────────────────────────────────────── */
  case 0x11U:
    if (req_len < 2U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    rc = uds_svc_ecu_reset(req[1], resp, resp_sz, resp_len, &nrc);
    if (rc == UDS_CORE_OK) {
      /* Simulate reset: return to default session */
      uds_core_session_init(&sim->session, sim->ecu_id, NULL);
      uds_core_security_init(&sim->security);
    }
    break;

  /* ── 0x28: CommunicationControl ─────────────────────────────────────── */
  case 0x28U:
    if (req_len < 3U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    rc = uds_svc_comm_control(req[1], req[2], resp, resp_sz, resp_len, &nrc);
    break;

  /* ── 0x14: ClearDiagnosticInformation ───────────────────────────────── */
  case 0x14U:
    if (req_len < 4U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    {
      uint32_t group = ((uint32_t)req[1] << 16U) | ((uint32_t)req[2] << 8U) |
                       (uint32_t)req[3];
      rc = uds_svc_clear_dtc(&sim->dtc_reg, group, resp, resp_sz, resp_len,
                             &nrc);
    }
    break;

  /* ── 0x19: ReadDTCInformation ───────────────────────────────────────── */
  case 0x19U:
    if (req_len < 2U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    {
      uint8_t sub_fn = req[1];
      /* sub-functions 0x01 and 0x02 require a statusMask byte */
      if ((sub_fn == 0x01U || sub_fn == 0x02U) && req_len < 3U) {
        build_nrc(resp, resp_len, sid,
                  (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
        return;
      }
      const uint8_t *dtc_req_data = (req_len > 2U) ? (req + 2U) : NULL;
      size_t dtc_req_len = (req_len > 2U) ? (req_len - 2U) : 0U;
      rc = uds_svc_read_dtc(&sim->dtc_reg, sub_fn, dtc_req_data, dtc_req_len,
                            resp, resp_sz, resp_len, &nrc);
    }
    break;

  /* ── 0x34: RequestDownload ──────────────────────────────────────────── */
  case 0x34U:
    if (req_len < 3U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    rc = uds_svc_request_download(&sim->xfer, &sim->flash, req[1], req + 2U,
                                  req_len - 2U, resp, resp_sz, resp_len, &nrc);
    break;

  /* ── 0x35: RequestUpload ────────────────────────────────────────────── */
  case 0x35U:
    if (req_len < 3U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    rc = uds_svc_request_upload(&sim->xfer, &sim->flash, req[1], req + 2U,
                                req_len - 2U, resp, resp_sz, resp_len, &nrc);
    break;

  /* ── 0x36: TransferData ─────────────────────────────────────────────── */
  case 0x36U:
    if (req_len < 2U) {
      build_nrc(resp, resp_len, sid,
                (uint8_t)UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT);
      return;
    }
    rc = uds_svc_transfer_data(&sim->xfer, &sim->flash, req[1], req + 2U,
                               req_len - 2U, resp, resp_sz, resp_len, &nrc);
    break;

  /* ── 0x37: RequestTransferExit ──────────────────────────────────────── */
  case 0x37U:
    rc = uds_svc_transfer_exit(&sim->xfer, resp, resp_sz, resp_len, &nrc);
    break;

  /* ── Unknown service ────────────────────────────────────────────────── */
  default:
    build_nrc(resp, resp_len, sid, (uint8_t)UDS_NRC_SERVICE_NOT_SUPPORTED);
    return;
  }

  /* If the service returned an NRC condition, build the negative response */
  if (rc != UDS_CORE_OK && nrc != 0U) {
    build_nrc(resp, resp_len, sid, nrc);
  }
}

/* ── Default registry population ────────────────────────────────────────── */

/**
 * Populate the simulator with a set of default DIDs and DTCs useful for
 * integration testing.
 */
static void ecusim_populate_defaults(EcuSimulator *sim) {
  /* ── DIDs ─────────────────────────────────────────────────────────── */
  static const char ecu_part[] = "UDS-SIM-0001";
  static const char hw_version[] = "HW-1.0";
  static const char sw_version[] = "SW-1.0.0";
  static const char vin[] = "1HGCM82633A123456";

  size_t idx = 0U;

  /* F187 — ECU Part Number (writable in extended session) */
  memcpy(sim->did_data[idx], ecu_part, sizeof(ecu_part) - 1U);
  sim->did_entries[idx].did_id = DID_ECU_PART_NUMBER;
  sim->did_entries[idx].data = sim->did_data[idx];
  sim->did_entries[idx].data_len = sizeof(ecu_part) - 1U;
  sim->did_entries[idx].writable = true;
  sim->did_entries[idx].min_session = (uint8_t)UDS_SESSION_DEFAULT;
  sim->did_entries[idx].min_write_session = (uint8_t)UDS_SESSION_EXTENDED;
  sim->did_entries[idx].requires_security = false;
  idx++;

  /* F191 — Hardware Version (read-only) */
  memcpy(sim->did_data[idx], hw_version, sizeof(hw_version) - 1U);
  sim->did_entries[idx].did_id = DID_HW_VERSION;
  sim->did_entries[idx].data = sim->did_data[idx];
  sim->did_entries[idx].data_len = sizeof(hw_version) - 1U;
  sim->did_entries[idx].writable = false;
  sim->did_entries[idx].min_session = (uint8_t)UDS_SESSION_DEFAULT;
  sim->did_entries[idx].min_write_session = (uint8_t)UDS_SESSION_EXTENDED;
  sim->did_entries[idx].requires_security = false;
  idx++;

  /* F189 — Software Version (read-only) */
  memcpy(sim->did_data[idx], sw_version, sizeof(sw_version) - 1U);
  sim->did_entries[idx].did_id = DID_SW_VERSION;
  sim->did_entries[idx].data = sim->did_data[idx];
  sim->did_entries[idx].data_len = sizeof(sw_version) - 1U;
  sim->did_entries[idx].writable = false;
  sim->did_entries[idx].min_session = (uint8_t)UDS_SESSION_DEFAULT;
  sim->did_entries[idx].min_write_session = (uint8_t)UDS_SESSION_EXTENDED;
  sim->did_entries[idx].requires_security = false;
  idx++;

  /* F190 — VIN (writable with security in extended session) */
  memcpy(sim->did_data[idx], vin, sizeof(vin) - 1U);
  sim->did_entries[idx].did_id = DID_VIN;
  sim->did_entries[idx].data = sim->did_data[idx];
  sim->did_entries[idx].data_len = sizeof(vin) - 1U;
  sim->did_entries[idx].writable = true;
  sim->did_entries[idx].min_session = (uint8_t)UDS_SESSION_DEFAULT;
  sim->did_entries[idx].min_write_session = (uint8_t)UDS_SESSION_EXTENDED;
  sim->did_entries[idx].requires_security = true;
  idx++;

  /* 0101 — Odometer (4 bytes, writable in extended session) */
  sim->did_data[idx][0] = 0x00U;
  sim->did_data[idx][1] = 0x00U;
  sim->did_data[idx][2] = 0x12U;
  sim->did_data[idx][3] = 0x34U;
  sim->did_entries[idx].did_id = DID_ODOMETER;
  sim->did_entries[idx].data = sim->did_data[idx];
  sim->did_entries[idx].data_len = 4U;
  sim->did_entries[idx].writable = true;
  sim->did_entries[idx].min_session = (uint8_t)UDS_SESSION_DEFAULT;
  sim->did_entries[idx].min_write_session = (uint8_t)UDS_SESSION_EXTENDED;
  sim->did_entries[idx].requires_security = false;
  idx++;

  sim->did_count = idx;
  uds_did_registry_init(&sim->did_reg, sim->did_entries, sim->did_count);

  /* ── DTCs ─────────────────────────────────────────────────────────── */
  size_t di = 0U;

  sim->dtc_entries[di].dtc_code = DTC_BATTERY_VOLTAGE;
  sim->dtc_entries[di].status =
      UDS_DTC_STATUS_CONFIRMED_DTC | UDS_DTC_STATUS_TEST_FAILED_SINCE_CLEAR;
  sim->dtc_entries[di].snapshot_len = 0U;
  di++;

  sim->dtc_entries[di].dtc_code = DTC_COMM_TIMEOUT;
  sim->dtc_entries[di].status = UDS_DTC_STATUS_PENDING_DTC;
  sim->dtc_entries[di].snapshot_len = 0U;
  di++;

  sim->dtc_count = di;
  uds_dtc_registry_init(&sim->dtc_reg, sim->dtc_entries, sim->dtc_count);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int ecusim_init(EcuSimulator *sim, uint8_t ecu_id, const char *ifname) {
  if (!sim || !ifname) {
    return -1;
  }

  memset(sim, 0, sizeof(*sim));

  sim->ecu_id = ecu_id;
  sim->req_can_id = uds_can_req_id(ecu_id);
  sim->resp_can_id = uds_can_resp_id(ecu_id);
  sim->running = 1;

  /* Initialise UDS state */
  uds_core_session_init(&sim->session, ecu_id, NULL);
  uds_core_security_init(&sim->security);

  /* Initialise flash memory with one 256 KB region */
  UdsFlashRegion regions[] = {
      {.base_address = 0x00000000U, .size = 256U * 1024U, .protected = false}};
  uds_flash_init(&sim->flash, regions, 1U);
  uds_xfer_init(&sim->xfer);

  /* Populate default DID / DTC data */
  ecusim_populate_defaults(sim);

  /* Open SocketCAN socket */
  int rc = uds_can_open(&sim->sock, ifname);
  if (rc != UDS_CAN_OK) {
    return -1;
  }

  /* Set receive filter: accept only the request CAN ID for this ECU */
  UdsCanFilter filt;
  filt.can_id = sim->req_can_id;
  filt.can_mask = 0x7FFU; /* standard 11-bit exact match */
  if (uds_can_set_filter(&sim->sock, &filt, 1U) != UDS_CAN_OK) {
    uds_can_close(&sim->sock);
    return -1;
  }

  return 0;
}

void ecusim_run(EcuSimulator *sim) {
  if (!sim) {
    return;
  }

  /* ISO-TP config: 1 s timeout (allows periodic running check), no STmin */
  UdsTpConfig tp_cfg = {
      .timeout_ms = 100U, /* short timeout so we can check running flag */
      .block_size = 0U,
      .st_min = 0U,
  };

  uint8_t req_buf[ECUSIM_MAX_PDU];
  uint8_t resp_buf[ECUSIM_MAX_PDU];

  while (sim->running) {
    size_t req_len = 0U;
    size_t resp_len = 0U;

    /* Block (up to timeout_ms) waiting for the next ISO-TP request */
    int rc = uds_tp_recv(&sim->sock, sim->resp_can_id, req_buf, sizeof(req_buf),
                         &req_len, &tp_cfg);

    if (rc == UDS_TP_ERR_TIMEOUT || rc == UDS_TP_ERR_RECV) {
      /* No frame arrived — check running flag and loop */
      continue;
    }
    if (rc != UDS_TP_OK) {
      if (sim->verbose) {
        fprintf(stderr, "[ecusim %u] recv error %d\n", sim->ecu_id, rc);
      }
      continue;
    }

    if (sim->verbose && req_len > 0U) {
      fprintf(stderr, "[ecusim %u] req SID=0x%02X len=%zu\n", sim->ecu_id,
              req_buf[0], req_len);
    }

    /* Dispatch and generate response */
    ecusim_dispatch(sim, req_buf, req_len, resp_buf, sizeof(resp_buf),
                    &resp_len);

    /* Send response (suppress if resp_len == 0, e.g. 0x3E suppress bit) */
    if (resp_len > 0U) {
      if (sim->verbose) {
        fprintf(stderr, "[ecusim %u] resp 0x%02X len=%zu\n", sim->ecu_id,
                resp_buf[0], resp_len);
      }
      rc = uds_tp_send(&sim->sock, sim->resp_can_id, resp_buf, resp_len,
                       &tp_cfg);
      if (rc != UDS_TP_OK && sim->verbose) {
        fprintf(stderr, "[ecusim %u] send error %d\n", sim->ecu_id, rc);
      }
    }
  }
}

void ecusim_stop(EcuSimulator *sim) {
  if (sim) {
    sim->running = 0;
  }
}

void ecusim_cleanup(EcuSimulator *sim) {
  if (sim) {
    uds_can_close(&sim->sock);
  }
}
