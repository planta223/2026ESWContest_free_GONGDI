#include "station_data.h"

#include "config.h"

namespace {

const GlyphId GLYPHS_DONGDAEMUN[] = {
    GlyphId::DONG, GlyphId::DAE, GlyphId::MUN, GlyphId::YEOK,
    GlyphId::SA, GlyphId::GONG, GlyphId::WON};
const GlyphId GLYPHS_SINDANG[] = {GlyphId::SIN, GlyphId::DANG};
const GlyphId GLYPHS_SANGWANGSIMNI[] = {
    GlyphId::SANG, GlyphId::WANG, GlyphId::SIP, GlyphId::RI};
const GlyphId GLYPHS_WANGSIMNI[] = {GlyphId::WANG, GlyphId::SIP, GlyphId::RI};
const GlyphId GLYPHS_HANYANG[] = {GlyphId::HAN, GlyphId::YANG, GlyphId::DAE};

// Station targets are cumulative because config.h stores the four physical
// gaps. Dongdaemun History & Culture Park remains the boot-time zero point.
constexpr int32_t MOTOR_POSITION_DONGDAEMUN_STEPS = 0;
constexpr int32_t MOTOR_POSITION_SINDANG_STEPS =
    MOTOR_POSITION_DONGDAEMUN_STEPS + MOTOR_INTERVAL_TO_SINDANG_STEPS;
constexpr int32_t MOTOR_POSITION_SANGWANGSIMNI_STEPS =
    MOTOR_POSITION_SINDANG_STEPS + MOTOR_INTERVAL_TO_SANGWANGSIMNI_STEPS;
constexpr int32_t MOTOR_POSITION_WANGSIMNI_STEPS =
    MOTOR_POSITION_SANGWANGSIMNI_STEPS + MOTOR_INTERVAL_TO_WANGSIMNI_STEPS;
constexpr int32_t MOTOR_POSITION_HANYANG_UNIV_STEPS =
    MOTOR_POSITION_WANGSIMNI_STEPS + MOTOR_INTERVAL_TO_HANYANG_UNIV_STEPS;

const StationInfo STATIONS[STATION_COUNT] = {
    {StationId::DONGDAEMUN_HISTORY_CULTURE_PARK, "동대문역사공원",
     "DONGDAEMUN_HISTORY_CULTURE_PARK", "205", 1, true,
     MOTOR_POSITION_DONGDAEMUN_STEPS,
     GLYPHS_DONGDAEMUN, static_cast<uint8_t>(sizeof(GLYPHS_DONGDAEMUN) / sizeof(GLYPHS_DONGDAEMUN[0]))},
    {StationId::SINDANG, "신당", "SINDANG", "206", 2, true,
     MOTOR_POSITION_SINDANG_STEPS,
     GLYPHS_SINDANG, static_cast<uint8_t>(sizeof(GLYPHS_SINDANG) / sizeof(GLYPHS_SINDANG[0]))},
    {StationId::SANGWANGSIMNI, "상왕십리", "SANGWANGSIMNI", "207", 3, true,
     MOTOR_POSITION_SANGWANGSIMNI_STEPS, GLYPHS_SANGWANGSIMNI,
     static_cast<uint8_t>(sizeof(GLYPHS_SANGWANGSIMNI) / sizeof(GLYPHS_SANGWANGSIMNI[0]))},
    {StationId::WANGSIMNI, "왕십리", "WANGSIMNI", "208", 4, false,
     MOTOR_POSITION_WANGSIMNI_STEPS,
     GLYPHS_WANGSIMNI, static_cast<uint8_t>(sizeof(GLYPHS_WANGSIMNI) / sizeof(GLYPHS_WANGSIMNI[0]))},
    {StationId::HANYANG_UNIV, "한양대", "HANYANG_UNIV", "209", 5, true,
     MOTOR_POSITION_HANYANG_UNIV_STEPS, GLYPHS_HANYANG,
     static_cast<uint8_t>(sizeof(GLYPHS_HANYANG) / sizeof(GLYPHS_HANYANG[0]))},
};

}  // namespace

const StationInfo* getStationInfo(StationId station) {
  const uint8_t index = static_cast<uint8_t>(station);
  return index < STATION_COUNT ? &STATIONS[index] : nullptr;
}

const StationInfo* getStationInfoByNumber(uint8_t number) {
  return number >= 1 && number <= STATION_COUNT ? &STATIONS[number - 1] : nullptr;
}

StationId stationIdFromNumber(uint8_t number) {
  const StationInfo* info = getStationInfoByNumber(number);
  return info == nullptr ? StationId::INVALID : info->id;
}

uint8_t stationNumber(StationId station) {
  const uint8_t index = static_cast<uint8_t>(station);
  return index < STATION_COUNT ? index + 1 : 0;
}
