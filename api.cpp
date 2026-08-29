#include "api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "station_notification.h"

namespace {

constexpr uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;
constexpr size_t API_URL_RESERVE_SIZE = 192;
constexpr size_t API_FILTER_JSON_CAPACITY = 256;

struct TimetableCache {
  uint32_t arrivalSeconds[STATION_COUNT][MAX_TIMES];
  uint16_t arrivalCount[STATION_COUNT];
  bool ready;
};

TimetableCache timetableCaches[2] = {};
uint8_t activeCacheIndex = 0;
StaticSemaphore_t cacheMutexStorage;
SemaphoreHandle_t cacheMutex = nullptr;

TaskHandle_t apiTaskHandle = nullptr;
portMUX_TYPE requestMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool refreshRequested = false;
volatile bool refreshing = false;
volatile uint32_t lastSuccessfulRefreshAt = 0;

bool autoMode = true;
StationId detectedStation = StationId::INVALID;
StationId lastEvaluatedStation = StationId::INVALID;
uint32_t lastStationCheckAt = 0;

bool timeIsReady() {
  return time(nullptr) >= NTP_MIN_VALID_EPOCH;
}

bool getLocalClock(tm& localTime, int32_t& secondsOfDay) {
  const time_t now = time(nullptr);
  if (now < NTP_MIN_VALID_EPOCH || localtime_r(&now, &localTime) == nullptr) {
    return false;
  }
  secondsOfDay = localTime.tm_hour * 3600L + localTime.tm_min * 60L + localTime.tm_sec;
  return true;
}

uint8_t weekTag(const tm& localTime) {
  if (localTime.tm_wday == 0) {
    return 3;
  }
  if (localTime.tm_wday == 6) {
    return 2;
  }
  return 1;
}

bool parseArrivalTime(const char* value, uint32_t& seconds) {
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (value == nullptr
      || sscanf(value, "%d:%d:%d", &hour, &minute, &second) != 3
      || hour < 0 || hour > 47
      || minute < 0 || minute > 59
      || second < 0 || second > 59) {
    return false;
  }

  // Railway timetables can express after-midnight service as 24:xx.
  seconds = static_cast<uint32_t>(hour * 3600L + minute * 60L + second)
            % SECONDS_PER_DAY;
  return true;
}

bool fetchTimetable(uint8_t stationIndex,
                    uint8_t week,
                    TimetableCache& destination) {
  const StationInfo* station = getStationInfoByNumber(stationIndex + 1);
  if (station == nullptr || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  destination.arrivalCount[stationIndex] = 0;

  String url;
  url.reserve(API_URL_RESERVE_SIZE);
  url += SUBWAY_API_BASE_URL;
  url += SUBWAY_API_KEY;
  url += F("/json/");
  url += SUBWAY_API_SERVICE;
  url += F("/1/");
  url += String(MAX_TIMES);
  url += '/';
  url += station->subwayCode;
  url += '/';
  url += String(week);
  url += '/';
  url += String(TRAIN_DIRECTION);
  url += '/';

  HTTPClient http;
  http.setTimeout(API_HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
#if DEBUG_API
    Serial.printf("[API] HTTP begin failed: %s\n", station->debugName);
#endif
    return false;
  }

  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
#if DEBUG_API
    Serial.printf("[API] HTTP %d: %s\n", statusCode, station->debugName);
#endif
    http.end();
    return false;
  }

  StaticJsonDocument<API_FILTER_JSON_CAPACITY> filter;
  filter[SUBWAY_API_SERVICE]["row"][0]["ARRIVETIME"] = true;
  DynamicJsonDocument document(API_JSON_DOCUMENT_CAPACITY);
  const DeserializationError error = deserializeJson(
      document,
      http.getStream(),
      DeserializationOption::Filter(filter));

  if (error) {
#if DEBUG_API
    Serial.printf("[API] JSON error for %s: %s\n", station->debugName, error.c_str());
#endif
    http.end();
    return false;
  }

  JsonArray rows = document[SUBWAY_API_SERVICE]["row"].as<JsonArray>();
  if (rows.isNull()) {
#if DEBUG_API
    Serial.printf("[API] Timetable rows missing: %s\n", station->debugName);
#endif
    http.end();
    return false;
  }

  for (JsonObject row : rows) {
    if (destination.arrivalCount[stationIndex] >= MAX_TIMES) {
      break;
    }
    uint32_t seconds = 0;
    if (parseArrivalTime(row["ARRIVETIME"] | "", seconds)) {
      destination.arrivalSeconds[stationIndex]
                                    [destination.arrivalCount[stationIndex]++] = seconds;
    }
  }
  http.end();

#if DEBUG_API
  Serial.printf("[API] %s(%s): %u arrivals\n",
                station->name,
                station->subwayCode,
                destination.arrivalCount[stationIndex]);
#endif
  return true;
}

bool fetchAllTimetables(TimetableCache& destination) {
  tm localTime = {};
  int32_t ignoredSeconds = 0;
  if (!getLocalClock(localTime, ignoredSeconds)) {
    return false;
  }

  memset(&destination, 0, sizeof(destination));
  const uint8_t todayWeekTag = weekTag(localTime);

#if DEBUG_API
  Serial.printf("[API] Timetable refresh started (week=%u, direction=%u)\n",
                todayWeekTag, TRAIN_DIRECTION);
#endif

  for (uint8_t stationIndex = 0; stationIndex < STATION_COUNT; ++stationIndex) {
    if (!fetchTimetable(stationIndex, todayWeekTag, destination)) {
      return false;
    }
    if (stationIndex + 1 < STATION_COUNT) {
      vTaskDelay(pdMS_TO_TICKS(API_INTER_REQUEST_DELAY_MS));
    }
  }
  destination.ready = true;
  return true;
}

void setRefreshing(bool value) {
  portENTER_CRITICAL(&requestMux);
  refreshing = value;
  portEXIT_CRITICAL(&requestMux);
}

bool consumeRefreshRequest() {
  portENTER_CRITICAL(&requestMux);
  const bool requested = refreshRequested;
  if (requested) {
    refreshRequested = false;
  }
  portEXIT_CRITICAL(&requestMux);
  return requested;
}

void restoreRefreshRequest() {
  portENTER_CRITICAL(&requestMux);
  refreshRequested = true;
  portEXIT_CRITICAL(&requestMux);
}

void apiWorkerTask(void*) {
  for (;;) {
    if (!consumeRefreshRequest()) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    if (WiFi.status() != WL_CONNECTED || !timeIsReady()) {
      restoreRefreshRequest();
      vTaskDelay(pdMS_TO_TICKS(API_RETRY_INTERVAL_MS));
      continue;
    }

    setRefreshing(true);
    uint8_t targetCacheIndex = 0;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    targetCacheIndex = static_cast<uint8_t>(1U - activeCacheIndex);
    xSemaphoreGive(cacheMutex);

    const bool success = fetchAllTimetables(timetableCaches[targetCacheIndex]);
    if (success) {
      xSemaphoreTake(cacheMutex, portMAX_DELAY);
      activeCacheIndex = targetCacheIndex;
      xSemaphoreGive(cacheMutex);
      lastSuccessfulRefreshAt = millis();
#if DEBUG_API
      Serial.println(F("[API] Timetable refresh complete"));
#endif
    } else {
      restoreRefreshRequest();
#if DEBUG_API
      Serial.println(F("[API] Timetable refresh failed; retry scheduled"));
#endif
    }
    setRefreshing(false);

    if (!success) {
      vTaskDelay(pdMS_TO_TICKS(API_RETRY_INTERVAL_MS));
    }
  }
}

uint32_t circularTimeDifference(uint32_t lhs, uint32_t rhs) {
  uint32_t difference = lhs > rhs ? lhs - rhs : rhs - lhs;
  if (difference > SECONDS_PER_DAY / 2) {
    difference = SECONDS_PER_DAY - difference;
  }
  return difference;
}

StationId findCurrentStation(uint32_t nowSeconds) {
  if (cacheMutex == nullptr) {
    return StationId::INVALID;
  }

  StationId bestStation = StationId::INVALID;
  uint32_t bestDifference = UINT32_MAX;

  xSemaphoreTake(cacheMutex, portMAX_DELAY);
  const TimetableCache& cache = timetableCaches[activeCacheIndex];
  if (cache.ready) {
    // Higher station codes are visited first and retain exact ties, matching
    // the legacy AUTO-mode priority policy.
    for (int stationIndex = STATION_COUNT - 1; stationIndex >= 0; --stationIndex) {
      for (uint16_t arrivalIndex = 0;
           arrivalIndex < cache.arrivalCount[stationIndex];
           ++arrivalIndex) {
        const uint32_t difference = circularTimeDifference(
            cache.arrivalSeconds[stationIndex][arrivalIndex], nowSeconds);
        if (difference < bestDifference) {
          bestDifference = difference;
          bestStation = stationIdFromNumber(static_cast<uint8_t>(stationIndex + 1));
        }
      }
    }
  }
  xSemaphoreGive(cacheMutex);

  return bestDifference <= static_cast<uint32_t>(STATION_WINDOW)
             ? bestStation
             : StationId::INVALID;
}

}  // namespace

