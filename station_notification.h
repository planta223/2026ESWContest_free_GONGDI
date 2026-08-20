#pragma once

#include "station_data.h"

// Common event boundary for every station source. Serial uses it now; a future
// API implementation can include this header and submit the same StationId.
void notifyStation(StationId station);

