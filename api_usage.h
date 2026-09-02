#pragma once

#include <Arduino.h>

enum class ApiUsageRecordResult : uint8_t {
  RECORDED,
  LIMIT_REACHED,
  TIME_NOT_READY,
  STORAGE_ERROR
};

bool apiUsageBegin();
ApiUsageRecordResult apiUsageRecordRequest(uint32_t dailyLimit);
uint32_t apiUsageCount();

