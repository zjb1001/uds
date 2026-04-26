/**
 * @file uds_flash.h
 * @brief Flash memory simulation and UDS flash programming services.
 *
 * Implements:
 *   - Service 0x34: Request Download
 *   - Service 0x35: Request Upload
 *   - Service 0x36: Transfer Data
 *   - Service 0x37: Request Transfer Exit
 *
 * Design:
 *   - No dynamic memory allocation; all state is in caller-supplied structs.
 *   - Flash memory is modelled as a flat byte array with named regions.
 *   - Service functions produce raw UDS response bytes in the caller's buffer.
 *   - On success (UDS_CORE_OK) the response buffer holds the positive response.
 *   - On NRC failure (UDS_CORE_ERR_NRC) *nrc_out is set to the NRC byte;
 *     the caller is responsible for building the 0x7F negative response frame.
 *   - Thread-safety: each UdsFlashMemory / UdsXferSession instance is
 *     independent.  Concurrent access to the same instance requires external
 *     locking.
 */

#ifndef UDS_FLASH_H
#define UDS_FLASH_H

#include "uds_core.h"
#include "uds_nrc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Flash memory model ─────────────────────────────────────────────────── */

/** Maximum number of distinct flash regions. */
#define UDS_FLASH_MAX_REGIONS 8U

/** Total simulated flash size in bytes (256 KB). */
#define UDS_FLASH_MAX_SIZE    (256U * 1024U)

/** Byte value representing an erased flash cell. */
#define UDS_FLASH_ERASE_VALUE 0xFFU

/**
 * @brief Descriptor for one contiguous flash memory region.
 */
typedef struct {
    uint32_t base_address; /**< Region start address. */
    uint32_t size;         /**< Region size in bytes. */
    bool     protected;    /**< True if the region is write-protected. */
} UdsFlashRegion;

/**
 * @brief Simulated flash memory device.
 *
 * Initialise with uds_flash_init() before use.
 * All fields are public to allow direct inspection in tests.
 */
typedef struct {
    uint8_t        data[UDS_FLASH_MAX_SIZE]; /**< Simulated flash content. */
    UdsFlashRegion regions[UDS_FLASH_MAX_REGIONS]; /**< Region table. */
    size_t         region_count;             /**< Number of defined regions. */
    bool           initialized;              /**< True after uds_flash_init(). */
} UdsFlashMemory;

/* ── Flash operations ───────────────────────────────────────────────────── */

/**
 * @brief Initialise the flash memory to the erased state.
 *
 * Sets all bytes to UDS_FLASH_ERASE_VALUE (0xFF) and copies the supplied
 * region table.
 *
 * @param[out] flash        Flash memory to initialise.
 * @param[in]  regions      Array of region descriptors (may be NULL if count=0).
 * @param[in]  region_count Number of entries in @p regions
 *                          (clamped to UDS_FLASH_MAX_REGIONS).
 */
void uds_flash_init(UdsFlashMemory *flash, const UdsFlashRegion *regions,
                    size_t region_count);

/**
 * @brief Erase a range of flash memory (set bytes to 0xFF).
 *
 * The entire range [address, address+length) must fall within a single
 * non-protected region.
 *
 * @param[in,out] flash   Flash memory device.
 * @param[in]     address Start address (relative to flash base 0).
 * @param[in]     length  Number of bytes to erase.
 * @return UDS_CORE_OK on success, UDS_CORE_ERR_PARAM on NULL flash or
 *         length=0, UDS_CORE_ERR_NRC if the range is invalid or protected
 *         (nrc not set — caller uses UDS_NRC_GENERAL_PROGRAMMING_FAILURE).
 */
int uds_flash_erase(UdsFlashMemory *flash, uint32_t address, uint32_t length);

/**
 * @brief Write bytes into flash memory.
 *
 * The entire range [address, address+len) must fall within a single
 * non-protected region.
 *
 * @param[in,out] flash   Flash memory device.
 * @param[in]     address Destination start address.
 * @param[in]     data    Source data bytes.
 * @param[in]     len     Number of bytes to write.
 * @return UDS_CORE_OK on success, UDS_CORE_ERR_PARAM on NULL pointers or
 *         len=0, UDS_CORE_ERR_NRC if the range is invalid or protected.
 */
int uds_flash_write(UdsFlashMemory *flash, uint32_t address,
                    const uint8_t *data, size_t len);

/**
 * @brief Read bytes from flash memory.
 *
 * The entire range [address, address+len) must fall within any defined region.
 *
 * @param[in]  flash   Flash memory device.
 * @param[in]  address Source start address.
 * @param[out] buf     Destination buffer.
 * @param[in]  len     Number of bytes to read.
 * @return UDS_CORE_OK on success, UDS_CORE_ERR_PARAM on NULL pointers or
 *         len=0, UDS_CORE_ERR_NRC if the range is invalid.
 */
int uds_flash_read(const UdsFlashMemory *flash, uint32_t address,
                   uint8_t *buf, size_t len);

/**
 * @brief Check whether an address range falls within any defined region.
 *
 * @param[in] flash   Flash memory device.
 * @param[in] address Start address.
 * @param[in] length  Range length in bytes.
 * @return true if [address, address+length) lies entirely within one region,
 *         false otherwise (including NULL flash or zero length).
 */
bool uds_flash_address_valid(const UdsFlashMemory *flash, uint32_t address,
                              uint32_t length);

/* ── Transfer session state ─────────────────────────────────────────────── */

/**
 * @brief Active transfer direction for services 0x34/0x35/0x36/0x37.
 */
