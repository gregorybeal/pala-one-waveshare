// ============================================================================
//  Pala One — firmware entry point.
//
//  The real firmware lives under src/ (hal/, pure/, storage/, ui/, web/).
//  This file exists because Arduino IDE requires a .ino with the same name
//  as the sketch folder.
//
//  Target hardware: Waveshare ESP32-S3-ePaper-1.54 (V2 — ESP32-S3-PICO-1-N8R8,
//  8MB flash, 8MB PSRAM, 200x200 SSD1681 panel). Pin map in src/config.h.
//
//  Build options:
//
//    - PlatformIO (recommended):
//        pio run -e waveshare-154-en -t upload
//
//    - Arduino IDE 2:
//        1. Install the esp32 board package (Espressif, 3.x) and pick
//           "ESP32S3 Dev Module".
//        2. Tools menu: Flash Size 8MB, PSRAM "OPI PSRAM", USB CDC On Boot
//           "Enabled", Partition Scheme "Custom" (uses partitions.csv here).
//        3. Install libraries: GxEPD2, Adafruit GFX, U8g2_for_Adafruit_GFX,
//           ArduinoJson, Improv-WiFi-Library.
//        4. Compile and upload.
// ============================================================================

// ── Language selection: uncomment exactly one (Arduino IDE) ─────────────────
//   PlatformIO users pick the env in platformio.ini (-en / -es leaf envs)
//   and can leave these defines commented out. Default if nothing is set:
//   English (with a #pragma message warning from src/config.h).
#define LANG_EN
// #define LANG_ES_LA
// ────────────────────────────────────────────────────────────────────────────

// ── Web UI default theme: uncomment exactly one ─────────────────────────────
//   The web UI has a light and a dark palette and a per-page toggle button.
//   Once a visitor picks one the choice is remembered in their browser's
//   localStorage and overrides whatever's set here — this define only picks
//   the *first-visit* default. Default if nothing is set: light.
//   PlatformIO users can also pass -D WEB_THEME_DARK in build_flags.
#define WEB_THEME_LIGHT
// #define WEB_THEME_DARK
// ────────────────────────────────────────────────────────────────────────────

// PlatformIO passes BOARD_WAVESHARE_154 in build_flags; Arduino IDE builds
// get it here so the rest of the firmware sees one consistent flag set.
#ifndef BOARD_WAVESHARE_154
  #define BOARD_WAVESHARE_154
#endif

#include <Arduino.h>
#include <esp_sleep.h>

#include "src/config.h"
#include "src/state.h"
#include "src/hal/battery.h"
#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/hal/power.h"
#include "src/hal/wifi_provisioning.h"
#include "src/pure/hashing.h"
#include "src/storage/app_catalog.h"
#include "src/storage/fs_util.h"
#include "src/storage/kosync_settings.h"
#include "src/storage/library.h"
#include "src/storage/list_items.h"
#include "src/storage/page_cache.h"
#include "src/storage/statistics.h"
#include "src/storage/wifi_creds.h"
#include "src/ui/font.h"
#include "src/ui/pala_api_impl.h"
#include "src/ui/reader.h"
#include "src/ui/reader_actions.h"  // Gestures::loadSettings
#include "src/ui/screen.h"
#include "src/ui/widgets.h"  // drawCenter
#include "src/ui/screens/about_screen.h"
#include "src/ui/screens/apps_screen.h"
#include "src/ui/screens/bookmarks/book_select_screen.h"
#include "src/ui/screens/bookmarks/bookmark_list_screen.h"
#include "src/ui/screens/bookmarks/preview_screen.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/screens/list_screen.h"
#include "src/ui/screens/reader_screen.h"
#include "src/ui/screens/statistics_screen.h"
#include "src/ui/screens/sync_screen.h"
#include "src/ui/screens/update_screen.h"
#include "src/ui/screens/upload_screen.h"
#include "src/ui/header_title.h"
#include "src/ui/lock.h"
#include "src/ui/screensavers.h"
#include "src/ui/sleep.h"
#include "src/ui/statusbar.h"
#include "src/ui/text.h"
#include "src/ui/toast.h"
#include "src/web/web.h"

