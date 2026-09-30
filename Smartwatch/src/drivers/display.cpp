/*
 * display.cpp - SH8601 AMOLED (QSPI) + FT3168 touch: the LVGL port.
 *
 * Rendering pipeline
 *   LVGL renders stripes into two internal-RAM DMA buffers (partial mode). A finished
 *   stripe is byte-swapped (the panel wants big-endian RGB565) and queued as a QSPI DMA
 *   transfer while LVGL renders the next one into the other buffer. The stripes are as
 *   tall as the free internal RAM allows (up to 64 lines, 1/7 of the screen): every
 *   stripe walks the whole widget tree again, so fewer, taller stripes are much faster -
 *   Espressif measures a severe slowdown below 1/10 of the screen.
 *
 * Touch
 *   The FT3168 runs in monitor mode (low power, reports touches) and pulls its interrupt
 *   line low when a finger lands. While a finger is down, LVGL polls it every frame; the
 *   rest of the time the bus stays quiet. With the screen off, the interrupt wakes the UI
 *   loop at once and src/ui/system/screen.cpp decides whether the tap wakes the screen.
 */
#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include "../core/system.h"

// ---- Panel -----------------------------------------------------------------------
#define DISP_SPI_HOST        SPI2_HOST
#define DISP_QSPI_HZ         (40 * 1000 * 1000)  // SH8601 QSPI clock
#define DISP_BUF_LINES_MAX   64                  // 368 x 64 x 2 B = 46 kB per buffer
#define DISP_BUF_LINES_MIN   24
#define DISP_RAM_RESERVE_KB  136                 // internal RAM the radios, audio and tasks need later
#define DISP_DMA_CHUNK       32000               // bytes per SPI transaction (hardware limit: 32 kB)
#define DISP_MAX_TRANS       ((LCD_WIDTH * DISP_BUF_LINES_MAX * 2 + DISP_DMA_CHUNK - 1) / DISP_DMA_CHUNK)
#define DISP_WAKE_MS         120                 // SH8601: 120 ms between SLPIN and SLPOUT / first frame

#define SH8601_SWRESET       0x01
#define SH8601_SLPIN         0x10
#define SH8601_SLPOUT        0x11
#define SH8601_NORON         0x13
#define SH8601_INVOFF        0x20
#define SH8601_DISPOFF       0x28
#define SH8601_DISPON        0x29
#define SH8601_CASET         0x2A
#define SH8601_PASET         0x2B
#define SH8601_RAMWR         0x2C
#define SH8601_MADCTL        0x36
#define SH8601_PIXFMT        0x3A
#define SH8601_BRIGHTNESS    0x51
#define SH8601_CTRLD1        0x53
#define SH8601_WCE           0x58

// ---- Touch -----------------------------------------------------------------------
#define FT3168_ADDR          0x38
#define FT3168_REG_POINTS    0x02   // TD_STATUS, then P1_XH (event flag in bits 7..6), P1_XL, P1_YH, P1_YL
#define FT3168_REG_ID        0xA0
#define FT3168_REG_POWER     0xA5
#define FT3168_EVENT_NONE    3

static spi_device_handle_t disp_spi;
static spi_transaction_ext_t disp_trans[DISP_MAX_TRANS];
static uint8_t disp_trans_queued;
static bool disp_burst_open;            // CS held low while a pixel burst is in flight
static uint32_t disp_on_ms;             // last SLPOUT
static uint32_t disp_off_ms;            // last SLPIN

static lv_display_t *disp;
static lv_indev_t *touch_indev;
static volatile bool touch_irq;
static bool touch_down;
static bool touch_suspended;
static bool touch_ignore;               // swallow the touch that woke the screen
static lv_point_t touch_last;

/* ================================ SH8601 QSPI ==================================== */

static inline void disp_cs(bool active) {
  gpio_set_level((gpio_num_t)LCD_CS, active ? 0 : 1);
}

// Collects every queued pixel transaction and releases CS.
static void disp_burst_finish() {
  while (disp_trans_queued) {
    spi_transaction_t *done;
    spi_device_get_trans_result(disp_spi, &done, portMAX_DELAY);
    disp_trans_queued--;
  }
  if (disp_burst_open) {
    disp_cs(false);
    disp_burst_open = false;
  }
}

