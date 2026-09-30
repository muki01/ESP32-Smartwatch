/*
 * power.cpp - AXP2101 PMU: battery, charger and the side (power) key.
 *
 * A 100 ms LVGL timer reads the PMU's interrupt flags for the power key and, every
 * second (at once after a charger interrupt), the charger and battery state.
 *
 * Every register read is checked. A failed or implausible read (all bits set: plugged
 * and unplugged at the same time cannot happen) is ignored instead of being taken for
 * key presses and charger events. Plugging in and a finished charge are derived from the
 * charger state itself and must be seen on two reads in a row, so a single bad read can
 * never cause a popup, a toast or a notification.
 * The key's 6 s hard power-off is the PMU's own.
 */
#define XPOWERS_CHIP_AXP2101
#include "power.h"

#include <Arduino.h>
#include <Wire.h>
#include "XPowersLib.h"
#include "../core/settings.h"
#include "../core/system.h"

#define PMU_POLL_MS          100
#define PMU_STATE_EVERY      10     // polls -> the charger state every second
#define PMU_FULL_LEVEL       95     // charge finished at or above this level: "fully charged"

#define AXP_REG_STATUS1      0x00   // bit 5: VBUS good, bit 3: battery present
#define AXP_REG_STATUS2      0x01   // bits 7..5: charger (1 = charging), bit 3: VBUS off
#define AXP_REG_IRQ_STATUS   0x48   // 3 registers, write 1 to clear
#define AXP_REG_BAT_PERCENT  0xA4
#define AXP_IRQ1_PKEY_LONG   0x04
#define AXP_IRQ1_PKEY_SHORT  0x08
#define AXP_IRQ1_CHARGER     0xF0   // VBUS insert/remove, battery insert/remove
#define AXP_IRQ2_CHARGER     0x18   // charge done/start

static XPowersPMU pmu;
static bool pmu_ok;
static power_event_cb_t power_handler;
static PowerState power_state;             // confirmed state (seen twice)
static PowerState power_pending;           // last read, waiting for confirmation
static bool power_state_known;
static bool power_full_told;               // "fully charged" already reported for this plug-in
static int32_t power_warned_level = 101;   // lowest level already warned about

/* ================================ Checked register access ========================= */

