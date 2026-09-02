#include "api_usage.h"

#include <Preferences.h>
#include <time.h>

namespace {

constexpr char NVS_NAMESPACE[] = "api-usage";
constexpr char NVS_DATE_KEY[] = "date";
constexpr char NVS_COUNT_KEY[] = "count";
constexpr time_t MIN_VALID_EPOCH = 1609459200;  // 2021-01-01 UTC

Preferences preferences;
bool storageReady = false;
uint32_t storedDate = 0;
uint32_t requestCount = 0;

bool currentLocalDate(uint32_t& date) {
  const time_t now = time(nullptr);
  tm localTime = {};
  if (now < MIN_VALID_EPOCH || localtime_r(&now, &localTime) == nullptr) {
    return false;
  }

  date = static_cast<uint32_t>(localTime.tm_year + 1900) * 10000UL
         + static_cast<uint32_t>(localTime.tm_mon + 1) * 100UL
         + static_cast<uint32_t>(localTime.tm_mday);
  return true;
}

bool resetForDate(uint32_t date) {
  // Store zero first. If power is lost between writes, the next boot safely
  // repeats the reset because the previous date is still present.
  if (preferences.putUInt(NVS_COUNT_KEY, 0) != sizeof(uint32_t)
      || preferences.putUInt(NVS_DATE_KEY, date) != sizeof(uint32_t)) {
    return false;
  }
  requestCount = 0;
  storedDate = date;
  return true;
}

ApiUsageRecordResult synchronizeDate() {
  if (!storageReady) {
    return ApiUsageRecordResult::STORAGE_ERROR;
  }

  uint32_t today = 0;
  if (!currentLocalDate(today)) {
    return ApiUsageRecordResult::TIME_NOT_READY;
  }
  if (today != storedDate && !resetForDate(today)) {
    return ApiUsageRecordResult::STORAGE_ERROR;
  }
  return ApiUsageRecordResult::RECORDED;
}

}  // namespace

bool apiUsageBegin() {
  if (storageReady) {
    return true;
  }
  storageReady = preferences.begin(NVS_NAMESPACE, false);
  if (!storageReady) {
    return false;
  }
  storedDate = preferences.getUInt(NVS_DATE_KEY, 0);
  requestCount = preferences.getUInt(NVS_COUNT_KEY, 0);
  return true;
}

ApiUsageRecordResult apiUsageRecordRequest(uint32_t dailyLimit) {
  const ApiUsageRecordResult synchronized = synchronizeDate();
  if (synchronized != ApiUsageRecordResult::RECORDED) {
    return synchronized;
  }
  if (requestCount >= dailyLimit) {
    return ApiUsageRecordResult::LIMIT_REACHED;
  }

  const uint32_t nextCount = requestCount + 1;
  if (preferences.putUInt(NVS_COUNT_KEY, nextCount) != sizeof(uint32_t)) {
    return ApiUsageRecordResult::STORAGE_ERROR;
  }
  requestCount = nextCount;
  return ApiUsageRecordResult::RECORDED;
}

uint32_t apiUsageCount() {
  synchronizeDate();
  return requestCount;
}