typedef enum {
    UDS_XFER_IDLE     = 0, /**< No transfer in progress. */
    UDS_XFER_DOWNLOAD = 1, /**< 0x34 accepted; expecting 0x36 writes. */
    UDS_XFER_UPLOAD   = 2, /**< 0x35 accepted; serving 0x36 reads. */
} UdsXferMode;

/**
 * @brief State for an active download or upload transfer session.
 *
 * Initialise with uds_xfer_init() before use.
 */
typedef struct {
    UdsXferMode mode;           /**< Current transfer direction. */
    uint32_t    address;        /**< Target start address in flash. */
    uint32_t    total_length;   /**< Total bytes to transfer. */
    uint32_t    transferred;    /**< Bytes successfully transferred so far. */
    uint8_t     block_seq;      /**< Expected next block sequence counter
                                     (1–255, wraps 0xFF → 0x01). */
    uint16_t    max_block_size; /**< Negotiated max data bytes per block
                                     (excluding the SID + seq header). */
} UdsXferSession;

/* ── UDS flash service API ──────────────────────────────────────────────── */

/**
 * @brief Initialise a UdsXferSession to the idle state.
 *
 * @param[out] xfer Transfer session to initialise.
 */
void uds_xfer_init(UdsXferSession *xfer);

/**
 * @brief Process a Service 0x34 (Request Download) request.
 *
 * Validates that no transfer is already in progress, parses the
 * addressAndLengthFormatIdentifier byte, erases the target flash region,
 * and sets up the transfer session.
 *
 * @param[in,out] xfer              Transfer session state.
 * @param[in,out] flash             Flash memory device.
 * @param[in]     address_and_len_fmt  Format byte: high nibble = address bytes,
 *                                  low nibble = length bytes (each 1–4).
 * @param[in]     addr_and_len_data Packed address+length bytes (big-endian).
 * @param[in]     data_len          Length of @p addr_and_len_data.
 * @param[out]    resp              Buffer for positive response bytes.
 * @param[in]     resp_size         Capacity of @p resp (must be ≥ 4).
 * @param[out]    resp_len          Number of bytes written on success.
 * @param[out]    nrc_out           NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_request_download(UdsXferSession *xfer, UdsFlashMemory *flash,
                              uint8_t address_and_len_fmt,
                              const uint8_t *addr_and_len_data, size_t data_len,
                              uint8_t *resp, size_t resp_size, size_t *resp_len,
                              uint8_t *nrc_out);

/**
 * @brief Process a Service 0x35 (Request Upload) request.
 *
 * Same parameter parsing as 0x34 but sets up an upload session.
 * Does not erase flash.
 *
 * @param[in,out] xfer              Transfer session state.
 * @param[in]     flash             Flash memory device (read-only).
 * @param[in]     address_and_len_fmt  Format byte.
 * @param[in]     addr_and_len_data Packed address+length bytes (big-endian).
 * @param[in]     data_len          Length of @p addr_and_len_data.
 * @param[out]    resp              Buffer for positive response bytes.
 * @param[in]     resp_size         Capacity of @p resp (must be ≥ 4).
 * @param[out]    resp_len          Number of bytes written on success.
 * @param[out]    nrc_out           NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_request_upload(UdsXferSession *xfer, const UdsFlashMemory *flash,
                            uint8_t address_and_len_fmt,
                            const uint8_t *addr_and_len_data, size_t data_len,
                            uint8_t *resp, size_t resp_size, size_t *resp_len,
                            uint8_t *nrc_out);

/**
 * @brief Process a Service 0x36 (Transfer Data) request.
 *
 * For a download session, writes @p data_len bytes from @p data into flash
 * at the current transfer offset.  For an upload session, reads up to
 * max_block_size bytes from flash into the response buffer.
 *
 * @param[in,out] xfer             Transfer session state.
 * @param[in,out] flash            Flash memory device.
 * @param[in]     block_seq_counter Block sequence counter from the tester.
 * @param[in]     data             Data bytes (download) or ignored (upload).
 * @param[in]     data_len         Length of @p data (download only).
 * @param[out]    resp             Buffer for positive response bytes.
 * @param[in]     resp_size        Capacity of @p resp.
 * @param[out]    resp_len         Number of bytes written on success.
 * @param[out]    nrc_out          NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_transfer_data(UdsXferSession *xfer, UdsFlashMemory *flash,
                           uint8_t block_seq_counter,
                           const uint8_t *data, size_t data_len,
                           uint8_t *resp, size_t resp_size, size_t *resp_len,
                           uint8_t *nrc_out);

/**
 * @brief Process a Service 0x37 (Request Transfer Exit) request.
 *
 * Finalises the transfer and resets the session to IDLE.
 *
 * @param[in,out] xfer      Transfer session state.
 * @param[out]    resp      Buffer for positive response bytes.
 * @param[in]     resp_size Capacity of @p resp (must be ≥ 1).
 * @param[out]    resp_len  Number of bytes written on success.
 * @param[out]    nrc_out   NRC byte set when return == UDS_CORE_ERR_NRC.
 * @return UDS_CORE_OK, UDS_CORE_ERR_PARAM, UDS_CORE_ERR_NRC, or
 *         UDS_CORE_ERR_BUF.
 */
int uds_svc_transfer_exit(UdsXferSession *xfer,
                           uint8_t *resp, size_t resp_size, size_t *resp_len,
                           uint8_t *nrc_out);

#ifdef __cplusplus
}
#endif

#endif /* UDS_FLASH_H */
