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

#include "api_usage.h"
#include "config.h"
#include "station_notification.h"

namespace {

constexpr uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;
constexpr size_t API_URL_RESERVE_SIZE = 192;
constexpr size_t API_FILTER_JSON_CAPACITY = 384;
constexpr size_t REALTIME_FILTER_JSON_CAPACITY = 512;
constexpr uint16_t INVALID_TRAIN_NUMBER = 0;
constexpr uint8_t REALTIME_STATUS_DEPARTED = 2;
constexpr uint8_t REALTIME_STATUS_PREVIOUS_STATION_DEPARTED = 3;
constexpr size_t DEPARTURE_UPDATE_DELAY_COUNT =
    sizeof(DEPARTURE_UPDATE_DELAY_SEC) / sizeof(DEPARTURE_UPDATE_DELAY_SEC[0]);

static_assert(AUTO_ROUTE_START_STATION_NUMBER >= 1
                  && AUTO_ROUTE_START_STATION_NUMBER <= STATION_COUNT,
              "AUTO route start station must be in the station table");
static_assert(DEPARTURE_UPDATE_DELAY_COUNT == STATION_COUNT - 1,
              "A departure update delay is required for each route segment");
static_assert(STATION_WINDOW >= 0, "Station window must not be negative");
static_assert(TRAIN_DIRECTION == 1 || TRAIN_DIRECTION == 2,
              "Train direction must be 1 or 2");
static_assert(REALTIME_API_POLL_INTERVAL_MS > 0,
              "Realtime polling interval must be positive");
static_assert(REALTIME_API_DAILY_LIMIT > 0,
              "Realtime daily limit must be positive");

struct TimetableCache {
  uint32_t departureUpdateSeconds[STATION_COUNT][MAX_TIMES];
  uint16_t trainNumbers[STATION_COUNT][MAX_TIMES];
  uint16_t departureCount[STATION_COUNT];
  bool ready;
};

struct TimetableMatch {
  StationId station;
  uint16_t trainNumber;
};

enum class RealtimePollOutcome : uint8_t {
  SUCCESS,
  FAILED,
  LIMIT_REACHED
};

struct RealtimeObservation {
  StationId station;
  uint16_t trainNumber;
  uint8_t trainStatus;
};

struct RealtimePollResult {
  RealtimePollOutcome outcome;
  RealtimeObservation observation;
  uint32_t trackingGeneration;
  uint32_t usageCount;
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
volatile bool realtimePollRequested = false;
volatile bool realtimePolling = false;
volatile uint16_t realtimeRequestedTrainNumber = INVALID_TRAIN_NUMBER;
volatile uint32_t realtimeRequestedGeneration = 0;

RealtimePollResult realtimeResult = {
    RealtimePollOutcome::FAILED,
    {StationId::INVALID, INVALID_TRAIN_NUMBER, 0},
    0,
    0};
bool realtimeResultAvailable = false;

bool autoMode = true;
StationId detectedStation = StationId::INVALID;
uint16_t trackedTrainNumber = INVALID_TRAIN_NUMBER;
uint8_t nextStationIndex = AUTO_ROUTE_START_STATION_NUMBER - 1;
bool routeComplete = false;
uint32_t lastStationCheckAt = 0;
uint32_t trackingGeneration = 0;
bool realtimePollStarted = false;
uint32_t lastRealtimePollAt = 0;
bool realtimeCooldownActive = false;
uint32_t realtimeCooldownStartedAt = 0;
bool realtimeFallbackActive = false;
volatile uint32_t latestRealtimeUsageCount = 0;

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

bool parseDepartureTime(const char* value, uint32_t& seconds) {
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

uint32_t departureUpdateTime(uint32_t departureSeconds, uint8_t stationIndex) {
  const int32_t delaySeconds = stationIndex < DEPARTURE_UPDATE_DELAY_COUNT
                                   ? DEPARTURE_UPDATE_DELAY_SEC[stationIndex]
                                   : 0;
  int64_t adjustedSeconds = static_cast<int64_t>(departureSeconds)
                            + delaySeconds;
  adjustedSeconds %= static_cast<int64_t>(SECONDS_PER_DAY);
  if (adjustedSeconds < 0) {
    adjustedSeconds += SECONDS_PER_DAY;
  }
  return static_cast<uint32_t>(adjustedSeconds);
}

bool parseTrainNumber(const char* value, uint16_t& trainNumber) {
  if (value == nullptr || *value == '\0') {
    return false;
  }

  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || *end != '\0'
      || parsed == INVALID_TRAIN_NUMBER || parsed > UINT16_MAX) {
    return false;
  }

  trainNumber = static_cast<uint16_t>(parsed);
  return true;
}

bool parseUint8Value(const char* value, uint8_t& parsedValue) {
  if (value == nullptr || *value == '\0') {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed > UINT8_MAX) {
    return false;
  }
  parsedValue = static_cast<uint8_t>(parsed);
  return true;
}

StationId stationFromRealtimeId(const char* value) {
  if (value == nullptr || *value == '\0') {
    return StationId::INVALID;
  }
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || *end != '\0') {
    return StationId::INVALID;
  }

