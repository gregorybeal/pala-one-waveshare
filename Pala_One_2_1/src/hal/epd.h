#ifndef PALA_HAL_EPD_H
#define PALA_HAL_EPD_H

#include <GxEPD2_BW.h>

#include "src/config.h"

// ============================================================================
//  E-paper panel driver — Waveshare ESP32-S3-ePaper-1.54 (SSD1681, 200x200).
//
//  Wraps GxEPD2 behind the small surface the firmware was written against
//  (update / fastmodeOn / fastmodeOff / clear / clearMemory / drawPixel), so
//  screens don't know which driver library sits underneath. All drawing goes
//  through the Adafruit_GFX adapter in hal/display.h, which lands here via
//  drawPixel(); nothing reaches the GxEPD2 object directly.
//
//  Refresh model: "fast mode" = partial refresh (no flash, some ghosting),
//  otherwise a full refresh. The mode is sticky — callers pick it right
//  before building a frame, then call update() once the frame is drawn.
// ============================================================================
class EInkDisplay {
public:
  using Panel = GxEPD2_154_D67;

  EInkDisplay();

  // Power the panel rail, bring up SPI and initialise the controller.
  // `initial` = true on a cold boot (the panel content is unknown, so the
  // first refresh must be full). Pass false when waking with an image we
  // want to keep on the glass.
  void begin(bool initial);

  void fastmodeOn()  { partial_ = true; }
  void fastmodeOff() { partial_ = false; }

  // Push the offscreen buffer to the panel (partial or full per fastmode).
  void update();

  // Full-refresh the panel to white (and the buffer with it).
  void clear();

  // Clear only the offscreen buffer; the panel is untouched until update().
  void clearMemory();

  // Legacy orientation hook from the Heltec driver. Rotation is fixed at
  // begin() (EPD_ROTATION), so this is a no-op kept for call-site parity.
  void landscape() {}

  // Colour is GxEPD_BLACK / GxEPD_WHITE.
  void drawPixel(int16_t x, int16_t y, uint16_t color) { gx_.drawPixel(x, y, color); }

  // Put the controller into deep sleep and cut + hold the panel rail off,
  // ready for the MCU's own deep sleep.
  void prepareToSleep();

private:
  GxEPD2_BW<Panel, Panel::HEIGHT> gx_;
  bool partial_ = false;
};

#endif  // PALA_HAL_EPD_H
