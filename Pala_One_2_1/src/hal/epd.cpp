#include "src/hal/epd.h"

#include <SPI.h>
#include <driver/gpio.h>

// Waveshare boards put an RC on the panel's reset line and need a short
// reset pulse; GxEPD2's default 10ms leaves some of them stuck busy.
static const uint16_t kResetPulseMs = 2;

EInkDisplay::EInkDisplay()
  : gx_(Panel(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

void EInkDisplay::begin(bool initial) {
  // The rail is held off through deep sleep (prepareToSleep); release the
  // hold before driving it back on.
  gpio_hold_dis((gpio_num_t)EPD_PWR);
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, LOW);  // active LOW
  delay(10);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  gx_.init(0, initial, kResetPulseMs, false);
  gx_.setRotation(EPD_ROTATION);
  gx_.setFullWindow();
}

void EInkDisplay::update() {
  gx_.display(partial_);
}

void EInkDisplay::clear() {
  gx_.clearScreen();
}

void EInkDisplay::clearMemory() {
  gx_.fillScreen(GxEPD_WHITE);
}

void EInkDisplay::prepareToSleep() {
  gx_.hibernate();
  SPI.end();
  digitalWrite(EPD_PWR, HIGH);
  gpio_hold_en((gpio_num_t)EPD_PWR);
}