void apiBegin() {
  cacheMutex = xSemaphoreCreateMutexStatic(&cacheMutexStorage);
  if (cacheMutex == nullptr) {
#if DEBUG_API
    Serial.println(F("[API] Cache mutex creation failed"));
#endif
    return;
  }

  const BaseType_t created = xTaskCreate(
      apiWorkerTask,
      "subway-api",
      API_TASK_STACK_BYTES,
      nullptr,
      API_TASK_PRIORITY,
      &apiTaskHandle);
  if (created != pdPASS) {
    apiTaskHandle = nullptr;
#if DEBUG_API
    Serial.println(F("[API] Background task creation failed"));
#endif
    return;
  }

#if DEBUG_API
  Serial.println(F("[API] Background timetable task ready"));
#endif
  apiRequestRefresh();
}

void apiUpdate() {
  const uint32_t now = millis();
  if (apiIsTimetableReady()
      && !apiIsRefreshing()
      && static_cast<uint32_t>(now - lastSuccessfulRefreshAt) >= API_REFRESH_INTERVAL_MS) {
    apiRequestRefresh();
  }

  if (!autoMode
      || static_cast<uint32_t>(now - lastStationCheckAt) < API_STATION_CHECK_INTERVAL_MS) {
    return;
  }
  lastStationCheckAt = now;

  tm localTime = {};
  int32_t secondsOfDay = 0;
  if (!getLocalClock(localTime, secondsOfDay) || !apiIsTimetableReady()) {
    detectedStation = StationId::INVALID;
    return;
  }

  const StationId station = findCurrentStation(static_cast<uint32_t>(secondsOfDay));
  detectedStation = station;
  if (station == lastEvaluatedStation) {
    return;
  }

  lastEvaluatedStation = station;
  if (station != StationId::INVALID) {
    // The worker only publishes cache data. Hardware fan-out remains in loop().
    notifyStation(station);
  }
}

