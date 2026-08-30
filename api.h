#pragma once

#include "station_data.h"

void apiBegin();
void apiUpdate();

void apiSetAutoMode(bool enabled);
bool apiIsAutoMode();

void apiRequestRefresh();
bool apiIsRefreshing();
bool apiIsTimetableReady();
StationId apiDetectedStation();
uint16_t apiTrackedTrainNumber();
uint8_t apiNextStationNumber();
bool apiIsRouteComplete();
