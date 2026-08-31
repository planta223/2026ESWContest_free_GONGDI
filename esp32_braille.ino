#include <Arduino.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "audio.h"
#include "button.h"
#include "config.h"
#include "matrix_display.h"
#include "motor.h"
#include "station_data.h"
#include "station_notification.h"
#include "vibration.h"
#include "wifi_manager.h"

namespace {

StationId currentStation = StationId::INVALID;
char serialCommand[SERIAL_COMMAND_BUFFER_SIZE] = {};
size_t serialCommandLength = 0;
uint32_t lastSerialByteAt = 0;

void runStationNotification(StationId station);
void replayCurrentStation();
void handleSerialInput();
void processSerialCommand(char* command);
void printHelp();
void printStatus();

void runStationNotification(StationId station) {
  const StationInfo* info = getStationInfo(station);
  if (info == nullptr) {
#if DEBUG_GLOBAL
    Serial.println(F("[SYSTEM] Invalid station event"));
#endif
    return;
  }

  currentStation = station;
#if DEBUG_GLOBAL
  Serial.printf("[SYSTEM] Station event: %s (%s)\n", info->name, info->debugName);
#endif

  // Matrix and stepper always react immediately to a station change. Audio
  // and vibration can be restricted to physical-button guidance in config.h.
  matrixShowStation(station);
  motorMoveToStation(station);

  if (!AUDIO_BUTTON_ONLY_MODE) {
    audioPlayStation(station);
  } else {
#if DEBUG_AUDIO
    Serial.println(F("[AUDIO] Automatic station playback disabled (button only)"));
#endif
  }

  if (VIBRATION_BUTTON_ONLY_MODE) {
    vibrationStop();
#if DEBUG_VIBRATION
    Serial.println(F("[VIB] Automatic station start disabled (button only)"));
#endif
  } else if (info->vibrationEnabled) {
    vibrationStart();
  } else {
    vibrationStop();
#if DEBUG_VIBRATION
    Serial.println(F("[VIB] Disabled for this station"));
#endif
  }
}

void replayCurrentStation() {
  const StationInfo* info = getStationInfo(currentStation);
  if (info == nullptr) {
#if DEBUG_BUTTON
    Serial.println(F("[BUTTON] No current station"));
#endif
    return;
  }

#if DEBUG_GLOBAL
  Serial.printf("[SYSTEM] Replay current station: %s\n", info->debugName);
#endif
  matrixShowStation(currentStation);
  audioPlayStation(currentStation);
  if (info->vibrationEnabled) {
    vibrationStart();
  } else {
    vibrationStop();
#if DEBUG_VIBRATION
    Serial.println(F("[VIB] Disabled for this station"));
#endif
  }

  // Intentionally no motorMoveToStation() here. A physical button replays
  // visual/audio/tactile guidance without moving the braille selector.
}

void trimCommand(char*& begin) {
  while (*begin != '\0' && isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }

  char* end = begin + strlen(begin);
  while (end > begin && isspace(static_cast<unsigned char>(end[-1]))) {
    --end;
  }
  *end = '\0';
}

bool parseNumberCommand(const char* command, const char* prefix, uint8_t& number) {
  const size_t prefixLength = strlen(prefix);
  if (strncmp(command, prefix, prefixLength) != 0
      || !isspace(static_cast<unsigned char>(command[prefixLength]))) {
    return false;
  }

  const char* cursor = command + prefixLength;
  while (isspace(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }

  char* end = nullptr;
  const unsigned long value = strtoul(cursor, &end, 10);
  while (end != nullptr && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (cursor == end || end == nullptr || *end != '\0' || value > 255) {
    return false;
  }

  number = static_cast<uint8_t>(value);
  return true;
}

void processSerialCommand(char* commandStorage) {
  char* command = commandStorage;
  trimCommand(command);
  if (*command == '\0') {
    return;
  }

  // Command words are ASCII. Lower-casing them does not touch station UTF-8.
  for (char* p = command; *p != '\0'; ++p) {
    if (*p >= 'A' && *p <= 'Z') {
      *p = static_cast<char>(*p - 'A' + 'a');
    }
  }

  if (command[0] >= '1' && command[0] <= '5' && command[1] == '\0') {
    notifyStation(stationIdFromNumber(static_cast<uint8_t>(command[0] - '0')));
    return;
  }

  if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    printHelp();
    return;
  }
  if (strcmp(command, "status") == 0) {
    printStatus();
    return;
  }
  if (strcmp(command, "vib") == 0) {
    Serial.println(F("[VIB TEST] Vibration ON"));
    vibrationStart();
    return;
  }

  uint8_t number = 0;
  if (parseNumberCommand(command, "motor", number)) {
    const StationId station = stationIdFromNumber(number);
    if (station != StationId::INVALID) {
      Serial.printf("[MOTOR TEST] Move -> Position %u\n", number);
      motorMoveToStation(station);
    } else {
      Serial.println(F("[MOTOR TEST] Position must be 1..5"));
    }
    return;
  }

  if (parseNumberCommand(command, "audio", number)) {
    if (number >= 1 && number <= STATION_COUNT) {
      Serial.printf("[AUDIO TEST] Play %04u.mp3\n", number);
      audioPlayTrack(number);
    } else {
      Serial.println(F("[AUDIO TEST] Track must be 1..5"));
    }
    return;
  }

  if (parseNumberCommand(command, "matrix", number)) {
    const StationId station = stationIdFromNumber(number);
    if (station != StationId::INVALID) {
      const StationInfo* info = getStationInfo(station);
      Serial.printf("[MATRIX TEST] %s\n", info->name);
      matrixShowStation(station);
    } else {
      Serial.println(F("[MATRIX TEST] Station must be 1..5"));
    }
    return;
  }

  Serial.printf("[SYSTEM] Unknown command: %s\n", command);
  Serial.println(F("[SYSTEM] Type 'help' for available commands"));
}

void handleSerialInput() {
  const uint32_t now = millis();

  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    lastSerialByteAt = now;

    if (incoming == '\n') {
      serialCommand[serialCommandLength] = '\0';
      processSerialCommand(serialCommand);
      serialCommandLength = 0;
    } else if (incoming != '\r') {
      if (serialCommandLength < SERIAL_COMMAND_BUFFER_SIZE - 1) {
        serialCommand[serialCommandLength++] = incoming;
      } else {
        serialCommandLength = 0;
        Serial.println(F("[SYSTEM] Serial command too long; discarded"));
      }
    }
  }

  // Also accepts Arduino Serial Monitor's "No line ending" mode.
  if (serialCommandLength > 0
      && static_cast<uint32_t>(now - lastSerialByteAt) >= SERIAL_COMMAND_IDLE_MS) {
    serialCommand[serialCommandLength] = '\0';
    processSerialCommand(serialCommand);
    serialCommandLength = 0;
  }
}

void printHelp() {
  Serial.println();
  Serial.println(F("=== DotJabi Debug Commands ==="));
  Serial.println(F("1~5              Station integrated test"));
  Serial.println(F("motor 1..5       Move stepper to station position"));
  Serial.println(F("vib              Run vibration timer"));
  Serial.println(F("audio 1..5       Play /mp3/000N.mp3"));
  Serial.println(F("matrix 1..5      Display station glyphs"));
  Serial.println(F("status           Show module state"));
  Serial.println(F("help             Show this help"));
  Serial.println();
}

void printStatus() {
  const StationInfo* info = getStationInfo(currentStation);
  Serial.println(F("=== DotJabi Status ==="));
  if (info == nullptr) {
    Serial.println(F("Current station : NONE"));
  } else {
    Serial.printf("Current station : %u / %s (%s)\n",
                  stationNumber(currentStation), info->name, info->debugName);
  }
  Serial.printf("Audio ready    : %s\n", audioIsReady() ? "YES" : "NO");
  Serial.printf("Matrix active  : %s\n", matrixIsActive() ? "YES" : "NO");
  Serial.printf("Vibration      : %s\n", vibrationIsActive() ? "ON" : "OFF");
  Serial.printf("Button LED     : %s\n", buttonLedIsOn() ? "ON" : "OFF");
  Serial.printf("Motor          : %ld -> %ld (%s)\n",
                static_cast<long>(motorCurrentPosition()),
                static_cast<long>(motorTargetPosition()),
                motorIsMoving() ? "MOVING" : "IDLE");
  Serial.printf("Wi-Fi          : %s\n", wifiIsConnected() ? "CONNECTED" : "DISCONNECTED");
  Serial.printf("API mode       : %s\n", apiIsAutoMode() ? "AUTO" : "REMOTE");
  const uint16_t trackedTrain = apiTrackedTrainNumber();
  if (!apiIsAutoMode()) {
    Serial.println(F("API train      : INACTIVE"));
  } else if (trackedTrain == 0) {
    Serial.printf("API train      : WAITING AT STATION %u\n",
                  AUTO_ROUTE_START_STATION_NUMBER);
  } else if (apiIsRouteComplete()) {
    Serial.printf("API train      : %u (ROUTE COMPLETE)\n", trackedTrain);
  } else {
    Serial.printf("API train      : %u / NEXT STATION %u\n",
                  trackedTrain, apiNextStationNumber());
  }
  Serial.printf("Timetable      : %s%s\n",
                apiIsTimetableReady() ? "READY" : "NOT READY",
                apiIsRefreshing() ? " / REFRESHING" : "");
}

}  // namespace

void notifyStation(StationId station) {
  runStationNotification(station);
}

StationId currentStationId() {
  return currentStation;
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  const uint32_t serialWaitStartedAt = millis();
  while (!Serial
         && static_cast<uint32_t>(millis() - serialWaitStartedAt) < SERIAL_READY_WAIT_MS) {
    yield();
  }

  Serial.println();
  Serial.println(F("[SYSTEM] DotJabi starting"));

  motorBegin();
  vibrationBegin();
  buttonBegin();
  matrixBegin();
  audioBegin();
  wifiBegin();
  apiBegin();

  Serial.println(F("[SYSTEM] Ready - enter 1..5 or 'help'"));
}

void loop() {
  handleSerialInput();
  wifiUpdate();
  apiUpdate();

  buttonUpdate();
  if (buttonConsumePress()) {
    replayCurrentStation();
  }

  matrixUpdate();
  motorUpdate();
  vibrationUpdate();
  buttonLedUpdate();
  audioUpdate();
}
