#pragma once

#include <Arduino.h>

#include "station_data.h"

void audioBegin();
void audioPlayStation(StationId station);
void audioPlayTrack(uint8_t track);
void audioUpdate();
bool audioIsReady();