// ============================================================================
//  Screen instances + current-screen pointer
// ============================================================================
LibraryScreen              g_libraryScreen;
ReaderScreen               g_readerScreen;
UploadScreen               g_uploadScreen;
AboutScreen                g_aboutScreen;
UpdateScreen               g_updateScreen;
SyncScreen                 g_syncScreen;
AppsScreen                 g_appsScreen;
ListScreen                 g_listScreen;
StatisticsScreen           g_statsScreen;
BookmarkBookSelectScreen   g_bmBookSelectScreen;
BookmarkListScreen         g_bmListScreen;
BookmarkPreviewScreen      g_bmPreviewScreen;

Screen* g_currentScreen = &g_libraryScreen;

// ============================================================================
//  Setup
// ============================================================================
// cppcheck-suppress unusedFunction
void setup() {
  // Latch battery power before anything else: on battery the board only
  // stays up past the PWR press once VBAT_LATCH is driven high.
  Power::earlyInit();

  Serial.begin(115200);
  delay(200);
  Serial.printf("[boot] wake cause: %d\n", esp_sleep_get_wakeup_cause());
  setCpuFrequencyMhz(240); // full speed for init; lowered to 80 MHz at end of setup

  pinMode(BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN), btnISR, CHANGE);

  // Button held through a deep-sleep wake: its down-edge predates the ISR, so seed
  // the press state manually. Pass 0 (not millis()) to credit the full boot
  // time; millis() ≈ 200 here (after delay(200)) would shorten the hold and
  // misclassify a Long press as Short.
  if (digitalRead(BTN) == LOW) {
    g_btns.seedPressOnWake(0);
  }

  u8g2.begin(gfx);

#if HAS_BATTERY
  adcSetupOnce();
  updateBatteryCached(true);
