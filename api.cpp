#include "api.h"

#include <Arduino.h>

#include "config.h"
#include "station_notification.h"

void apiBegin() {
  // TODO: Initialize a future subway-position/next-station data source here.
  // The API module must translate its data to StationId and then call the
  // notifyStation(StationId) interface currently exercised by Serial input.
#if DEBUG_API
  Serial.println(F("[API] Stub active (Serial input is the current event source)"));
#endif
}

void apiUpdate() {
  // TODO: Future non-blocking subway API implementation.
}