void apiSetAutoMode(bool enabled) {
  if (autoMode == enabled) {
    return;
  }
  autoMode = enabled;
  detectedStation = StationId::INVALID;
  lastEvaluatedStation = StationId::INVALID;
  lastStationCheckAt = 0;

#if DEBUG_API
  Serial.printf("[API] Mode: %s\n", autoMode ? "AUTO" : "REMOTE");
#endif
}

bool apiIsAutoMode() {
  return autoMode;
}

void apiRequestRefresh() {
  portENTER_CRITICAL(&requestMux);
  refreshRequested = true;
  portEXIT_CRITICAL(&requestMux);
  if (apiTaskHandle != nullptr) {
    xTaskNotifyGive(apiTaskHandle);
  }
}

bool apiIsRefreshing() {
  portENTER_CRITICAL(&requestMux);
  const bool value = refreshing;
  portEXIT_CRITICAL(&requestMux);
  return value;
}

bool apiIsTimetableReady() {
  if (cacheMutex == nullptr) {
    return false;
  }
  xSemaphoreTake(cacheMutex, portMAX_DELAY);
  const bool ready = timetableCaches[activeCacheIndex].ready;
  xSemaphoreGive(cacheMutex);
  return ready;
}

StationId apiDetectedStation() {
  return detectedStation;
}
