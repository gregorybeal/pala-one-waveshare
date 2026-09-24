#include "src/ui/screens/update_screen.h"

#include <esp_system.h>
#include "src/config.h"
#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/hal/ota.h"
#include "src/hal/wifi.h"
#include "src/hal/wifi_provisioning.h"
#include "src/state.h"
#include "src/storage/wifi_creds.h"
#include "src/ui/font.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/widgets.h"

static constexpr const char* kKeyOtaChannel = "cfg_ota_channel";

// Static progress callback — called from OTA::download() during the blocking
// binary stream. Fires g_updateScreen.setProgress() which redraws at 10%.
static void onDownloadProgress(int pct) {
  g_updateScreen.setProgress(pct);
}

// ----------------------------------------------------------------------------
//  NVS helpers
// ----------------------------------------------------------------------------
void UpdateScreen::loadChannel() {
  stableChan_ = (prefs.getString(kKeyOtaChannel, "stable") == "stable");
}

void UpdateScreen::saveChannel() {
  prefs.putString(kKeyOtaChannel, stableChan_ ? "stable" : "dev");
}

// ----------------------------------------------------------------------------
//  Progress update (called during blocking download)
// ----------------------------------------------------------------------------
void UpdateScreen::setProgress(int pct) {
  progress_ = pct;
  if (pct % 10 == 0 || pct == 100) draw();
}

// ----------------------------------------------------------------------------
//  Exit
// ----------------------------------------------------------------------------
void UpdateScreen::exitToLibrary() {
  if (wifiStarted_) {
    wifiEnd();
    WifiProvisioning::notifyUploadSession(false);
    wifiStarted_ = false;
  }
  nextScreen = &g_libraryScreen;
}

// ----------------------------------------------------------------------------
//  Lifecycle
// ----------------------------------------------------------------------------
void UpdateScreen::onEnter() {
  loadChannel();
  focusItem_     = 0;
  remoteVersion_ = "";
  progress_      = 0;
  wifiStarted_   = false;

  if (!WifiCreds::has()) {
    phase_ = Phase::NoCreds;
  } else if (wifiStaBegin()) {
    wifiStarted_ = true;
    WifiProvisioning::notifyUploadSession(true);
    phase_      = Phase::Connecting;
    staStartMs_ = millis();
  } else {
    phase_ = Phase::ConnFailed;
  }
  draw();
}

void UpdateScreen::onIdleTick() {
  if (phase_ != Phase::Connecting) return;

  WifiStaResult r = wifiStaPoll(net_);
  if (r == WifiStaResult::Connected) {
    phase_ = Phase::Idle;
    draw();
    return;
  }
  if (r == WifiStaResult::Failed ||
      (uint32_t)(millis() - staStartMs_) > wifiStaBudgetMs()) {
    wifiEnd();
    WifiProvisioning::notifyUploadSession(false);
    wifiStarted_ = false;
    phase_ = Phase::ConnFailed;
    draw();
  }
}

