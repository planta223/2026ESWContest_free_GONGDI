#include "button.h"

#include "config.h"

namespace {

uint8_t lastRawState = HIGH;
uint8_t stableState = HIGH;
uint32_t rawChangedAt = 0;
bool pressPending = false;

bool ledOn = false;
uint32_t ledStartedAt = 0;

}  // namespace

void buttonBegin() {
  // Internal pull-up: released=HIGH, pressed=LOW. Polling plus a stable-time
  // check removes contact bounce without an interrupt or blocking delay.
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LED, OUTPUT);
  digitalWrite(PIN_BUTTON_LED, LOW);

  lastRawState = digitalRead(PIN_BUTTON);
  stableState = lastRawState;
  rawChangedAt = millis();
}

void buttonUpdate() {
  const uint32_t now = millis();
  const uint8_t rawState = digitalRead(PIN_BUTTON);

  if (rawState != lastRawState) {
    lastRawState = rawState;
    rawChangedAt = now;
  }

  if (rawState != stableState
      && static_cast<uint32_t>(now - rawChangedAt) >= BUTTON_DEBOUNCE_MS) {
    stableState = rawState;
    if (stableState == LOW) {
      pressPending = true;
      ledOn = true;
      ledStartedAt = now;
      digitalWrite(PIN_BUTTON_LED, HIGH);
#if DEBUG_BUTTON
      Serial.println(F("[BUTTON] Press"));
#endif
    }
  }
}

bool buttonConsumePress() {
  if (!pressPending) {
    return false;
  }
  pressPending = false;
  return true;
}

void buttonLedUpdate() {
  if (ledOn
      && static_cast<uint32_t>(millis() - ledStartedAt) >= BUTTON_LED_ON_MS) {
    digitalWrite(PIN_BUTTON_LED, LOW);
    ledOn = false;
#if DEBUG_BUTTON
    Serial.println(F("[BUTTON] LED off"));
#endif
  }
}

bool buttonLedIsOn() {
  return ledOn;
}

