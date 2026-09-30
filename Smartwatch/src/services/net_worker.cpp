/*
 * net_worker.cpp - A single background task for network jobs and the blocking HTTP
 * helper they use.
 */
#include "net_worker.h"

#include <Arduino.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "../core/system.h"

#define NET_TASK_STACK  12288   // TLS handshakes need ~10 kB of stack
#define NET_QUEUE_LEN   8

struct NetJob {
  net_job_fn_t work;
  net_job_fn_t done;
  void *arg;
};

static QueueHandle_t net_jobs;
static QueueHandle_t net_results;

static void net_task(void *arg) {
  NetJob job;
  for (;;) {
    if (xQueueReceive(net_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
    if (job.work) job.work(job.arg);
    xQueueSend(net_results, &job, portMAX_DELAY);
    system_ui_wake();
  }
}

// UI loop: the `done` half of every finished job.
static void net_results_cb(lv_timer_t *t) {
  NetJob job;
  while (xQueueReceive(net_results, &job, 0) == pdTRUE) {
    if (job.done) job.done(job.arg);
  }
}

void net_worker_init() {
  net_jobs = xQueueCreate(NET_QUEUE_LEN, sizeof(NetJob));
  net_results = xQueueCreate(NET_QUEUE_LEN, sizeof(NetJob));
  task_create_psram(net_task, "net", NET_TASK_STACK, NULL, 2, NULL, tskNO_AFFINITY);
  lv_timer_create(net_results_cb, 50, NULL);
}

bool net_submit(net_job_fn_t work, net_job_fn_t done, void *arg) {
  if (!net_jobs) return false;
  NetJob job = { work, done, arg };
  return xQueueSend(net_jobs, &job, 0) == pdTRUE;
}

int net_http(const char *url, const char *body, char *buf, size_t cap, int timeout_ms, char *err, size_t err_len) {
  esp_http_client_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.url = url;
  cfg.timeout_ms = timeout_ms;
  cfg.buffer_size = 1536;
  cfg.user_agent = "MukiWatch/" FW_VERSION;
  cfg.method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  if (!strncmp(url, "https:", 6)) cfg.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    strlcpy(err, "Out of memory", err_len);
    return -1;
  }
  int len = body ? (int)strlen(body) : 0;
  if (body) esp_http_client_set_header(client, "Content-Type", "application/json");
  int status = -1;
  if (esp_http_client_open(client, len) != ESP_OK) {
    strlcpy(err, "Not reachable", err_len);
  } else if (body && esp_http_client_write(client, body, len) != len) {
    strlcpy(err, "Send failed", err_len);
  } else if (esp_http_client_fetch_headers(client) < 0) {
    strlcpy(err, "No answer", err_len);
  } else {
    status = esp_http_client_get_status_code(client);
    size_t total = 0;
    while (buf && total + 1 < cap) {
      int r = esp_http_client_read(client, buf + total, cap - 1 - total);
      if (r <= 0) break;
      total += r;
    }
    if (buf && cap) buf[total] = 0;
    if (status != 200) snprintf(err, err_len, "Server error %d", status);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return status;
}
