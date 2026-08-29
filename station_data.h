#pragma once

#include <Arduino.h>

enum class StationId : uint8_t {
  DONGDAEMUN_HISTORY_CULTURE_PARK = 0,
  SINDANG,
  SANGWANGSIMNI,
  WANGSIMNI,
  HANYANG_UNIV,
  INVALID
};

// Only the 15 Hangul syllables used by this prototype are represented.
// Station names refer directly to glyph IDs, so no UTF-8 parsing is needed.
enum class GlyphId : uint8_t {
  DONG = 0,
  DAE,
  MUN,
  YEOK,
  SA,
  GONG,
  WON,
  SIN,
  DANG,
  SANG,
  WANG,
  SIP,
  RI,
  HAN,
  YANG,
  COUNT
};

struct StationInfo {
  StationId id;
  const char* name;
  const char* debugName;
  const char* subwayCode;
  uint8_t audioTrack;
  bool vibrationEnabled;
  int32_t motorTargetStep;
  const GlyphId* glyphs;
  uint8_t glyphCount;
};

constexpr uint8_t STATION_COUNT = 5;
constexpr uint8_t MAX_STATION_GLYPHS = 7;

const StationInfo* getStationInfo(StationId station);
const StationInfo* getStationInfoByNumber(uint8_t number);  // Serial numbers 1..5
StationId stationIdFromNumber(uint8_t number);
uint8_t stationNumber(StationId station);
