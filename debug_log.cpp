#include <Arduino.h>
#include <freertos/FreeRTOS.h>

// Keep the real ESP32 HardwareSerial name visible in this implementation.
#define DEBUG_LOG_NO_SERIAL_ALIAS
#include "debug_log.h"

#include <string.h>

namespace {

constexpr size_t LOG_LINE_LENGTH = 192;
constexpr size_t LOG_LINE_COUNT = 64;

struct BufferedLogLine {
  char text[LOG_LINE_LENGTH];
};

BufferedLogLine logLines[LOG_LINE_COUNT] = {};
char currentLine[LOG_LINE_LENGTH] = {};
size_t currentLineLength = 0;
size_t logHead = 0;
size_t logTail = 0;
size_t bufferedLineCount = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void pushCurrentLine() {
  if (currentLineLength == 0) {
    return;
  }

  currentLine[currentLineLength] = '\0';
  memcpy(logLines[logHead].text, currentLine, currentLineLength + 1);
  logHead = (logHead + 1) % LOG_LINE_COUNT;
  if (bufferedLineCount == LOG_LINE_COUNT) {
    logTail = (logTail + 1) % LOG_LINE_COUNT;
  } else {
    ++bufferedLineCount;
  }
  currentLineLength = 0;
}

void bufferBytes(const uint8_t* buffer, size_t size) {
  portENTER_CRITICAL(&logMux);
  for (size_t index = 0; index < size; ++index) {
    const char value = static_cast<char>(buffer[index]);
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      pushCurrentLine();
      continue;
    }
    if (currentLineLength >= LOG_LINE_LENGTH - 1) {
      pushCurrentLine();
    }
    currentLine[currentLineLength++] = value;
  }
  portEXIT_CRITICAL(&logMux);
}

}  // namespace

MirroredSerial AppSerial;

void MirroredSerial::begin(uint32_t baudRate) {
  ::Serial.begin(baudRate);
}

MirroredSerial::operator bool() const {
  return true;
}

int MirroredSerial::available() {
  return ::Serial.available();
}

int MirroredSerial::read() {
  return ::Serial.read();
}

size_t MirroredSerial::write(uint8_t value) {
  const size_t written = ::Serial.write(value);
  bufferBytes(&value, 1);
  return written;
}

size_t MirroredSerial::write(const uint8_t* buffer, size_t size) {
  const size_t written = ::Serial.write(buffer, size);
  bufferBytes(buffer, size);
  return written;
}

bool debugLogPopLine(char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return false;
  }

  portENTER_CRITICAL(&logMux);
  if (bufferedLineCount == 0) {
    portEXIT_CRITICAL(&logMux);
    return false;
  }

  strncpy(output, logLines[logTail].text, outputSize - 1);
  output[outputSize - 1] = '\0';
  logTail = (logTail + 1) % LOG_LINE_COUNT;
  --bufferedLineCount;
  portEXIT_CRITICAL(&logMux);
  return true;
}