// ----------------------------------------------------------------------------
//  Drawing
// ----------------------------------------------------------------------------
void UpdateScreen::draw() {
  prepareMenuFrame();
  Font::useBody();
  int y = drawSectionHeader(D_UPDATE_HEADER);

  // ---- Transient / single-message states -----------------------------------

  if (phase_ == Phase::NoCreds) {
    y = drawWrappedText(MARGIN_X, y, D_UPDATE_NO_CREDS_L1, 14);
    drawWrappedText(MARGIN_X, y, D_UPDATE_NO_CREDS_L2, 14);
    display.update();
    return;
  }
  if (phase_ == Phase::Connecting) {
    drawWrappedText(MARGIN_X, y, D_UPDATE_CONNECTING, 14);
    display.update();
    return;
  }
  if (phase_ == Phase::ConnFailed) {
    drawWrappedText(MARGIN_X, y, D_UPDATE_CONN_FAILED, 14);
    display.update();
    return;
  }
  if (phase_ == Phase::Checking) {
    drawWrappedText(MARGIN_X, y, D_UPDATE_CHECKING, 14);
    display.update();
    return;
  }
  if (phase_ == Phase::Downloading) {
    y = drawWrappedText(MARGIN_X, y, D_UPDATE_INSTALLING, 16);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", progress_);
    drawWrappedText(MARGIN_X, y, pctBuf, 14);
    display.update();
    return;
  }
  if (phase_ == Phase::RebootPrompt) {
    Font::useBold();
    y = drawWrappedText(MARGIN_X, y, D_UPDATE_REBOOT_MSG, 18);
    Font::useBody();
    drawWrappedText(MARGIN_X, y, D_UPDATE_REBOOT_HINT, 14);
    display.update();
    return;
  }

  // ---- Full UI (Idle, ServerFail, UpToDate, UpdateAvailable, DownloadFailed)

  bool hasInstall = (phase_ == Phase::UpdateAvailable ||
                     phase_ == Phase::DownloadFailed);
  int maxItems = hasInstall ? 4 : 3;
  if (focusItem_ >= maxItems) focusItem_ = 0;

  // Version line
  y = drawWrappedText(MARGIN_X, y, D_UPDATE_VERSION_PREFIX FW_VERSION, 16);

  // Channel label on its own line
  Font::useBody();
  y = drawWrappedText(MARGIN_X, y, D_UPDATE_CHANNEL_LABEL, 14);

  // Channel checkboxes — one per line; side by side overflows 200px in
  // the longer translations.
  const int boxW = u8g2.getUTF8Width("[x] ");
  const char* chanLabels[2] = { D_UPDATE_CHAN_STABLE, D_UPDATE_CHAN_DEV };
  for (int i = 0; i < 2; i++) {
    bool checked = (i == 0) ? stableChan_ : !stableChan_;
    Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(checked ? "[x] " : "[ ] ");
    if (focusItem_ == i) Font::useBold(); else Font::useBody();
    u8g2.setCursor(MARGIN_X + boxW, y);
    u8g2.print(chanLabels[i]);
    y += 14;
  }
  y += 2;

  // Check button
  if (focusItem_ == 2) Font::useBold(); else Font::useBody();
  y = drawWrappedText(MARGIN_X, y, D_UPDATE_BTN_CHECK, 20);

  // Status line — shown below the check button
  Font::useBody();
  if (phase_ == Phase::ServerFail) {
    y = drawWrappedText(MARGIN_X, y, D_UPDATE_SERVER_FAIL, 14);
  } else if (phase_ == Phase::UpToDate) {
    y = drawWrappedText(MARGIN_X, y, D_UPDATE_UP_TO_DATE, 14);
  } else if (phase_ == Phase::UpdateAvailable) {
    y = drawWrappedText(MARGIN_X, y, String(D_UPDATE_AVAILABLE_PREFIX) + remoteVersion_, 14);
  } else if (phase_ == Phase::DownloadFailed) {
    y = drawWrappedText(MARGIN_X, y, D_UPDATE_DOWNLOAD_FAILED, 14);
  }

  // Install button — shown when update is available or after failed download
  if (hasInstall) {
    if (focusItem_ == 3) Font::useBold(); else Font::useBody();
    drawWrappedText(MARGIN_X, y, D_UPDATE_BTN_INSTALL, 14);
  }

  display.update();
}

// ----------------------------------------------------------------------------
//  Input
// ----------------------------------------------------------------------------
void UpdateScreen::onButton(const ButtonEvent& e) {
  if (!e.any()) return;

  // Reboot prompt — only 2x is accepted; device has no manual reboot option.
  if (phase_ == Phase::RebootPrompt) {
    if (e.kind == ButtonEvent::Double) {
      esp_restart();
    }
    return;
  }

  if (e.kind == ButtonEvent::Triple) {
    exitToLibrary();
    return;
  }

  // Full UI phases only
  const bool fullUi = phase_ == Phase::Idle        ||
                      phase_ == Phase::ServerFail   ||
                      phase_ == Phase::UpToDate     ||
                      phase_ == Phase::UpdateAvailable ||
                      phase_ == Phase::DownloadFailed;
  if (!fullUi) return;

  if (e.kind == ButtonEvent::Short) {
    bool hasInstall = (phase_ == Phase::UpdateAvailable ||
                       phase_ == Phase::DownloadFailed);
    focusItem_ = (focusItem_ + 1) % (hasInstall ? 4 : 3);
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (focusItem_ == 0) {
      stableChan_ = true;
      saveChannel();
      draw();

    } else if (focusItem_ == 1) {
      stableChan_ = false;
      saveChannel();
      draw();

    } else if (focusItem_ == 2) {
      // Check: probe + manifest fetch
      phase_ = Phase::Checking;
      draw();
      if (OTA::isUpdateServerReachable()) {
        OtaCheckResult r = OTA::checkAvailable(stableChan_ ? "stable" : "dev");
        if (r.updateAvailable) {
          remoteVersion_ = r.remoteVersion;
          phase_         = Phase::UpdateAvailable;
        } else if (r.remoteVersion.length() > 0) {
          phase_ = Phase::UpToDate;
        } else {
          phase_ = Phase::ServerFail;
        }
      } else {
        phase_ = Phase::ServerFail;
      }
      clearButtonQueue();
      draw();

    } else if (focusItem_ == 3) {
      // Install: download + flash
      progress_ = 0;
      phase_    = Phase::Downloading;
      draw();
      OtaDownloadResult r = OTA::download(stableChan_ ? "stable" : "dev",
                                          onDownloadProgress);
      clearButtonQueue();
      if (r.success) {
        // Flash complete — tear down WiFi and drop CPU to 80 MHz before
        // showing the reboot prompt. Sleep is now allowed again.
        if (wifiStarted_) {
          wifiEnd();
          WifiProvisioning::notifyUploadSession(false);
          wifiStarted_ = false;
        }
        phase_ = Phase::RebootPrompt;
      } else {
        phase_ = Phase::DownloadFailed;
      }
      draw();
    }
    return;
  }
}
