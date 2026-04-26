/**
 * @file ecusim.h
 * @brief ECU Simulator — UDS server over SocketCAN / ISO-TP.
 *
 * Provides a single-ECU UDS server that:
 *   - Listens for ISO-TP requests on  CAN ID = 0x600 + ecu_id
 *   - Responds with ISO-TP responses on CAN ID = 0x680 + ecu_id
 *
 * Supported services:
 *   0x10  Diagnostic Session Control
 *   0x3E  Tester Present
 *   0x27  Security Access (XOR seed/key)
 *   0x22  ReadDataByIdentifier
 *   0x2E  WriteDataByIdentifier
 *   0x11  ECUReset
 *   0x28  CommunicationControl
 *   0x14  ClearDiagnosticInformation
 *   0x19  ReadDTCInformation (sub-fn 0x01, 0x02, 0x0A)
 *   0x34  RequestDownload
 *   0x35  RequestUpload
 *   0x36  TransferData
 *   0x37  RequestTransferExit
 *
 * Design:
 *   - No dynamic memory; all state in the caller-supplied EcuSimulator struct.
 *   - Single-threaded receive loop.
 *   - Graceful shutdown via ecusim_stop() (sets running = 0).
 */

#ifndef ECUSIM_H
#define ECUSIM_H

#include "uds_can.h"
#include "uds_core.h"
#include "uds_data.h"
#include "uds_dtc.h"
#include "uds_flash.h"
#include "uds_routine.h"

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Simulator buffer sizes ─────────────────────────────────────────────── */

/** Maximum UDS PDU size handled by the simulator. */
#define ECUSIM_MAX_PDU 4095U

/** Maximum number of DID entries in the default registry. */
#define ECUSIM_MAX_DIDS 16U

/** Maximum number of DTC entries in the default registry. */
#define ECUSIM_MAX_DTCS 8U

/** Maximum number of routine entries in the default registry. */
#define ECUSIM_MAX_ROUTINES 4U

/* ── Simulator state ────────────────────────────────────────────────────── */

/**
 * @brief All state required for one simulated ECU instance.
 *
 * Initialise with ecusim_init() before calling ecusim_run().
 * Fields are public for inspection in tests / integration scenarios.
 */
typedef struct {
  /* Communication */
  UdsCanSocket sock;    /**< Open SocketCAN socket. */
  uint8_t ecu_id;       /**< Logical ECU identifier (1..127). */
  uint32_t req_can_id;  /**< 0x600 + ecu_id — receive requests on this. */
  uint32_t resp_can_id; /**< 0x680 + ecu_id — send responses on this. */

  /* UDS core state */
  UdsCoreSession session;   /**< Session state (0x10 / 0x3E). */
  UdsCoreSecurity security; /**< Security access state (0x27). */

  /* DID registry */
  UdsDidRegistry did_reg;                   /**< DID registry handle. */
  UdsDidEntry did_entries[ECUSIM_MAX_DIDS]; /**< DID table storage. */
  uint8_t did_data[ECUSIM_MAX_DIDS][32];    /**< DID data buffers. */
  size_t did_count; /**< Number of active DID entries. */

  /* DTC registry */
  UdsDtcRegistry dtc_reg;                   /**< DTC registry handle. */
  UdsDtcEntry dtc_entries[ECUSIM_MAX_DTCS]; /**< DTC table storage. */
  size_t dtc_count; /**< Number of active DTC entries. */

  /* Flash / bootloader */
  UdsFlashMemory flash; /**< Simulated flash memory. */
  UdsXferSession xfer;  /**< In-progress transfer session (0x34-0x37). */

  /* Run control */
  volatile sig_atomic_t running; /**< Set to 0 to stop ecusim_run(). */
  bool verbose;                  /**< Print request/response summaries. */
} EcuSimulator;

/* ── Simulator lifecycle ────────────────────────────────────────────────── */

/**
 * @brief Initialise an EcuSimulator with default DID/DTC data.
 *
 * Opens the SocketCAN socket, sets receive filters, initialises session and
 * security state, and populates a default DID/DTC registry.
 *
 * @param[out] sim     Simulator to initialise.
 * @param[in]  ecu_id  Logical ECU identifier (1..127).
 * @param[in]  ifname  CAN interface name (e.g. "vcan0").
 * @return 0 on success, -1 on socket/filter error.
 */
int ecusim_init(EcuSimulator *sim, uint8_t ecu_id, const char *ifname);

/**
 * @brief Run the ECU simulator receive loop.
 *
 * Blocks, processing UDS requests, until sim->running is set to 0 (e.g. by
 * ecusim_stop() or signal handler).
 *
 * @param[in,out] sim Initialised simulator.
 */
void ecusim_run(EcuSimulator *sim);

/**
 * @brief Request graceful shutdown of the ecusim_run() loop.
 *
 * Thread-safe (sets a volatile flag).
 *
 * @param[in,out] sim Running simulator.
 */
void ecusim_stop(EcuSimulator *sim);

/**
 * @brief Close the simulator socket and release resources.
 *
 * Safe to call even if ecusim_init() was not called or failed.
 *
 * @param[in,out] sim Simulator to tear down.
 */
void ecusim_cleanup(EcuSimulator *sim);

#ifdef __cplusplus
}
#endif

#endif /* ECUSIM_H */