static bool axp_read(uint8_t reg, uint8_t *buf, uint8_t len) {
  I2CGuard lock;
  Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)AXP2101_SLAVE_ADDRESS, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void axp_write(uint8_t reg, uint8_t value) {
  I2CGuard lock;
  Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static bool power_read_state(PowerState *s) {
  uint8_t st[2], pct;
  if (!axp_read(AXP_REG_STATUS1, st, 2) || !axp_read(AXP_REG_BAT_PERCENT, &pct, 1)) return false;
  s->battery = st[0] & 0x08;
  s->vbus = (st[0] & 0x20) && !(st[1] & 0x08);
  s->charging = s->battery && (st[1] >> 5) == 1;
  s->level = s->battery ? min((int32_t)pct, (int32_t)100) : -1;
  return true;
}

static bool power_same(const PowerState &a, const PowerState &b) {
  return a.battery == b.battery && a.vbus == b.vbus && a.charging == b.charging && abs(a.level - b.level) <= 1;
}

/* ================================ Events ========================================== */

static void power_emit(PowerEvent event) {
  if (power_handler) power_handler(event, &power_state);
}

// A new confirmed state: publish it and report what changed.
static void power_apply(const PowerState &s) {
  bool first = !power_state_known;
  PowerState old = power_state;
  power_state = s;
  power_state_known = true;
  subj_set(subj_battery, s.level);
  subj_set(subj_charging, s.charging ? 1 : 0);
  subj_set(subj_usb_power, s.vbus ? 1 : 0);
  if (first) return;  // the state at boot is not an event

  if (s.vbus && !old.vbus) {
    power_full_told = !s.charging && s.level >= PMU_FULL_LEVEL;  // plugged in full: nothing to announce later
    power_warned_level = 101;                                    // warn again after this charge
    power_emit(POWER_PLUGGED);
  }
  if (!s.vbus) power_full_told = false;
  if (s.vbus && old.vbus && old.charging && !s.charging && s.battery && s.level >= PMU_FULL_LEVEL && !power_full_told) {
    power_full_told = true;
    power_emit(POWER_CHARGED);
  }
  if (!s.vbus && s.level >= 0) {
    static const int32_t STEPS[] = { 20, 10 };
    for (int32_t step : STEPS) {
      if (s.level <= step && power_warned_level > step) {
        power_warned_level = step;
        power_emit(POWER_LOW);
      }
    }
  }
}

static void power_poll_state() {
  PowerState s;
  if (!power_read_state(&s)) return;
  if (!power_state_known) {  // boot: take it as it is
    power_pending = s;
    power_apply(s);
    return;
  }
  if (power_same(s, power_state)) {
    power_pending = s;
    if (s.level != power_state.level) power_apply(s);  // the level moved by 1 %: no event, just publish
    return;
  }
  if (power_same(s, power_pending)) power_apply(s);  // the same change twice in a row: real
  power_pending = s;
}

static void power_poll_cb(lv_timer_t *t) {
  static uint8_t polls;
  bool refresh = ++polls >= PMU_STATE_EVERY;

  uint8_t st[3];
  if (axp_read(AXP_REG_IRQ_STATUS, st, 3) && (st[0] | st[1] | st[2])) {
    bool valid = st[0] != 0xFF && st[1] != 0xFF && st[2] != 0xFF;
    for (int i = 0; i < 3; i++) {
      if (st[i]) axp_write(AXP_REG_IRQ_STATUS + i, st[i]);
    }
    if (valid) {
      if (st[1] & AXP_IRQ1_PKEY_LONG) power_emit(POWER_KEY_LONG);
      else if (st[1] & AXP_IRQ1_PKEY_SHORT) power_emit(POWER_KEY_SHORT);
      if ((st[1] & AXP_IRQ1_CHARGER) || (st[2] & AXP_IRQ2_CHARGER)) refresh = true;
    }
  }
  if (refresh) {
    polls = 0;
    power_poll_state();
  }
}

/* ================================ Public API ====================================== */

void power_init() {
  I2CGuard lock;
  pmu_ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);
  if (!pmu_ok) {
    Serial.println("[PMU] AXP2101 not found - running without battery management");
    return;
  }
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
  pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_400MA);
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);

  pmu.enableBattDetection();
  pmu.enableBattVoltageMeasure();
  pmu.enableVbusVoltageMeasure();
  pmu.enableSystemVoltageMeasure();
  pmu.enableTemperatureMeasure();

  pmu.clearIrqStatus();
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |
                XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
                XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |
                XPOWERS_AXP2101_BAT_CHG_START_IRQ | XPOWERS_AXP2101_BAT_CHG_DONE_IRQ);
  Serial.println("[PMU] AXP2101 ready");
}

void power_start(power_event_cb_t handler) {
  power_handler = handler;
  if (!pmu_ok) return;
  power_poll_state();
  lv_timer_create(power_poll_cb, PMU_POLL_MS, NULL);
}

float power_battery_voltage() {
  I2CGuard lock;
  return pmu_ok && pmu.isBatteryConnect() ? pmu.getBattVoltage() / 1000.0f : 0.0f;
}

float power_usb_voltage() {
  I2CGuard lock;
  return pmu_ok && pmu.isVbusIn() ? pmu.getVbusVoltage() / 1000.0f : 0.0f;
}

float power_temperature() {
  I2CGuard lock;
  return pmu_ok ? pmu.getTemperature() : 0.0f;
}

void power_shutdown() {
  Serial.println("[PMU] power off");
  delay(50);
  if (pmu_ok) {
    I2CGuard lock;
    pmu.shutdown();
  } else {
    esp_deep_sleep_start();
  }
}
