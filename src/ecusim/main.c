/**
 * @file main.c
 * @brief ECU Simulator main entry point.
 *
 * Usage: ecusim [--interface <ifname>] [--ecu-id <id>] [--verbose]
 *
 * Defaults: interface=vcan0, ecu_id=1
 *
 * Runs until SIGTERM or SIGINT is received, then shuts down cleanly.
 */

#define _POSIX_C_SOURCE 200809L

#include "ecusim.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Global signal flag ─────────────────────────────────────────────────── */

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

/* ── Argument parsing ───────────────────────────────────────────────────── */

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--interface <if>] [--ecu-id <id>] [--verbose]\n"
          "  --interface  CAN interface (default: vcan0)\n"
          "  --ecu-id     ECU logical ID 1..127 (default: 1)\n"
          "  --verbose    Print request/response summaries to stderr\n",
          prog);
}

int main(int argc, char *argv[]) {
  const char *ifname = "vcan0";
  uint8_t ecu_id = 1U;
  bool verbose = false;

  /* Simple argument parsing */
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--interface") == 0 && i + 1 < argc) {
      ifname = argv[++i];
    } else if (strcmp(argv[i], "--ecu-id") == 0 && i + 1 < argc) {
      long id = strtol(argv[++i], NULL, 0);
      if (id < 1 || id > 127) {
        fprintf(stderr, "Error: ecu-id must be 1..127\n");
        return 1;
      }
      ecu_id = (uint8_t)id;
    } else if (strcmp(argv[i], "--verbose") == 0) {
      verbose = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  /* Install signal handlers for graceful shutdown */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  /* Initialise simulator */
  EcuSimulator sim;
  if (ecusim_init(&sim, ecu_id, ifname) != 0) {
    fprintf(stderr, "ecusim_init failed — is '%s' up?\n", ifname);
    return 1;
  }

  sim.verbose = verbose;

  printf("ecusim: ECU %u listening on %s (req=0x%03X resp=0x%03X)\n", ecu_id,
         ifname, sim.req_can_id, sim.resp_can_id);
  fflush(stdout);

  /* Run main loop until signal or ecusim_stop() */
  while (!g_stop && sim.running) {
    /* ecusim_run() loops internally; we break by clearing running */
    ecusim_run(&sim);
  }

  ecusim_cleanup(&sim);
  printf("ecusim: shutdown complete\n");
  return 0;
}