  const uint16_t stationCode = static_cast<uint16_t>(parsed % 1000UL);
  for (uint8_t number = 1; number <= STATION_COUNT; ++number) {
    const StationInfo* station = getStationInfoByNumber(number);
    if (station != nullptr
        && static_cast<uint16_t>(strtoul(station->subwayCode, nullptr, 10)) == stationCode) {
      return station->id;
    }
  }
  return StationId::INVALID;
}

bool fetchTimetable(uint8_t stationIndex,
                    uint8_t week,
                    TimetableCache& destination) {
  const StationInfo* station = getStationInfoByNumber(stationIndex + 1);
  if (station == nullptr || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  destination.departureCount[stationIndex] = 0;

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
  // ArduinoJson reads the HTTPClient stream directly to avoid buffering the
  // roughly 70 KB response in a String.  The Seoul API uses chunked transfer
  // encoding for HTTP/1.1, but getStream() exposes those chunk frames.  Request
  // HTTP/1.0 so the server returns a connection-delimited JSON body instead.
  http.useHTTP10(true);
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
  filter[SUBWAY_API_SERVICE]["row"][0]["LEFTTIME"] = true;
  filter[SUBWAY_API_SERVICE]["row"][0]["TRAIN_NO"] = true;
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
    if (destination.departureCount[stationIndex] >= MAX_TIMES) {
      break;
    }
    uint32_t seconds = 0;
    uint16_t trainNumber = INVALID_TRAIN_NUMBER;
    if (parseDepartureTime(row["LEFTTIME"] | "", seconds)
        && parseTrainNumber(row["TRAIN_NO"] | "", trainNumber)) {
      const uint16_t departureIndex = destination.departureCount[stationIndex]++;
      destination.departureUpdateSeconds[stationIndex][departureIndex] =
          departureUpdateTime(seconds, stationIndex);
      destination.trainNumbers[stationIndex][departureIndex] = trainNumber;
    }
  }
  http.end();

#if DEBUG_API
  Serial.printf("[API] %s(%s): %u departures\n",
                station->name,
                station->subwayCode,
                destination.departureCount[stationIndex]);
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

RealtimePollResult fetchRealtimePosition(uint16_t requiredTrainNumber,
                                         uint32_t requestGeneration) {
  RealtimePollResult result = {
      RealtimePollOutcome::FAILED,
      {StationId::INVALID, INVALID_TRAIN_NUMBER, 0},
      requestGeneration,
      apiUsageCount()};
  if (WiFi.status() != WL_CONNECTED || !timeIsReady()) {
    return result;
  }

  String url;
  url.reserve(API_URL_RESERVE_SIZE);
  url += REALTIME_API_BASE_URL;
  url += REALTIME_API_KEY;
  url += F("/json/");
  url += REALTIME_API_SERVICE;
  url += '/';
  url += String(REALTIME_API_START_INDEX);
  url += '/';
  url += String(REALTIME_API_END_INDEX);
  url += '/';
  url += REALTIME_SUBWAY_NAME_ENCODED;

  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(API_HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
#if DEBUG_API
    Serial.println(F("[API] Realtime HTTP begin failed"));
#endif
    return result;
  }

  const ApiUsageRecordResult usageResult =
      apiUsageRecordRequest(REALTIME_API_DAILY_LIMIT);
  result.usageCount = apiUsageCount();
  latestRealtimeUsageCount = result.usageCount;
  if (usageResult != ApiUsageRecordResult::RECORDED) {
    result.outcome = usageResult == ApiUsageRecordResult::LIMIT_REACHED
                         ? RealtimePollOutcome::LIMIT_REACHED
                         : RealtimePollOutcome::FAILED;
#if DEBUG_API
    Serial.printf("[API] Realtime request blocked (usage=%u, reason=%u)\n",
                  static_cast<unsigned>(result.usageCount),
                  static_cast<unsigned>(usageResult));
#endif
    http.end();
    return result;
  }

  // Count is persisted immediately before the actual HTTP attempt.
  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
#if DEBUG_API
    Serial.printf("[API] Realtime HTTP %d (usage=%u)\n",
                  statusCode,
                  static_cast<unsigned>(result.usageCount));
#endif
    http.end();
    return result;
  }

  StaticJsonDocument<REALTIME_FILTER_JSON_CAPACITY> filter;
  filter["errorMessage"]["code"] = true;
  filter["RESULT"]["CODE"] = true;
  filter["realtimePositionList"][0]["statnId"] = true;
  filter["realtimePositionList"][0]["trainNo"] = true;
  filter["realtimePositionList"][0]["updnLine"] = true;
  filter["realtimePositionList"][0]["trainSttus"] = true;

  DynamicJsonDocument document(REALTIME_API_JSON_DOCUMENT_CAPACITY);
  const DeserializationError error = deserializeJson(
      document,
      http.getStream(),
      DeserializationOption::Filter(filter));
  http.end();

  if (error) {
#if DEBUG_API
    Serial.printf("[API] Realtime JSON error: %s\n", error.c_str());
#endif
    return result;
  }

  const char* responseCode = document["errorMessage"]["code"] | "";
  if (*responseCode == '\0') {
    responseCode = document["RESULT"]["CODE"] | "";
  }
  if (*responseCode != '\0' && strcmp(responseCode, "INFO-000") != 0) {
#if DEBUG_API
    Serial.printf("[API] Realtime response error: %s\n", responseCode);
#endif
    return result;
  }

  JsonArray rows = document["realtimePositionList"].as<JsonArray>();
  if (rows.isNull()) {
#if DEBUG_API
    Serial.println(F("[API] Realtime position rows missing"));
#endif
    return result;
  }

  result.outcome = RealtimePollOutcome::SUCCESS;
  const uint8_t expectedDirection = static_cast<uint8_t>(TRAIN_DIRECTION - 1);
  uint8_t bestInitialPriority = UINT8_MAX;
  for (JsonObject row : rows) {
    uint8_t direction = 0;
    uint8_t trainStatus = 0;
    uint16_t trainNumber = INVALID_TRAIN_NUMBER;
    if (!parseUint8Value(row["updnLine"] | "", direction)
        || direction != expectedDirection
        || !parseUint8Value(row["trainSttus"] | "", trainStatus)
        || !parseTrainNumber(row["trainNo"] | "", trainNumber)) {
      continue;
    }

    const StationId station = stationFromRealtimeId(row["statnId"] | "");
    if (station == StationId::INVALID) {
      continue;
    }

    if (requiredTrainNumber != INVALID_TRAIN_NUMBER) {
      if (trainNumber == requiredTrainNumber) {
        result.observation = {station, trainNumber, trainStatus};
        break;
      }
      continue;
    }

    if (stationNumber(station) != AUTO_ROUTE_START_STATION_NUMBER) {
      continue;
    }
    const uint8_t priority = trainStatus == REALTIME_STATUS_DEPARTED
                                 ? 0
                                 : trainStatus == 1 ? 1 : trainStatus == 0 ? 2 : 3;
    if (priority < bestInitialPriority) {
      bestInitialPriority = priority;
      result.observation = {station, trainNumber, trainStatus};
    }
  }

#if DEBUG_API
  if (result.observation.trainNumber == INVALID_TRAIN_NUMBER) {
    Serial.printf("[API] Realtime train not found (tracked=%u, usage=%u)\n",
                  requiredTrainNumber,
                  static_cast<unsigned>(result.usageCount));
  } else {
    Serial.printf("[API] Realtime train %u station=%u status=%u (usage=%u)\n",
                  result.observation.trainNumber,
                  stationNumber(result.observation.station),
                  result.observation.trainStatus,
                  static_cast<unsigned>(result.usageCount));
  }
#endif
  return result;
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

bool requestRealtimePoll(uint16_t trainNumber, uint32_t requestGeneration) {
  bool scheduled = false;
  portENTER_CRITICAL(&requestMux);
  if (!realtimePollRequested && !realtimePolling) {
    realtimeRequestedTrainNumber = trainNumber;
    realtimeRequestedGeneration = requestGeneration;
    realtimePollRequested = true;
    scheduled = true;
  }
  portEXIT_CRITICAL(&requestMux);

  if (scheduled && apiTaskHandle != nullptr) {
    xTaskNotifyGive(apiTaskHandle);
  }
  return scheduled;
}

bool consumeRealtimePollRequest(uint16_t& trainNumber,
                                uint32_t& requestGeneration) {
  portENTER_CRITICAL(&requestMux);
  const bool requested = realtimePollRequested;
  if (requested) {
    trainNumber = realtimeRequestedTrainNumber;
    requestGeneration = realtimeRequestedGeneration;
    realtimePollRequested = false;
    realtimePolling = true;
  }
  portEXIT_CRITICAL(&requestMux);
  return requested;
}

void publishRealtimeResult(const RealtimePollResult& result) {
  xSemaphoreTake(cacheMutex, portMAX_DELAY);
  realtimeResult = result;
  realtimeResultAvailable = true;
  xSemaphoreGive(cacheMutex);

  portENTER_CRITICAL(&requestMux);
  realtimePolling = false;
  portEXIT_CRITICAL(&requestMux);
}

bool consumeRealtimeResult(RealtimePollResult& result) {
  xSemaphoreTake(cacheMutex, portMAX_DELAY);
  const bool available = realtimeResultAvailable;
  if (available) {
    result = realtimeResult;
    realtimeResultAvailable = false;
  }
  xSemaphoreGive(cacheMutex);
  return available;
}

void apiWorkerTask(void*) {
  for (;;) {
    uint16_t requestedTrainNumber = INVALID_TRAIN_NUMBER;
    uint32_t requestGeneration = 0;
    const bool pollRealtime = consumeRealtimePollRequest(
        requestedTrainNumber, requestGeneration);
    const bool refreshTimetable = consumeRefreshRequest();
    if (!pollRealtime && !refreshTimetable) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    if (pollRealtime) {
      publishRealtimeResult(fetchRealtimePosition(
          requestedTrainNumber, requestGeneration));
    }

    if (refreshTimetable) {
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
}

uint32_t elapsedSecondsSince(uint32_t earlier, uint32_t later) {
  return later >= earlier ? later - earlier
                          : SECONDS_PER_DAY - earlier + later;
}

TimetableMatch findClosestTimetableMatch(uint32_t nowSeconds,
                                         uint16_t requiredTrainNumber) {
  if (cacheMutex == nullptr) {
    return {StationId::INVALID, INVALID_TRAIN_NUMBER};
  }

  StationId bestStation = StationId::INVALID;
  uint16_t bestTrainNumber = INVALID_TRAIN_NUMBER;
  uint32_t bestDifference = UINT32_MAX;

  xSemaphoreTake(cacheMutex, portMAX_DELAY);
  const TimetableCache& cache = timetableCaches[activeCacheIndex];
  if (cache.ready) {
    const uint8_t firstStationIndex = requiredTrainNumber == INVALID_TRAIN_NUMBER
                                          ? AUTO_ROUTE_START_STATION_NUMBER - 1
                                          : 0;
    const uint8_t stationLimit = requiredTrainNumber == INVALID_TRAIN_NUMBER
                                     ? AUTO_ROUTE_START_STATION_NUMBER
                                     : STATION_COUNT;
    for (uint8_t stationIndex = firstStationIndex;
         stationIndex < stationLimit;
         ++stationIndex) {
      for (uint16_t departureIndex = 0;
           departureIndex < cache.departureCount[stationIndex];
           ++departureIndex) {
        const uint16_t candidateTrainNumber =
            cache.trainNumbers[stationIndex][departureIndex];
        if (requiredTrainNumber != INVALID_TRAIN_NUMBER
            && candidateTrainNumber != requiredTrainNumber) {
          continue;
        }
        const uint32_t difference = elapsedSecondsSince(
            cache.departureUpdateSeconds[stationIndex][departureIndex], nowSeconds);
        if (difference < bestDifference) {
          bestDifference = difference;
          bestStation = stationIdFromNumber(static_cast<uint8_t>(stationIndex + 1));
          bestTrainNumber = candidateTrainNumber;
        }
      }
    }
  }
  xSemaphoreGive(cacheMutex);

  if (bestDifference > static_cast<uint32_t>(STATION_WINDOW)) {
    return {StationId::INVALID, INVALID_TRAIN_NUMBER};
  }
  return {bestStation, bestTrainNumber};
}

void startRealtimeCooldown(uint32_t now) {
  if (AUTO_API_SOURCE == AutoApiSource::REALTIME) {
    realtimeCooldownStartedAt = now;
    realtimeCooldownActive = true;
  }
}

bool realtimeCooldownIsActive(uint32_t now) {
  if (!realtimeCooldownActive) {
    return false;
  }
  if (static_cast<uint32_t>(now - realtimeCooldownStartedAt)
      < REALTIME_API_TRANSITION_COOLDOWN_MS) {
    return true;
  }
  realtimeCooldownActive = false;
  return false;
}

bool advanceAfterDeparture(StationId departureStation, uint32_t now) {
  const StationId expectedDepartureStation = stationIdFromNumber(nextStationIndex + 1);
  if (departureStation != expectedDepartureStation) {
    return false;
  }

  const StationId followingStation = stationIdFromNumber(nextStationIndex + 2);
  if (followingStation == StationId::INVALID) {
    routeComplete = true;
    return false;
  }

#if DEBUG_API
  Serial.printf("[API] Train %u departed station %u; update to station %u\n",
                trackedTrainNumber,
                static_cast<unsigned>(nextStationIndex + 1),
                static_cast<unsigned>(nextStationIndex + 2));
#endif
  // Both API sources converge here; hardware fan-out remains in loop().
  notifyStation(followingStation);

  ++nextStationIndex;
  // Ignore a realtime response that was already in flight for the previous
  // route segment (possible while timetable fallback is active).
  ++trackingGeneration;
  startRealtimeCooldown(now);
  if (nextStationIndex + 1 >= STATION_COUNT) {
    routeComplete = true;
#if DEBUG_API
    Serial.printf("[API] Train %u route complete\n", trackedTrainNumber);
#endif
  }
  return true;
}

void updateFromTimetable(uint32_t secondsOfDay, uint32_t now) {
  if (!apiIsTimetableReady()) {
    detectedStation = StationId::INVALID;
    return;
  }

  const TimetableMatch match = findClosestTimetableMatch(
      secondsOfDay, trackedTrainNumber);
  detectedStation = match.station;
  if (match.station == StationId::INVALID) {
    return;
  }

  if (trackedTrainNumber == INVALID_TRAIN_NUMBER) {
    trackedTrainNumber = match.trainNumber;
#if DEBUG_API
    Serial.printf("[API] Train %u locked at route start\n", trackedTrainNumber);
#endif
  }
  advanceAfterDeparture(match.station, now);
}

void updateFromRealtime(const RealtimePollResult& result, uint32_t now) {
  if (result.outcome != RealtimePollOutcome::SUCCESS) {
    realtimeFallbackActive = REALTIME_API_FALLBACK_TO_TIMETABLE;
#if DEBUG_API
    Serial.printf("[API] Realtime fallback %s (reason=%u)\n",
                  realtimeFallbackActive ? "enabled" : "disabled",
                  static_cast<unsigned>(result.outcome));
#endif
    return;
  }

  realtimeFallbackActive = false;
  const RealtimeObservation& observation = result.observation;
  detectedStation = observation.station;
  if (observation.station == StationId::INVALID
      || observation.trainNumber == INVALID_TRAIN_NUMBER) {
    return;
  }

  if (trackedTrainNumber == INVALID_TRAIN_NUMBER) {
    if (stationNumber(observation.station) != AUTO_ROUTE_START_STATION_NUMBER) {
      return;
    }
    trackedTrainNumber = observation.trainNumber;
#if DEBUG_API
    Serial.printf("[API] Realtime train %u locked at route start\n",
                  trackedTrainNumber);
#endif
  } else if (observation.trainNumber != trackedTrainNumber) {
    return;
  }

  const uint8_t expectedDepartureNumber = nextStationIndex + 1;
  const uint8_t observedStationNumber = stationNumber(observation.station);
  const bool departureConfirmed =
      (observedStationNumber == expectedDepartureNumber
       && observation.trainStatus == REALTIME_STATUS_DEPARTED)
      || (observedStationNumber == expectedDepartureNumber + 1
          && observation.trainStatus
                 == REALTIME_STATUS_PREVIOUS_STATION_DEPARTED);
  if (departureConfirmed) {
    advanceAfterDeparture(
        stationIdFromNumber(expectedDepartureNumber), now);
  }
}

void resetRouteTracking() {
  detectedStation = StationId::INVALID;
  trackedTrainNumber = INVALID_TRAIN_NUMBER;
  nextStationIndex = AUTO_ROUTE_START_STATION_NUMBER - 1;
  routeComplete = false;
  ++trackingGeneration;
  realtimePollStarted = false;
  realtimeCooldownActive = false;
  realtimeFallbackActive = false;

  portENTER_CRITICAL(&requestMux);
  realtimePollRequested = false;
  portEXIT_CRITICAL(&requestMux);

  if (cacheMutex != nullptr) {
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    realtimeResultAvailable = false;
    xSemaphoreGive(cacheMutex);
  }
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

  if (AUTO_API_SOURCE == AutoApiSource::REALTIME && !apiUsageBegin()) {
#if DEBUG_API
    Serial.println(F("[API] Realtime usage NVS initialization failed"));
#endif
  }
  if (AUTO_API_SOURCE == AutoApiSource::REALTIME) {
    latestRealtimeUsageCount = apiUsageCount();
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
  Serial.printf("[API] Background task ready (source=%s)\n",
                AUTO_API_SOURCE == AutoApiSource::REALTIME
                    ? "REALTIME"
                    : "TIMETABLE");
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
  if (!getLocalClock(localTime, secondsOfDay)) {
    detectedStation = StationId::INVALID;
    return;
  }

  if (routeComplete) {
    detectedStation = StationId::INVALID;
    return;
  }

  if (AUTO_API_SOURCE == AutoApiSource::TIMETABLE) {
    updateFromTimetable(static_cast<uint32_t>(secondsOfDay), now);
    return;
  }

  RealtimePollResult result = {
      RealtimePollOutcome::FAILED,
      {StationId::INVALID, INVALID_TRAIN_NUMBER, 0},
      0,
      latestRealtimeUsageCount};
  if (consumeRealtimeResult(result)) {
    latestRealtimeUsageCount = result.usageCount;
    if (result.trackingGeneration == trackingGeneration) {
      updateFromRealtime(result, now);
    }
  }

  if (routeComplete) {
    return;
  }

  const bool cooldown = realtimeCooldownIsActive(now);
  if (!cooldown
      && (!realtimePollStarted
          || static_cast<uint32_t>(now - lastRealtimePollAt)
                 >= REALTIME_API_POLL_INTERVAL_MS)
      && requestRealtimePoll(trackedTrainNumber, trackingGeneration)) {
    lastRealtimePollAt = now;
    realtimePollStarted = true;
  }

  if (realtimeFallbackActive && REALTIME_API_FALLBACK_TO_TIMETABLE) {
    updateFromTimetable(static_cast<uint32_t>(secondsOfDay), now);
  }
}

void apiSetAutoMode(bool enabled) {
  if (autoMode == enabled) {
    return;
  }
  autoMode = enabled;
  resetRouteTracking();
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

uint16_t apiTrackedTrainNumber() {
  return trackedTrainNumber;
}

uint8_t apiNextStationNumber() {
  return !routeComplete && nextStationIndex + 1 < STATION_COUNT
             ? static_cast<uint8_t>(nextStationIndex + 2)
             : 0;
}

bool apiIsRouteComplete() {
  return routeComplete;
}

bool apiUsesRealtime() {
  return AUTO_API_SOURCE == AutoApiSource::REALTIME;
}

bool apiIsRealtimePolling() {
  if (!apiUsesRealtime()) {
    return false;
  }
  portENTER_CRITICAL(&requestMux);
  const bool active = realtimePollRequested || realtimePolling;
  portEXIT_CRITICAL(&requestMux);
  return active;
}

int32_t apiRealtimeSecondsUntilNextPoll() {
  if (!apiUsesRealtime() || !autoMode || routeComplete || !timeIsReady()) {
    return -1;
  }

  if (apiIsRealtimePolling()) {
    return 0;
  }

  const uint32_t now = millis();
  uint32_t waitMs = 0;
  if (realtimePollStarted) {
    const uint32_t elapsed = static_cast<uint32_t>(now - lastRealtimePollAt);
    if (elapsed < REALTIME_API_POLL_INTERVAL_MS) {
      waitMs = REALTIME_API_POLL_INTERVAL_MS - elapsed;
    }
  }
  if (realtimeCooldownActive) {
    const uint32_t elapsed = static_cast<uint32_t>(now - realtimeCooldownStartedAt);
    if (elapsed < REALTIME_API_TRANSITION_COOLDOWN_MS) {
      const uint32_t cooldownRemaining =
          REALTIME_API_TRANSITION_COOLDOWN_MS - elapsed;
      if (cooldownRemaining > waitMs) {
        waitMs = cooldownRemaining;
      }
    }
  }
  return static_cast<int32_t>((waitMs + 999UL) / 1000UL);
}

uint32_t apiRealtimeRemainingRequests() {
  if (!apiUsesRealtime()) {
    return 0;
  }
  const uint32_t used = latestRealtimeUsageCount;
  return used >= REALTIME_API_DAILY_LIMIT
             ? 0
             : REALTIME_API_DAILY_LIMIT - used;
}