#endif

  // Load Sleep and Lock settings early — before display.clear() — so both
  // flags are available to gate the full-refresh boot clear below.
  prefs.begin("ereader", false);
  Sleep::loadSettings();
  Lock::loadSettings();

  // Skip the full-refresh boot clear when waking from deep sleep AND either:
  //   (a) the device is locked — the screensaver (with its lock badge) is
  //       already on the e-ink; clearing to white and then drawing nothing
  //       leaves a blank screen until the idle timeout fires, OR
  //   (b) no-screensaver mode is on and we were reading — the last reader
  //       page sits cleanly on the panel; a clear would briefly flash white
  //       before the page redraws.
  // On a fresh boot (not a button wake) always clear, regardless of lock
  // state. Deep sleep wakes on BOOT or PWR through ext1.
  bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1);
  bool wereReading   = (prefs.getString("wake_path", "").length() > 0);
  bool skipClear = wokeFromSleep &&
                   (Lock::isLocked() ||
                    (Sleep::noScreensaver() && wereReading));
  display.begin(/*initial=*/!skipClear);
  display.fastmodeOff();
  if (!skipClear) {
    display.clear();
  } else {
    // The controller lost its RAM in deep sleep, so a partial refresh has no
    // base image to diff against. Make the first real frame a full one.
    forceNextRenderFull();
    forceNextMenuFrameFull();
  }

  if (!fsBegin()) {
    drawCenter(D_BOOT_STORAGE_ERROR, D_BOOT_TRY_FACTORY_RESET);
    return;
  }
  ensureBooksDir();

  {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(AP_SSID, sizeof(AP_SSID), "PALA-%06llX", chipId & 0xFFFFFFULL);
  }

  Font::loadSettings();
  Screensavers::loadSettings();
  Statusbar::loadSettings();
  Gestures::loadSettings();
  WifiCreds::loadSettings();
  Kosync::loadSettings();
  HeaderTitle::loadSettings();
  // Sleep::loadSettings() and Lock::loadSettings() already ran earlier in
  // setup() so both flags were available for the boot-clear gate above —
  // don't reload them here.
  loadBooks();
  loadListItems();
  loadApps();
  initPalaAPI();
  registerWebRoutes();
  markUserActivity();

  // Reload lifetime counters from NVS into RTC RAM (no-op on warm wake).
  // Streak state bootstraps lazily on the first page turn.
  Statistics::loadOnBoot();

  // When locked at boot, we skip the screen draw and leave the sleep image
  // (with its lock indicator) on the e-ink. Otherwise a short-press wake
  // would render the reader/library over the screensaver while the loop is
  // still swallowing input — the device looks alive but ignores presses
  // until an unlock gesture. The unlock branch in loop() calls
  // g_currentScreen->draw() to paint the real screen once unlocked.
  if (tryRestoreReadingSession()) {
    g_currentScreen = &g_readerScreen;
    if (Lock::isLocked()) {
      // Keep the wake-press edges so a click-then-hold can wake AND unlock
      // in one motion. resetInputFrontend would otherwise drain them and
      // force the user to repeat the unlock gesture.
      markUserActivity();
    } else {
      renderCurrentPage();      // ~300ms draw — wake-press releases during this
      resetInputFrontend();     // discard the wake-press only
    }
  } else {
    g_currentScreen = &g_libraryScreen;
    if (Lock::isLocked()) {
      markUserActivity();
    } else {
      g_libraryScreen.onEnter();
      resetInputFrontend();
    }
  }

  // Drop to 80 MHz for normal operation — saves significant power.
  // Upload mode will raise it back to 240 MHz temporarily.
  setCpuFrequencyMhz(80);

  // Browser-side Wi-Fi provisioning over USB-CDC (Improv Serial under the
  // hood). Once registered, WifiProvisioning::loop() listens whenever a host
  // has the USB-CDC port open. No host = no listening = no battery cost. See
  // src/hal/wifi_provisioning.h for the contract.
  WifiProvisioning::begin();
}


