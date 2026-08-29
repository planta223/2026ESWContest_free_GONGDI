#pragma once

#include "station_data.h"

// Common main-loop event boundary for Serial, API AUTO detection and web REMOTE
// selection. It fans one validated StationId out to every output module.
void notifyStation(StationId station);

// Last station accepted by notifyStation(), or INVALID before the first event.
StationId currentStationId();
