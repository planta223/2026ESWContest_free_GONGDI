#include "vibration.h"

#include "config.h"

namespace {

bool active = false;
uint32_t startedAt = 0;

}  // namespace

void vibrationBegin() {
  pinMode(PIN_VIBRATION, OUTPUT);
  digitalWrite(PIN_VIBRATION, !VIBRATION_ACTIVE_LEVEL);
}

void vibrationStart() {
  // GPIO14 drives only the NPN transistor base through a resistor. The motor
  // must use its own supply and a flyback diode as documented in README.md.
  digitalWrite(PIN_VIBRATION, VIBRATION_ACTIVE_LEVEL);
  startedAt = millis();
  active = true;

#if DEBUG_VIBRATION
  Serial.println(F("[VIB] Start"));
#endif
}

void vibrationStop() {
  if (!active) {
    return;
  }

  digitalWrite(PIN_VIBRATION, !VIBRATION_ACTIVE_LEVEL);
  active = false;

#if DEBUG_VIBRATION
  Serial.println(F("[VIB] Stop"));
#endif
}

void vibrationUpdate() {
  // Unsigned subtraction remains valid when millis() wraps around.
  if (active && static_cast<uint32_t>(millis() - startedAt) >= VIBRATION_DURATION_MS) {
    vibrationStop();
  }
}

bool vibrationIsActive() {
  return active;
}

