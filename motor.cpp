#include "motor.h"

#include <AccelStepper.h>

#include "config.h"

namespace {

// 28BYJ-48 HALF4WIRE commonly requires the physical IN order 1-3-2-4.
// If the motor only vibrates, adjust this constructor order for the ULN2003 board.
AccelStepper stepper(
    AccelStepper::HALF4WIRE,
    PIN_STEPPER_IN1,
    PIN_STEPPER_IN3,
    PIN_STEPPER_IN2,
    PIN_STEPPER_IN4);

bool wasMoving = false;

}  // namespace

void motorBegin() {
  stepper.setMaxSpeed(MOTOR_MAX_SPEED);
  stepper.setAcceleration(MOTOR_ACCELERATION);
  stepper.enableOutputs();

  // There is no home sensor in the prototype. The physical boot position is
  // treated as the zero-step (Dongdaemun History & Culture Park) position.
  stepper.setCurrentPosition(0);

#if DEBUG_MOTOR
  Serial.println(F("[MOTOR] No homing sensor"));
  Serial.println(F("[MOTOR] Boot position assumed as ZERO"));
  Serial.printf("[MOTOR] Steps/revolution: %ld\n", static_cast<long>(MOTOR_STEPS_PER_REV));
#endif
}

void motorMoveToStation(StationId station) {
  const StationInfo* info = getStationInfo(station);
  if (info == nullptr) {
#if DEBUG_MOTOR
    Serial.println(F("[MOTOR] Invalid station"));
#endif
    return;
  }

  // Keep motor output independent from button/audio/vibration settings.
  // Explicitly enabling the outputs also recovers if a future code path calls
  // disableOutputs() to reduce idle current.
  stepper.enableOutputs();
  const int32_t currentStep = stepper.currentPosition();
  stepper.moveTo(info->motorTargetStep);
  wasMoving = stepper.distanceToGo() != 0;

#if DEBUG_MOTOR
  Serial.printf("[MOTOR] Move: %ld -> %ld (distance: %ld)\n",
                static_cast<long>(currentStep),
                static_cast<long>(info->motorTargetStep),
                static_cast<long>(stepper.distanceToGo()));
#endif
}

void motorUpdate() {
  // run() performs at most the currently due step and returns immediately.
  // Calling it every loop keeps the motor independent from all other timers.
  stepper.run();

  const bool moving = stepper.distanceToGo() != 0;
  if (wasMoving && !moving) {
#if DEBUG_MOTOR
    Serial.printf("[MOTOR] Arrived: %ld\n", static_cast<long>(stepper.currentPosition()));
#endif
  }
  wasMoving = moving;
}

bool motorIsMoving() {
  return stepper.distanceToGo() != 0;
}

int32_t motorCurrentPosition() {
  return stepper.currentPosition();
}

int32_t motorTargetPosition() {
  return stepper.targetPosition();
}
