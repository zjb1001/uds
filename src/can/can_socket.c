/**
 * @file can_socket.c
 * @brief SocketCAN adapter layer implementation.
 *
 * Wraps the Linux SocketCAN AF_CAN / SOCK_RAW API to provide a clean,
 * error-enumerated interface for the rest of the UDS stack.
 *
 * Design notes:
 * - All blocking I/O uses select() so that a finite timeout can be applied
 *   without permanently altering the socket's blocking mode.
 * - No dynamic memory allocation; all buffers are caller-supplied.
 * - errno is preserved across calls to allow callers to inspect it.
 */

#include "uds_can.h"

#include <assert.h>
#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

/** Confirm UdsCanFilter is layout-compatible with struct can_filter. */
static_assert(sizeof(UdsCanFilter) == sizeof(struct can_filter),
              "UdsCanFilter must match struct can_filter layout");

/* ── Socket lifecycle ───────────────────────────────────────────────────── */

int uds_can_open(UdsCanSocket *sock, const char *ifname) {
  if (!sock || !ifname || ifname[0] == '\0') {
    return UDS_CAN_ERR_PARAM;
  }

  sock->fd = -1;

  /* 1. Create raw CAN socket */
  int fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    return UDS_CAN_ERR_SOCKET;
  }

  /* 2. Resolve interface index via POSIX if_nametoindex() */
  unsigned int ifindex = if_nametoindex(ifname);
  if (ifindex == 0) {
    close(fd);
    return UDS_CAN_ERR_IFINDEX;
  }

  /* 3. Bind to the interface */
  struct sockaddr_can addr;
  memset(&addr, 0, sizeof(addr));
  addr.can_family = AF_CAN;
  addr.can_ifindex = (int)ifindex;

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return UDS_CAN_ERR_BIND;
  }

  sock->fd = fd;
  return UDS_CAN_OK;
}

void uds_can_close(UdsCanSocket *sock) {
  if (!sock) {
    return;
  }
  if (sock->fd >= 0) {
    close(sock->fd);
    sock->fd = -1;
  }
}

/* ── Filter management ──────────────────────────────────────────────────── */

int uds_can_set_filter(UdsCanSocket *sock, const UdsCanFilter *filters,
                       unsigned int count) {
  if (!sock || sock->fd < 0) {
    return UDS_CAN_ERR_PARAM;
  }
  if (count > UDS_CAN_MAX_FILTERS) {
    return UDS_CAN_ERR_PARAM;
  }
  if (count > 0 && !filters) {
    return UDS_CAN_ERR_PARAM;
  }

  /* Reset to "receive all" when count == 0 and filters == NULL */
  if (count == 0) {
    /* Passing a NULL filter list with length 0 re-enables reception */
    if (setsockopt(sock->fd, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0) < 0) {
      return UDS_CAN_ERR_FILTER;
    }
    return UDS_CAN_OK;
  }

  /* UdsCanFilter is layout-compatible with struct can_filter */
  if (setsockopt(sock->fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters,
                 count * sizeof(*filters)) < 0) {
    return UDS_CAN_ERR_FILTER;
  }

  return UDS_CAN_OK;
}

/* ── Frame I/O ──────────────────────────────────────────────────────────── */

int uds_can_send(UdsCanSocket *sock, const struct can_frame *frame) {
  if (!sock || sock->fd < 0 || !frame) {
    return UDS_CAN_ERR_PARAM;
  }

  ssize_t written = write(sock->fd, frame, sizeof(*frame));
  if (written < 0) {
    return UDS_CAN_ERR_SEND;
  }
  if ((size_t)written != sizeof(*frame)) {
    return UDS_CAN_ERR_SEND;
  }

  return UDS_CAN_OK;
}

int uds_can_recv(UdsCanSocket *sock, struct can_frame *frame,
                 unsigned int timeout_ms) {
  if (!sock || sock->fd < 0 || !frame) {
    return UDS_CAN_ERR_PARAM;
  }

  if (timeout_ms > 0) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock->fd, &rfds);

    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000U);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U);

    int ready = select(sock->fd + 1, &rfds, NULL, NULL, &tv);
    if (ready < 0) {
      return UDS_CAN_ERR_RECV;
    }
    if (ready == 0) {
      return UDS_CAN_ERR_TIMEOUT;
    }
  }

  ssize_t nbytes = read(sock->fd, frame, sizeof(*frame));
  if (nbytes < 0) {
    return UDS_CAN_ERR_RECV;
  }
  if ((size_t)nbytes < sizeof(*frame)) {
    return UDS_CAN_ERR_TRUNC;
  }

  return UDS_CAN_OK;
}

/* ── UDS CAN ID helpers ─────────────────────────────────────────────────── */

uint32_t uds_can_req_id(uint8_t ecu_id) {
  if (ecu_id < UDS_CAN_ECU_ID_MIN || ecu_id > UDS_CAN_ECU_ID_MAX) {
    return 0;
  }
  return UDS_CAN_REQ_BASE + ecu_id;
}

uint32_t uds_can_resp_id(uint8_t ecu_id) {
  if (ecu_id < UDS_CAN_ECU_ID_MIN || ecu_id > UDS_CAN_ECU_ID_MAX) {
    return 0;
  }
  return UDS_CAN_RESP_BASE + ecu_id;
}

uint8_t uds_can_ecu_id_from_resp(uint32_t resp_id) {
  if (resp_id <= UDS_CAN_RESP_BASE) {
    return 0;
  }
  uint32_t offset = resp_id - UDS_CAN_RESP_BASE;
  if (offset < UDS_CAN_ECU_ID_MIN || offset > UDS_CAN_ECU_ID_MAX) {
    return 0;
  }
  return (uint8_t)offset;
}

unsigned int uds_can_ecu_filters(UdsCanFilter *filters, uint8_t ecu_id) {
  if (!filters) {
    return 0;
  }
  if (ecu_id < UDS_CAN_ECU_ID_MIN || ecu_id > UDS_CAN_ECU_ID_MAX) {
    return 0;
  }

  unsigned int n = 0;

  /* Physical-addressing request: Tester → ECU (exact match) */
  filters[n].can_id = UDS_CAN_REQ_BASE + ecu_id;
  filters[n].can_mask = 0x7FFU;
  n++;

  /* Functional-addressing broadcast: all ECUs */
  filters[n].can_id = UDS_CAN_FUNCTIONAL_ID;
  filters[n].can_mask = 0x7FFU;
  n++;

  /* Physical response: ECU → Tester (exact match) */
  filters[n].can_id = UDS_CAN_RESP_BASE + ecu_id;
  filters[n].can_mask = 0x7FFU;
  n++;

  return n;
}

/* ── Error string ───────────────────────────────────────────────────────── */

const char *uds_can_strerror(int err) {
  switch ((UdsCanError)err) {
  case UDS_CAN_OK:
    return "Success";
  case UDS_CAN_ERR_PARAM:
    return "Invalid parameter";
  case UDS_CAN_ERR_SOCKET:
    return "socket() failed";
  case UDS_CAN_ERR_BIND:
    return "bind() failed";
  case UDS_CAN_ERR_IFINDEX:
    return "Interface not found";
  case UDS_CAN_ERR_FILTER:
    return "setsockopt(CAN_RAW_FILTER) failed";
  case UDS_CAN_ERR_SEND:
    return "write() failed";
  case UDS_CAN_ERR_RECV:
    return "read() failed";
  case UDS_CAN_ERR_TIMEOUT:
    return "Receive timed out";
  case UDS_CAN_ERR_TRUNC:
    return "Received frame was truncated";
  default:
    return "Unknown error";
  }
}
