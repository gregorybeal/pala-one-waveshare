#ifndef PALA_UI_SCREENSAVERS_H
#define PALA_UI_SCREENSAVERS_H

#include "src/config.h"                // SCREEN_W / SCREEN_H
#include "src/pure/arduino_compat.h"  // uint8_t, size_t

// ============================================================================
//  Multi-screensaver module — owns the rotation of 1-bit XBM images shown
//  on the e-ink before deep sleep.
//
//  Storage:
//    /screensavers/0.bin … /screensavers/7.bin    rotation slots (SCREENSAVER_BYTES each)
//    /sleep.bin                                   legacy single image (kept as fallback)
//
//  Each slot file is exactly SCREENSAVER_BYTES (SCREEN_W x SCREEN_H px, 1-bit,
//  LSB-first, SCREENSAVER_ROW_BYTES per row — the XBitmap format
//  Adafruit_GFX::drawXBitmap consumes). 200x200 → 25 bytes/row, 5000 bytes.
//
//  Mode (NVS key `cfg_ss_mode`):
//    Single   draw /sleep.bin if present; otherwise yield to the built-in icon
//    Cycle    advance through populated slots in order, persisted across sleeps
//    Shuffle  pick a random populated slot, avoiding immediate repeats
//
//  Sleep.cpp's drawSleepScreen() calls drawNext() first. If that returns
//  false (no slots populated or mode is Single), Sleep.cpp falls back to
//  /sleep.bin or the built-in icon.
// ============================================================================
namespace Screensavers {

constexpr int MAX_SLOTS         = 8;
constexpr int SCREENSAVER_ROW_BYTES = (SCREEN_W + 7) / 8;
constexpr int SCREENSAVER_BYTES     = SCREENSAVER_ROW_BYTES * SCREEN_H;  // 1-bit packed
// The web UI copy in src/lang/ (D_WEB_SCREENSAVER_SPECS, D_WEB_SLEEP_ERR_SIZE,
// D_WEB_SS_EDITOR_INTRO) states these numbers as literals — update it with
// this assert if the panel ever changes.
static_assert(SCREENSAVER_BYTES == 5000 && SCREENSAVER_ROW_BYTES == 25,
              "screensaver size changed: update the D_WEB_* strings in src/lang/");

enum class Mode : uint8_t { Single = 0, Cycle = 1, Shuffle = 2 };

// Read persisted mode + rotation pointer + last-shown slot from NVS into
// the module's internal state. Call once from setup() after `prefs.begin`.
void loadSettings();

// Apply + persist the rotation mode.
void  setMode(Mode m);
Mode  currentMode();

// Render the next screensaver image into the framebuffer (via gfx). The
// caller must have already prepared the canvas / paged the e-ink.
// Returns true if pixels were emitted; false means the caller should draw
// its own fallback (typically the built-in icon).
//
// Mode::Single tries /sleep.bin; Cycle/Shuffle walk the rotation slots.
bool drawNext();

// ---- Editor API: slot enumeration + R/W -------------------------------------

// True if /screensavers/<slot>.bin exists with the expected size.
bool slotExists(int slot);

// Number of slots currently populated (0..MAX_SLOTS).
int populatedCount();

// Index of the first unpopulated slot in [0, MAX_SLOTS), or -1 if all full.
int firstFreeSlot();

// Read slot bytes into `out` (must be SCREENSAVER_BYTES). Returns true iff
// the file existed and was the correct length.
bool readSlot(int slot, uint8_t out[SCREENSAVER_BYTES]);

// Promote a freshly-uploaded temp file to slot `slot`. The temp file is
// expected to be exactly SCREENSAVER_BYTES; on success the rotation
// pointer is reset so the new slot enters the cycle on the next sleep.
// Returns true on success (caller may report it to the user); on failure,
// leaves the existing slot untouched and removes the temp file.
bool installFromTemp(int slot, const String& tmpPath);

// Remove /screensavers/<slot>.bin if present + reset the rotation pointer.
// No-op if the slot isn't populated.
void deleteSlot(int slot);

// Slot's on-disk path. Exposed so the upload handler can stage to a sibling
// `.tmp` then call `installFromTemp`.
String slotPath(int slot);

}  // namespace Screensavers

#endif  // PALA_UI_SCREENSAVERS_H
