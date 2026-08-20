#pragma once

#include <Arduino.h>

#include "station_data.h"

void motorBegin();
void motorMoveToStation(StationId station);
void motorUpdate();
bool motorIsMoving();
int32_t motorCurrentPosition();
int32_t motorTargetPosition();

