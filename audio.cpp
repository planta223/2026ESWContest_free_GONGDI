#include "audio.h"

#include <DFRobotDFPlayerMini.h>

#include "config.h"

namespace {

// ESP32 UART2 is used; SoftwareSerial is intentionally not used.
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;
bool ready = false;

}  // namespace

void audioBegin() {
  dfSerial.begin(DFPLAYER_BAUD_RATE, SERIAL_8N1, PIN_DFPLAYER_RX, PIN_DFPLAYER_TX);

  // Failure is non-fatal: all other output modules continue to operate.
  ready = dfPlayer.begin(dfSerial, true, true);
  if (!ready) {
#if DEBUG_AUDIO
    Serial.println(F("[AUDIO] DFPlayer initialization failed"));
    Serial.println(F("[AUDIO] Matrix, motor, vibration and button remain available"));
#endif
    return;
  }

  dfPlayer.volume(DFPLAYER_VOLUME);
#if DEBUG_AUDIO
  Serial.printf("[AUDIO] Ready, volume: %u\n", DFPLAYER_VOLUME);
#endif
}

void audioPlayStation(StationId station) {
  const StationInfo* info = getStationInfo(station);
  if (info != nullptr) {
    audioPlayTrack(info->audioTrack);
  }
}

void audioPlayTrack(uint8_t track) {
  if (track < 1 || track > STATION_COUNT) {
#if DEBUG_AUDIO
    Serial.printf("[AUDIO] Invalid track: %u\n", track);
#endif
    return;
  }

  if (!ready) {
#if DEBUG_AUDIO
    Serial.printf("[AUDIO] Not ready; cannot play track: %u\n", track);
#endif
    return;
  }

  // playMp3Folder(3) selects /mp3/0003.mp3 on the microSD card.
  dfPlayer.playMp3Folder(track);
#if DEBUG_AUDIO
  Serial.printf("[AUDIO] Play track: %u (/mp3/%04u.mp3)\n", track, track);
#endif
}

void audioUpdate() {
  // DFPlayer playback is asynchronous after a serial command. No polling is
  // required for this prototype, but this hook is reserved for future events.
}

bool audioIsReady() {
  return ready;
}

