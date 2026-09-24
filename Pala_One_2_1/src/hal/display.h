#ifndef PALA_HAL_DISPLAY_H
#define PALA_HAL_DISPLAY_H

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "src/config.h"
#include "src/state.h"

// ============================================================================
//  Display adapter — lets Adafruit_GFX (and u8g2 on top of it) draw into the
//  panel's offscreen buffer. Rotation is applied by the panel driver
//  (EPD_ROTATION in config.h), so coordinates pass straight through.
//  Colour convention: 1 = ink (black), 0 = paper (white).
// ============================================================================
class EpdGFXAdapter : public Adafruit_GFX {
public:
  explicit EpdGFXAdapter(EInkDisplay& d)
    : Adafruit_GFX(SCREEN_W, SCREEN_H), disp(d) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    disp.drawPixel(x, y, color ? GxEPD_BLACK : GxEPD_WHITE);
  }

private:
  EInkDisplay& disp;
};

extern EpdGFXAdapter gfx;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================================
//  Drawing primitives
// ============================================================================

// Set up the device for any drawing pass: clear the offscreen buffer (unless
// the caller has already managed that) and put u8g2 into the project's
// transparent-foreground mode. Higher-level helpers (drawCenter,
// prepareMenuFrame in ui/widgets.h) call this internally.
void beginPageCanvas(bool clearMem = true);

#endif  // PALA_HAL_DISPLAY_H