// ============================================================================
//  Main loop
// ============================================================================
void loop() {
  // PWR button: hold = power off (always honoured — it's the user's hard
  // override); short press = sleep now, on screens that allow sleeping
  // (not mid-upload / mid-OTA).
  switch (Power::poll()) {
    case Power::ButtonEvent::Hold:
      Sleep::powerOff();
      return;
    case Power::ButtonEvent::Short:
      if (g_currentScreen->allowSleep() && !WifiProvisioning::isActive()) {
        Sleep::enter();
        return;
      }
      break;
    case Power::ButtonEvent::None:
      break;
  }

  g_btns.poll();
  maybeRecoverFromIsrOverflow();

  ButtonEvent ev = ButtonEvent::fromButtonState(g_btns);

  // Lifetime button-press counter. peekPressCount is monotonic-up except
  // when the apps API consumes-and-resets — guard by clamping lastSeen to
  // the current value if it ran backwards. Runs before the lock check so
  // physical presses count toward lifetime stats even when the UI is
  // swallowing them.
  {
    static uint32_t lastSeenPressCount = 0;
    uint32_t pc = g_btns.peekPressCount();
    if (pc < lastSeenPressCount) lastSeenPressCount = pc;
    if (pc != lastSeenPressCount) {
      Statistics::bumpButtons(pc - lastSeenPressCount);
      lastSeenPressCount = pc;
    }
  }

  // Locked: swallow all input except unlock gestures (Long/VeryLong/ClickHold).
  // Does NOT call markUserActivity for non-unlock events so the idle deadline
  // keeps ticking. cfg_locked persists in NVS so a re-sleep stays locked.
  //
  // Wake-press handling: the short press that woke the device re-appears
  // through the classifier in the first loop iteration. We absorb it silently
  // (s_lockedWakePressConsumed). A *subsequent* non-unlock press while still
  // locked means the user deliberately tapped again → re-enter deep sleep
  // immediately. A short 1500ms locked-idle timeout (independent of the user's
  // sleep setting) also returns to deep sleep so an accidental wake doesn't
  // leave the device on indefinitely.
  {
    static bool s_lockedWakePressConsumed = false;  // reset each deep-sleep wake

    if (Lock::isLocked()) {
      if (Lock::isUnlockGesture(ev)) {
        s_lockedWakePressConsumed = false;
        Lock::disengage();
        markUserActivity();
        Toast::show(D_TOAST_UNLOCKED);
        // Full refresh to clear screensaver ghosting on unlock.
        // forceNextRenderFull() overrides the reader's per-page fast-mode
        // decision so renderCurrentPage() uses fastmodeOff regardless of
        // pageTurnsSinceFull. forceNextMenuFrameFull() does the same for menu
        // screens whose prepareMenuFrame() would otherwise override fastmodeOff.
        forceNextRenderFull();
        forceNextMenuFrameFull();
        display.fastmodeOff();
        g_currentScreen->draw();
        return;
      }
      if (ev.any()) {
        if (!s_lockedWakePressConsumed) {
          s_lockedWakePressConsumed = true;   // absorb wake press
        } else {
          Sleep::enter();                     // second tap → back to screensaver
          return;
        }
      }
      // Short locked-idle: re-sleep after 1500ms with no input.
      // Don't sleep while the button is held — a Long-press unlock gesture
      // fires on release, so sleeping mid-hold would swallow the gesture.
      if (ENABLE_DEEP_SLEEP && g_currentScreen->allowSleep()
          && userIdleMs() > 1500 && !g_btns.isPressed()) {
        Sleep::enter();
        return;
      }
      return;
    }
    s_lockedWakePressConsumed = false;  // clear when unlocked so state is fresh on next lock
  }

  if (ev.any()) markUserActivity();

  if (ENABLE_DEEP_SLEEP && g_currentScreen->allowSleep() && !WifiProvisioning::isActive()) {
    if (userIdleMs() > Sleep::idleTimeoutMs()) {
      Sleep::enter();
      return;
    }
  }

  WifiProvisioning::loop();   // no-op unless a USB host is on the bus

  g_currentScreen->onButton(ev);
  g_currentScreen->onIdleTick();

  // Toast just expired? Repaint so its pixels actually disappear.
  if (Toast::clearIfExpired()) g_currentScreen->draw();

  if (g_currentScreen->nextScreen) {
    g_currentScreen = g_currentScreen->nextScreen;
    g_currentScreen->nextScreen = nullptr;
    g_currentScreen->onEnter();
  }

  // Light-sleep idle gating. The single biggest battery saver while reading:
  // between page turns the loop has nothing to do, so we drop the CPU until
  // either the button is pressed or a short timer fires for housekeeping.
  // Skipped on screens that need the CPU active (UploadScreen → SoftAP), and
  // mid-click-sequence — the classifier's trailing-silence wait runs against
  // millis(), and sleeping through it would add up to one tick interval of
  // latency per emit. Cost of staying awake during a click sequence is at
  // most ~550ms (MAX_CLICK_SEQUENCE_MS); the long quiet gaps between page
  // turns are where the battery savings actually come from.
  //
  // The `buttonQueueNonEmpty()` check closes a race: the ISR can queue a
  // release edge after this iter's `poll()` ran but before we reach this
  // gate — `clickCount_` is still 0 at that instant, but the edge is
  // sitting in the ring buffer waiting to be drained. Without the check we
  // sleep through it; the BOOT wake (level-low) doesn't fire on a release, so we'd
  // only re-process the edge on the next timer wake (~150ms later in the
  // worst case under the bound below).
  if (g_currentScreen->allowSleep()
      && !g_btns.hasPendingClicks()
      && !buttonQueueNonEmpty()
      && !WifiProvisioning::isActive()) {
    Sleep::idleLightSleep(Toast::isActive());
  }
}
