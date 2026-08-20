#pragma once

#include <Arduino.h>

#include "station_data.h"

void matrixBegin();
void matrixShowStation(StationId station);
void matrixUpdate();
void matrixClear();
bool matrixIsActive();

