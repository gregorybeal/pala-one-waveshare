#include "src/hal/power.h"

#include <driver/gpio.h>

namespace Power {

// A press shorter than this is contact bounce, not a press.
static const uint32_t kDebounceMs = 30;

static bool     s_ignoreUntilRelease = false;
static bool     s_down               = false;
static bool     s_holdFired          = false;
static uint32_t s_downAtMs           = 0;

void earlyInit() {
  // Both rails were held through deep sleep (holdRailsForDeepSleep);
  // release the holds before touching the levels.
  gpio_hold_dis((gpio_num_t)VBAT_LATCH);
  pinMode(VBAT_LATCH, OUTPUT);
  digitalWrite(VBAT_LATCH, HIGH);

  gpio_hold_dis((gpio_num_t)AUDIO_PWR);
  pinMode(AUDIO_PWR, OUTPUT);
  digitalWrite(AUDIO_PWR, HIGH);  // active LOW — off

  pinMode(PWR_BTN, INPUT_PULLUP);
  s_ignoreUntilRelease = isButtonDown();
}

bool isButtonDown() {
  return digitalRead(PWR_BTN) == LOW;
}

ButtonEvent poll() {
  bool down = isButtonDown();
  uint32_t now = millis();

  if (s_ignoreUntilRelease) {
    if (!down) s_ignoreUntilRelease = false;
    return ButtonEvent::None;
  }

  if (down && !s_down) {
    s_down = true;
    s_holdFired = false;
    s_downAtMs = now;
    return ButtonEvent::None;
  }

  if (down) {
    if (!s_holdFired && (uint32_t)(now - s_downAtMs) >= POWER_OFF_HOLD_MS) {
      s_holdFired = true;
      return ButtonEvent::Hold;
    }
    return ButtonEvent::None;
  }

  if (s_down) {  // released
    s_down = false;
    if (!s_holdFired && (uint32_t)(now - s_downAtMs) >= kDebounceMs) {
      return ButtonEvent::Short;
    }
  }
  return ButtonEvent::None;
}

void releaseLatch() {
  gpio_hold_dis((gpio_num_t)VBAT_LATCH);
  digitalWrite(VBAT_LATCH, LOW);
  delay(200);  // on battery, the rail collapses somewhere in here
}

void holdRailsForDeepSleep() {
  gpio_hold_en((gpio_num_t)VBAT_LATCH);
  gpio_hold_en((gpio_num_t)AUDIO_PWR);
  // GPIO42 (AUDIO_PWR) is not an RTC pin; its hold only survives deep sleep
  // with the digital-domain hold switched on.
  gpio_deep_sleep_hold_en();
}

}  // namespace Power
