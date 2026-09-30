/*
 * net_worker.h - Background worker for network jobs (HTTP requests, mDNS queries).
 *
 * Jobs run one after another in a single task (stack in PSRAM), so the UI never waits
 * for the network and only one TLS session exists at a time. A job's `work` runs in the
 * worker (no LVGL calls there); its `done` runs afterwards in the UI loop with the same
 * argument.
 */
#pragma once

#include <stddef.h>

typedef void (*net_job_fn_t)(void *arg);

void net_worker_init();
bool net_submit(net_job_fn_t work, net_job_fn_t done, void *arg);  // false: the queue is full

// Blocking HTTP(S) request for jobs. body == NULL: GET, else POST with a JSON body.
// The response (NUL-terminated) goes to buf. Returns the HTTP status (200 ...) or -1
// with a short reason in err.
int net_http(const char *url, const char *body, char *buf, size_t cap, int timeout_ms, char *err, size_t err_len);