static void disp_cmd(uint8_t cmd, const uint8_t *data, uint8_t len) {
  if (!disp_spi) return;
  disp_burst_finish();
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
  t.cmd = 0x02;  // SH8601 QSPI opcode: write register, 1-line data
  t.addr = (uint32_t)cmd << 8;
  if (len) {
    t.length = len * 8;
    if (len <= 4) {
      t.flags |= SPI_TRANS_USE_TXDATA;
      memcpy(t.tx_data, data, len);
    } else {
      t.tx_buffer = data;
    }
  }
  disp_cs(true);
  spi_device_polling_transmit(disp_spi, &t);
  disp_cs(false);
}

static void disp_cmd1(uint8_t cmd, uint8_t value) {
  disp_cmd(cmd, &value, 1);
}

static void disp_set_window(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  const uint8_t col[4] = { (uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2 };
  const uint8_t row[4] = { (uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2 };
  disp_cmd(SH8601_CASET, col, 4);
  disp_cmd(SH8601_PASET, row, 4);
  disp_cmd(SH8601_RAMWR, nullptr, 0);
}

// Queues a pixel burst (4-line data). Returns immediately; disp_burst_finish() waits.
static void disp_push_async(const uint8_t *data, size_t len) {
  disp_burst_finish();
  disp_cs(true);
  disp_burst_open = true;
  for (uint8_t i = 0; len && i < DISP_MAX_TRANS; i++) {
    size_t n = len > DISP_DMA_CHUNK ? DISP_DMA_CHUNK : len;
    spi_transaction_ext_t *t = &disp_trans[i];
    memset(t, 0, sizeof(*t));
    if (i == 0) {
      t->base.flags = SPI_TRANS_MODE_QIO;
      t->base.cmd = 0x32;          // SH8601 QSPI opcode: 4-line pixel data
      t->base.addr = 0x003C00;     // "memory write continue" after RAMWR
    } else {
      // Continuation: data phase only while CS stays low.
      t->base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    }
    t->base.tx_buffer = data;
    t->base.length = n * 8;
    if (spi_device_queue_trans(disp_spi, &t->base, portMAX_DELAY) == ESP_OK) disp_trans_queued++;
    data += n;
    len -= n;
  }
}

static bool disp_bus_init() {
  pinMode(LCD_CS, OUTPUT);
  digitalWrite(LCD_CS, HIGH);

  spi_bus_config_t bus;
  memset(&bus, 0, sizeof(bus));
  bus.data0_io_num = LCD_SDIO0;
  bus.data1_io_num = LCD_SDIO1;
  bus.sclk_io_num = LCD_SCLK;
  bus.data2_io_num = LCD_SDIO2;
  bus.data3_io_num = LCD_SDIO3;
  bus.data4_io_num = -1;
  bus.data5_io_num = -1;
  bus.data6_io_num = -1;
  bus.data7_io_num = -1;
  bus.max_transfer_sz = DISP_DMA_CHUNK + 16;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
  esp_err_t err = spi_bus_initialize(DISP_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.printf("[DISP] spi_bus_initialize failed: %s\n", esp_err_to_name(err));
    return false;
  }

  spi_device_interface_config_t dev;
  memset(&dev, 0, sizeof(dev));
  dev.command_bits = 8;
  dev.address_bits = 24;
  dev.mode = 0;
  dev.clock_source = SPI_CLK_SRC_DEFAULT;
  dev.clock_speed_hz = DISP_QSPI_HZ;
  dev.spics_io_num = -1;  // CS is driven by hand so a burst can span several transactions
  dev.flags = SPI_DEVICE_HALFDUPLEX;
  dev.queue_size = DISP_MAX_TRANS;
  err = spi_bus_add_device(DISP_SPI_HOST, &dev, &disp_spi);
  if (err != ESP_OK) {
    Serial.printf("[DISP] spi_bus_add_device failed: %s\n", esp_err_to_name(err));
    return false;
  }
  spi_device_acquire_bus(disp_spi, portMAX_DELAY);  // the panel is the only device on this bus
  return true;
}

static void disp_panel_init() {
  disp_cmd(SH8601_SWRESET, nullptr, 0);
  delay(200);
  disp_cmd(SH8601_SLPOUT, nullptr, 0);
  delay(120);
  disp_cmd(SH8601_NORON, nullptr, 0);
  disp_cmd(SH8601_INVOFF, nullptr, 0);
  disp_cmd1(SH8601_PIXFMT, 0x05);      // 16 bit/pixel
  disp_cmd1(SH8601_MADCTL, 0x00);
  disp_cmd1(SH8601_CTRLD1, 0x28);      // brightness control + smooth dimming
  disp_cmd1(SH8601_BRIGHTNESS, 0x00);  // dark until the first frame is out
  disp_cmd1(SH8601_WCE, 0x00);         // sunlight enhancement off
  disp_cmd(SH8601_DISPON, nullptr, 0);
  delay(10);
  disp_on_ms = millis();
}

void display_panel_on() {
  uint32_t since_off = millis() - disp_off_ms;
  if (since_off < DISP_WAKE_MS) delay(DISP_WAKE_MS - since_off);
  disp_cmd(SH8601_SLPOUT, nullptr, 0);
  delay(10);
  disp_cmd(SH8601_DISPON, nullptr, 0);
  disp_on_ms = millis();
}

void display_panel_off() {
  disp_cmd1(SH8601_BRIGHTNESS, 0x00);
  disp_cmd(SH8601_DISPOFF, nullptr, 0);
  disp_cmd(SH8601_SLPIN, nullptr, 0);
  disp_off_ms = millis();
}

// Perceptual brightness curve: 1..100 % -> 12..255, 0 = dark.
void display_set_brightness(int32_t percent) {
  uint8_t level = 0;
  if (percent > 0) {
    percent = constrain(percent, 1, 100);
    level = (uint8_t)(12 + (243L * percent * percent) / 10000);
  }
  disp_cmd1(SH8601_BRIGHTNESS, level);
}

uint32_t display_ms_since_panel_on() {
  return millis() - disp_on_ms;
}

/* ================================ LVGL display =================================== */

static void disp_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
  if (!disp_spi) return;  // no panel bus: nothing to send, flush_wait_cb returns at once
  uint32_t px = lv_area_get_size(area);
  lv_draw_rgb565_swap(px_map, px);
  disp_set_window(area->x1, area->y1, area->x2, area->y2);
  disp_push_async(px_map, px * 2);
  // Completion is reported through disp_flush_wait_cb, not lv_display_flush_ready().
}

static void disp_flush_wait_cb(lv_display_t *d) {
  disp_burst_finish();
}

// SH8601 needs areas starting on even coordinates with even width and height.
static void disp_rounder_cb(lv_event_t *e) {
  lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

static uint32_t disp_tick_cb() {
  return millis();
}

static void disp_delay_cb(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

#if LV_USE_LOG
static void disp_log_cb(lv_log_level_t level, const char *buf) {
  Serial.print(buf);
}
#endif

// Two DMA buffers as tall as internal RAM allows while DISP_RAM_RESERVE_KB stay free.
static void disp_alloc_buffers(void **buf1, void **buf2, uint32_t *bytes) {
  *buf1 = *buf2 = NULL;
  *bytes = 0;
  for (int lines = DISP_BUF_LINES_MAX; lines >= DISP_BUF_LINES_MIN; lines -= 8) {
    uint32_t size = LCD_WIDTH * lines * 2;
    uint32_t free_now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (free_now < 2 * size + DISP_RAM_RESERVE_KB * 1024 && lines > DISP_BUF_LINES_MIN) continue;
    void *a = heap_caps_aligned_alloc(4, size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b = a ? heap_caps_aligned_alloc(4, size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) : NULL;
    if (a && (b || lines == DISP_BUF_LINES_MIN)) {
      *buf1 = a;
      *buf2 = b;
      *bytes = size;
      Serial.printf("[DISP] draw buffers %d x %d lines (%u kB)\n", b ? 2 : 1, lines, (unsigned)((b ? 2 : 1) * size / 1024));
      return;
    }
    heap_caps_free(a);
    heap_caps_free(b);
  }
}

/* ================================ FT3168 touch =================================== */

static void IRAM_ATTR touch_isr() {
  touch_irq = true;
  system_ui_wake_from_isr();
}

static bool touch_read_regs(uint8_t reg, uint8_t *buf, uint8_t len) {
  I2CGuard lock;
  Wire.beginTransmission(FT3168_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)FT3168_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

// Reads the first touch point. Returns false on a bus error.
static bool touch_read_point(bool *pressed, uint8_t *event) {
  uint8_t buf[5];
  if (!touch_read_regs(FT3168_REG_POINTS, buf, sizeof(buf))) return false;
  uint8_t points = buf[0] & 0x0F;
  *event = buf[1] >> 6;
  *pressed = points > 0 && points <= 2;
  if (*pressed) {
    int32_t x = ((buf[1] & 0x0F) << 8) | buf[2];
    int32_t y = ((buf[3] & 0x0F) << 8) | buf[4];
    touch_last.x = constrain(x, 0, LCD_WIDTH - 1);
    touch_last.y = constrain(y, 0, LCD_HEIGHT - 1);
  }
  return true;
}

static void touch_init() {
  pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
  attachInterrupt(TOUCH_INT_PIN, touch_isr, FALLING);

  uint8_t id = 0;
  bool found = touch_read_regs(FT3168_REG_ID, &id, 1);
  I2CGuard lock;
  Wire.beginTransmission(FT3168_ADDR);
  Wire.write(FT3168_REG_POWER);
  Wire.write(0x01);  // monitor mode: low power while idle, still reports touches
  Wire.endTransmission();
  Serial.printf("[TOUCH] FT3x68 %s (id 0x%02X)\n", found ? "ready" : "NOT FOUND", id);
}

// Idle: no bus traffic at all, the interrupt tells when a finger lands.
// Pressed: the controller is polled every read period until the finger is lifted.
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  data->point = touch_last;
  data->state = LV_INDEV_STATE_RELEASED;
  if (touch_suspended || (!touch_irq && !touch_down)) return;
  touch_irq = false;

  bool pressed = false;
  uint8_t event;
  if (!touch_read_point(&pressed, &event)) pressed = false;
  touch_down = pressed;
  if (touch_ignore) {
    if (!pressed) touch_ignore = false;
    return;
  }
  data->point = touch_last;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void display_touch_suspend(bool suspend) {
  touch_suspended = suspend;
  if (!touch_indev) return;
  if (suspend) lv_indev_reset(touch_indev, NULL);  // no half-finished press survives the screen off
  lv_indev_enable(touch_indev, !suspend);
}

bool display_touch_irq_take() {
  if (!touch_irq) return false;
  touch_irq = false;
  return true;
}

// A finger is on the glass, or the last report was a tap (press or lift event): short
// taps are often over before the controller is read.
bool display_touch_detected() {
  bool pressed = false;
  uint8_t event = FT3168_EVENT_NONE;
  if (!touch_read_point(&pressed, &event)) return false;
  touch_down = pressed;
  return pressed || event != FT3168_EVENT_NONE;
}

void display_touch_ignore_until_release() {
  touch_ignore = true;
  touch_down = true;  // poll until the finger is lifted
}

/* ================================ Init =========================================== */

void display_init() {
  if (disp_bus_init()) disp_panel_init();
  touch_init();

  lv_init();
  lv_tick_set_cb(disp_tick_cb);
  lv_delay_set_cb(disp_delay_cb);
#if LV_USE_LOG
  lv_log_register_print_cb(disp_log_cb);
#endif

  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  void *buf1, *buf2;
  uint32_t bytes;
  disp_alloc_buffers(&buf1, &buf2, &bytes);
  if (!buf1) Serial.println("[DISP] no memory for draw buffers");
  lv_display_set_buffers(disp, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_flush_wait_cb(disp, disp_flush_wait_cb);
  lv_display_add_event_cb(disp, disp_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#if LV_USE_PERF_MONITOR
  lv_sysmon_hide_performance(disp);  // shown on demand (Settings > About)
#endif

  touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, touch_read_cb);
  lv_indev_set_display(touch_indev, disp);
}
