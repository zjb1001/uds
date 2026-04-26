/**
 * @file nrc.c
 * @brief ISO 14229-1 NRC string table.
 */

#include "uds_nrc.h"

const char *uds_nrc_string(uint8_t nrc) {
  switch ((UdsNrc)nrc) {
  case UDS_NRC_GENERAL_REJECT:
    return "generalReject";
  case UDS_NRC_SERVICE_NOT_SUPPORTED:
    return "serviceNotSupported";
  case UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED:
    return "subFunctionNotSupported";
  case UDS_NRC_INCORRECT_MSG_LEN_OR_FORMAT:
    return "incorrectMessageLengthOrInvalidFormat";
  case UDS_NRC_RESPONSE_TOO_LONG:
    return "responseTooLong";
  case UDS_NRC_BUSY_REPEAT_REQUEST:
    return "busyRepeatRequest";
  case UDS_NRC_CONDITIONS_NOT_CORRECT:
    return "conditionsNotCorrect";
  case UDS_NRC_REQUEST_SEQUENCE_ERROR:
    return "requestSequenceError";
  case UDS_NRC_NO_RESPONSE_FROM_SUBNET:
    return "noResponseFromSubnetComponent";
  case UDS_NRC_FAILURE_PREVENTS_EXEC:
    return "failurePreventsExecutionOfRequestedAction";
  case UDS_NRC_REQUEST_OUT_OF_RANGE:
    return "requestOutOfRange";
  case UDS_NRC_SECURITY_ACCESS_DENIED:
    return "securityAccessDenied";
  case UDS_NRC_INVALID_KEY:
    return "invalidKey";
  case UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS:
    return "exceededNumberOfAttempts";
  case UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED:
    return "requiredTimeDelayNotExpired";
  case UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED:
    return "uploadDownloadNotAccepted";
  case UDS_NRC_TRANSFER_DATA_SUSPENDED:
    return "transferDataSuspended";
  case UDS_NRC_GENERAL_PROGRAMMING_FAILURE:
    return "generalProgrammingFailure";
  case UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER:
    return "wrongBlockSequenceCounter";
  case UDS_NRC_RESPONSE_PENDING:
    return "requestCorrectlyReceivedResponsePending";
  case UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED_IN_SESSION:
    return "subFunctionNotSupportedInActiveSession";
  case UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION:
    return "serviceNotSupportedInActiveSession";
  default:
    return "unknown NRC";
  }
}
