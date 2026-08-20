#include "matrix_display.h"

#include <MD_MAX72xx.h>

#include "config.h"

namespace {

// Each glyph is eight rows, MSB at the left. These 15 precomposed Hangul
// bitmaps are adapted from Dalmoori, an 8x8 pixel Hangul font designed for
// legibility at this exact resolution (Apache-2.0; see THIRD_PARTY_NOTICES.md).
// Keeping only the required glyphs avoids UTF-8 parsing and a full font table.
const uint8_t GLYPH_ROWS[static_cast<uint8_t>(GlyphId::COUNT)][8] PROGMEM = {
    // 동
    {0b01111100, 0b01000000, 0b01111100, 0b00010000,
     0b11111110, 0b00111000, 0b01000100, 0b00111000},
    // 대
    {0b11110101, 0b10000101, 0b10000101, 0b10000111,
     0b10000101, 0b11110101, 0b00000101, 0b00000101},
    // 문
    {0b01111100, 0b01000100, 0b01111100, 0b11111110,
     0b00010000, 0b01000000, 0b01000000, 0b01111100},
    // 역
    {0b01110110, 0b10001010, 0b10001110, 0b01110010,
     0b00000000, 0b01111110, 0b00000010, 0b00000010},
    // 사
    {0b00100010, 0b00100010, 0b00100010, 0b01010011,
     0b01010010, 0b10001010, 0b00000010, 0b00000010},
    // 공
    {0b01111100, 0b00000100, 0b00010100, 0b11111110,
     0b00000000, 0b00111000, 0b01000100, 0b00111000},
    // 원
    {0b01110010, 0b10001010, 0b01110010, 0b11111010,
     0b00100110, 0b01000000, 0b01000000, 0b01111110},
    // 신
    {0b00100010, 0b00100010, 0b01010010, 0b10001010,
     0b00000000, 0b01000000, 0b01000000, 0b01111110},
    // 당
    {0b11111010, 0b10000011, 0b10000010, 0b11111010,
     0b00000000, 0b00111100, 0b01000010, 0b00111100},
    // 상
    {0b00100010, 0b00100011, 0b01010010, 0b10001010,
     0b00000000, 0b00111100, 0b01000010, 0b00111100},
    // 왕
    {0b01110010, 0b10001010, 0b01110011, 0b00100010,
     0b11111010, 0b00111100, 0b01000010, 0b00111100},
    // 십
    {0b00100010, 0b00100010, 0b01010010, 0b10001010,
     0b01000010, 0b01111110, 0b01000010, 0b01111110},
    // 리
    {0b11111010, 0b00001010, 0b11111010, 0b10000010,
     0b10000010, 0b11111010, 0b00000010, 0b00000010},
    // 한
    {0b01110010, 0b11111011, 0b01010010, 0b00100010,
     0b00000000, 0b01000000, 0b01000000, 0b01111110},
    // 양
    {0b01110011, 0b10001010, 0b10001011, 0b01110010,
     0b00000000, 0b00111100, 0b01000010, 0b00111100},
};

constexpr uint8_t GLYPH_WIDTH = 8;
constexpr uint8_t MAX_CONTENT_COLUMNS = MAX_STATION_GLYPHS * GLYPH_WIDTH;

MD_MAX72XX matrix(
    MATRIX_HARDWARE_TYPE,
    PIN_MATRIX_DIN,
    PIN_MATRIX_CLK,
    PIN_MATRIX_CS,
    MATRIX_DEVICE_COUNT);

enum class DisplayMode : uint8_t { IDLE, STATIC, SCROLLING };

DisplayMode mode = DisplayMode::IDLE;
uint8_t contentColumns[MAX_CONTENT_COLUMNS] = {};
uint8_t contentWidth = 0;
int16_t scrollOffset = 0;
uint8_t scrollCompleted = 0;
uint32_t lastFrameAt = 0;
uint32_t staticStartedAt = 0;

uint8_t glyphColumn(GlyphId glyph, uint8_t x) {
  const uint8_t glyphIndex = static_cast<uint8_t>(glyph);
  if (glyphIndex >= static_cast<uint8_t>(GlyphId::COUNT) || x >= GLYPH_WIDTH) {
    return 0;
  }

  uint8_t column = 0;
  const uint8_t rowMask = static_cast<uint8_t>(0x80U >> x);
  for (uint8_t row = 0; row < MATRIX_HEIGHT; ++row) {
    const uint8_t rowBits = pgm_read_byte(&GLYPH_ROWS[glyphIndex][row]);
    if ((rowBits & rowMask) != 0) {
      const uint8_t targetRow = MATRIX_FLIP_VERTICAL ? (MATRIX_HEIGHT - 1 - row) : row;
      column |= static_cast<uint8_t>(1U << targetRow);
    }
  }
  return column;
}

void writePhysicalColumn(uint8_t logicalColumn, uint8_t value) {
  if (logicalColumn >= MATRIX_WIDTH) {
    return;
  }
  const uint8_t physicalColumn = MATRIX_REVERSE_COLUMNS
                                     ? (MATRIX_WIDTH - 1 - logicalColumn)
                                     : logicalColumn;
  matrix.setColumn(physicalColumn, value);
}

void beginFrame() {
  matrix.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
}

void endFrame() {
  matrix.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void renderStatic(const StationInfo& info) {
  const uint8_t textWidth = info.glyphCount * GLYPH_WIDTH;
  const uint8_t leftPadding = textWidth < MATRIX_WIDTH ? (MATRIX_WIDTH - textWidth) / 2 : 0;

  beginFrame();
  for (uint8_t screenX = 0; screenX < MATRIX_WIDTH; ++screenX) {
    uint8_t value = 0;
    if (screenX >= leftPadding && screenX < leftPadding + textWidth) {
      const uint8_t textX = screenX - leftPadding;
      value = glyphColumn(info.glyphs[textX / GLYPH_WIDTH], textX % GLYPH_WIDTH);
    }
    writePhysicalColumn(screenX, value);
  }
  endFrame();
}

void buildScrollContent(const StationInfo& info) {
  contentWidth = 0;
  for (uint8_t glyphIndex = 0;
       glyphIndex < info.glyphCount && contentWidth <= MAX_CONTENT_COLUMNS - GLYPH_WIDTH;
       ++glyphIndex) {
    for (uint8_t x = 0; x < GLYPH_WIDTH; ++x) {
      contentColumns[contentWidth++] = glyphColumn(info.glyphs[glyphIndex], x);
    }
  }
}

void renderScrollFrame() {
  beginFrame();
  for (uint8_t screenX = 0; screenX < MATRIX_WIDTH; ++screenX) {
    const int16_t sourceX = scrollOffset + screenX;
    const uint8_t value = sourceX >= 0 && sourceX < contentWidth
                              ? contentColumns[sourceX]
                              : 0;
    writePhysicalColumn(screenX, value);
  }
  endFrame();
}

}  // namespace

void matrixBegin() {
  matrix.begin();
  matrix.control(MD_MAX72XX::INTENSITY, MATRIX_INTENSITY);
  matrix.clear();

#if DEBUG_MATRIX
  Serial.printf("[MATRIX] Ready: %ux%u, devices: %u\n",
                MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_DEVICE_COUNT);
#endif
}

void matrixShowStation(StationId station) {
  const StationInfo* info = getStationInfo(station);
  if (info == nullptr || info->glyphCount == 0 || info->glyphCount > MAX_STATION_GLYPHS) {
#if DEBUG_MATRIX
    Serial.println(F("[MATRIX] Invalid station glyph data"));
#endif
    return;
  }

#if DEBUG_MATRIX
  Serial.printf("[MATRIX] Display: %s (%s)\n", info->name, info->debugName);
#endif

  const uint32_t now = millis();
  if (info->glyphCount <= MATRIX_WIDTH / GLYPH_WIDTH) {
    renderStatic(*info);
    staticStartedAt = now;
    mode = DisplayMode::STATIC;
    return;
  }

  // Long names are flattened into one column buffer. The 32-column display
  // window then advances over it by exactly one pixel on each timed frame.
  buildScrollContent(*info);
  scrollOffset = -static_cast<int16_t>(MATRIX_WIDTH);
  scrollCompleted = 0;
  lastFrameAt = now;
  mode = DisplayMode::SCROLLING;
  renderScrollFrame();
}

void matrixUpdate() {
  const uint32_t now = millis();

  if (mode == DisplayMode::STATIC) {
    if (static_cast<uint32_t>(now - staticStartedAt) >= MATRIX_STATIC_DISPLAY_MS) {
      matrixClear();
    }
    return;
  }

  if (mode != DisplayMode::SCROLLING
      || static_cast<uint32_t>(now - lastFrameAt) < MATRIX_SCROLL_INTERVAL_MS) {
    return;
  }

  lastFrameAt = now;
  ++scrollOffset;
  renderScrollFrame();

  // source offset == contentWidth is the first completely blank exit frame.
  if (scrollOffset >= contentWidth) {
    ++scrollCompleted;
    const uint8_t repeatTarget = MATRIX_SCROLL_REPEAT == 0 ? 1 : MATRIX_SCROLL_REPEAT;
    if (scrollCompleted >= repeatTarget) {
      matrixClear();
#if DEBUG_MATRIX
      Serial.println(F("[MATRIX] Scroll complete"));
#endif
    } else {
      scrollOffset = -static_cast<int16_t>(MATRIX_WIDTH);
    }
  }
}

void matrixClear() {
  matrix.clear();
  mode = DisplayMode::IDLE;
}

bool matrixIsActive() {
  return mode != DisplayMode::IDLE;
}
