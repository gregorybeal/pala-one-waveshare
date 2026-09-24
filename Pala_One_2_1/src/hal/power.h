#ifndef PALA_HAL_POWER_H
#define PALA_HAL_POWER_H

#include <Arduino.h>

#include "src/config.h"

// ============================================================================
//  Board power: battery latch, unused rails, and the PWR button.
//
//  On battery the board is powered up by the PWR button through a hardware
//  path that only lasts while the button is held; the firmware has to drive
//  VBAT_LATCH high to stay alive after release. Driving it low powers off.
//  On USB the latch does nothing (the board stays powered regardless).
//
//  The PWR button is not part of the click classifier (that's BOOT's job,
//  hal/input.h). It's polled from the main loop and yields at most one
//  coarse event per press: a short press, or a hold past POWER_OFF_HOLD_MS.
//  The main loop decides what those mean (sleep / power off).
// ============================================================================
namespace Power {

// Hold PWR this long to power off.
static const uint32_t POWER_OFF_HOLD_MS = 2000;

enum class ButtonEvent { None, Short, Hold };

// First thing in setup(): latch battery power on, turn off the audio rail,
// and configure the PWR button input. If PWR is already down (it's what
// powered us up, or what woke us), that press is ignored until released so
// it can't immediately send the device back to sleep.
void earlyInit();

// Debounced PWR button tracker. Call once per loop iteration. Hold fires
// while the button is still down (no need to wait for release).
ButtonEvent poll();

// Is PWR physically down right now? Light sleep uses this to avoid a
// level-triggered wake loop while the button is held.
bool isButtonDown();

// Release the battery latch. On battery this cuts power and never returns;
// on USB it returns and the caller should fall back to deep sleep.
void releaseLatch();

// Keep the latch and the audio rail at their current levels through deep
// sleep (digital GPIO state is otherwise lost when the digital domain
// powers down).
void holdRailsForDeepSleep();

}  // namespace Power

#endif  // PALA_HAL_POWER_H
