/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 *
 * koan_test_v1: test control for conformance runners (WLCS) inside a
 * VitruvianOS session. See protocol/koan-test-v1.xml.
 *
 * Wayland thread only. The requests become KOAN_CMD_TEST_* commands for
 * app_server, which moves native windows and injects mouse events into its
 * EventDispatcher. sync is answered once app_server's fence has passed
 * every Wayland input looper (KOAN_CMD_TEST_FENCE_BEGIN/_PART).
 */

#ifndef KOAN_TEST_H
#define KOAN_TEST_H

#include <stdbool.h>

#include <wayland-server-core.h>

#include "koan_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The root-owned regular file that enables the global when app_server
 * starts. */
#define KOAN_TEST_CONTROL_FILE "/run/koan-test-control"

/* True when KOAN_TEST_CONTROL_FILE exists, is a regular file and is owned
 * by root, so an unprivileged user cannot turn input injection on. */
bool koan_test_control_requested(void);

/* Create / destroy the koan_test_v1 global. */
bool koan_test_control_create(struct wl_display *display);
void koan_test_control_destroy(void);

/* Handle a KOAN_CMD_TEST_FENCE_BEGIN/_PART on the Wayland thread. Returns
 * false for any other command. */
bool koan_test_handle_command(const struct koan_command *cmd);

#ifdef __cplusplus
}
#endif

#endif /* KOAN_TEST_H */
