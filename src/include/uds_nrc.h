/**
 * @file uds_nrc.h
 * @brief ISO 14229-1 Negative Response Code (NRC) definitions.
 *
 * Enumerates all standard NRC values used by UDS diagnostic services.
 * These are sent in 3-byte negative response messages:
 *   [0x7F, original_SID, NRC]
 */

#ifndef UDS_NRC_H
#define UDS_NRC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ISO 14229-1 Negative Response Codes.
 *
 * All values are as defined in ISO 14229-1 §A.1.
 */
typedef enum {
  /** General reject — no more specific NRC applies. */
  UDS_NRC_GENERAL_REJECT = 0x10,

  /** Service ID is not supported by this ECU. */
  UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11,

  /** Sub-function byte is not supported for the requested service. */
  UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED = 0x12,

  /** Message length or format is incorrect. */
  UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT = 0x13,

  /** Response would exceed the maximum allowed length. */
  UDS_NRC_RESPONSE_TOO_LONG = 0x14,

  /** Server is busy; client should repeat the request later. */
  UDS_NRC_BUSY_REPEAT_REQUEST = 0x21,

  /** Conditions are not correct for the requested action. */
  UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22,

  /** Request sequence is incorrect (e.g., key before seed). */
  UDS_NRC_REQUEST_SEQUENCE_ERROR = 0x24,

  /** No response from sub-net component. */
  UDS_NRC_NO_RESPONSE_FROM_SUBNET = 0x25,

  /** A failure prevents execution of the requested action. */
  UDS_NRC_FAILURE_PREVENTS_EXEC = 0x26,

  /** Request parameter is out of range. */
  UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31,

  /** Security access is denied (insufficient privilege level). */
  UDS_NRC_SECURITY_ACCESS_DENIED = 0x33,

  /** Security key sent by tester is invalid. */
  UDS_NRC_INVALID_KEY = 0x35,

  /** Number of allowed security access attempts has been exceeded. */
  UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS = 0x36,

  /**
   * Required time delay has not yet expired (lockout cooldown or
   * seed expiry — client must wait before retrying).
   */
  UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED = 0x37,

  /** Upload/download not accepted (e.g., wrong addressing mode). */
  UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70,

  /** Transfer data has been suspended. */
  UDS_NRC_TRANSFER_DATA_SUSPENDED = 0x71,

  /** General programming failure (flash write / erase error). */
  UDS_NRC_GENERAL_PROGRAMMING_FAILURE = 0x72,

  /** Block sequence counter in Transfer Data is wrong. */
  UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73,

  /**
   * Request correctly received, but the response is pending.
   * Server will send 0x7F + SID + 0x78 periodically while processing.
   */
  UDS_NRC_RESPONSE_PENDING = 0x78,

  /** Sub-function is not supported in the currently active session. */
  UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED_IN_SESSION = 0x7E,

  /** Service is not supported in the currently active session. */
  UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION = 0x7F,
} UdsNrc;

/**
 * @brief Return a human-readable string for an NRC byte value.
 *
 * @param nrc NRC byte (e.g. UDS_NRC_INVALID_KEY).
 * @return Pointer to a static string; never NULL.
 */
const char *uds_nrc_string(uint8_t nrc);

#ifdef __cplusplus
}
#endif

#endif /* UDS_NRC_H */
