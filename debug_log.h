#pragma once

#include <Arduino.h>

// Mirrors application Serial output into a bounded line buffer. wifiUpdate()
// drains that buffer and publishes each line to connected WebSocket clients.
class MirroredSerial : public Print {
 public:
  void begin(uint32_t baudRate);
  explicit operator bool() const;
  int available();
  int read();

  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  using Print::write;
};

extern MirroredSerial AppSerial;

bool debugLogPopLine(char* output, size_t outputSize);

// Project sources include config.h after Arduino and third-party headers, so
// their existing Serial calls are mirrored without changing each log site.
#ifndef DEBUG_LOG_NO_SERIAL_ALIAS
#define Serial AppSerial
#endif
