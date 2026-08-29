/* =====================================================================
 *  점잡이 — 시각장애인을 위한 지하철 안내  (ESP32 통합 펌웨어)
 *  ---------------------------------------------------------------------
 *  서울교통공사 "역코드로 지하철 열차 시간표" API
 *      - 부팅 시 역코드 0205~0209 시간표를 1회 받아 메모리에 캐싱
 *      - NTP 현재 시각과 비교해 "이번 역"을 매초 자동 판정
 *      - 같은 시간대 여러 역이 겹치면 역코드 높은 역(한양대→역순) 우선
 *      - 역별 원격 제어(모터/스피커/진동/모니터) → 보드 ACK 회신
 *      - 이번 역 / 각 출력 상태 실시간 모니터링
 *      - 원격으로 역을 고르면 자동판정보다 "원격 우선"
 *
 *  [필요 라이브러리]
 *    - ESP Async WebServer (ESP32Async/me-no-dev)
 *    - AsyncTCP
 *    - ArduinoJson (v6)
 * ===================================================================== */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <time.h>
#include "../index_html.h"

/* ---------------------------------------------------------------------
 *  0. 사용자 설정
 * ------------------------------------------------------------------- */
// 시간표 API + NTP 는 인터넷이 필요 → 반드시 2.4GHz 공유기/폰 핫스팟
const char* WIFI_SSID = "braille_handle_wifi";
const char* WIFI_PASS = "12345678";

// API 인증키
const char* SUBWAY_API_KEY = "67726f57766461683530747742654f";

// 추적 방향: 1 = 상행/내선, 2 = 하행/외선
// (동대문→한양대 방향 도착시각이 이상하면 반대 값으로 바꿔서 테스트)
const int TRAIN_DIRECTION = 1;

// "이번 역"으로 인정할 시간 창(초). 현재 시각과 도착시각 차이가 이 안이면 그 역에 정차/진입 중으로 간주
const long STATION_WINDOW = 90;      // ±90초

// 상태 브로드캐스트 주기 (ms)
const unsigned long STATUS_PUSH_INTERVAL = 1000;

/* ---------------------------------------------------------------------
 *  1. 하드웨어 핀 매핑
 * ------------------------------------------------------------------- */
const int PIN_MOTOR     = ;
const int PIN_SPEAKER   = ;
const int PIN_VIBRATION = ;
const int PIN_BUTTON    = ;

/* ---------------------------------------------------------------------
 *  2. 역 프로필
 *     - 인덱스 0~4 = 역코드 0205~0209
 * ------------------------------------------------------------------- */
struct StationProfile {
  const char* code;       // 역코드
  const char* name;       // 역명
  /*
  int   toneFreq;         // 스피커 주파수(Hz)
  int   vibrationPulses;  // 진동 횟수
  int   motorMs;          // 모터 동작 시간(ms)
  const char* monitor;    // 모니터(디스플레이) 표시 문구
  */
};

StationProfile STATIONS[] = {  // 하드웨어 부분 수정
  //  코드     역명                    스피커  진동  모터    모니터 문구
  { "0205", "동대문역사문화공원",    1000,   1,   300,  "동대문역사문화공원" },
  { "0206", "신당",                 1200,   2,   400,  "신당" },
  { "0207", "상왕십리",             1400,   3,   500,  "상왕십리" },
  { "0208", "왕십리",               1600,   4,   600,  "왕십리" },
  { "0209", "한양대",               1800,   5,   700,  "한양대" },
};
const int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);

/* ---------------------------------------------------------------------
 *  3. 시간표 캐시 (부팅 시 1회 채움)
 *     arrTimes[역][i] = 그날의 도착시각(자정 기준 '초')
 * ------------------------------------------------------------------- */
const int MAX_TIMES = 320;
uint32_t arrTimes[STATION_COUNT][MAX_TIMES];
int      arrCount[STATION_COUNT] = {0};
bool     timetableReady = false;

/* ---------------------------------------------------------------------
 *  4. 전역 상태
 * ------------------------------------------------------------------- */
struct DeviceState {
  int    motor     = 0;
  int    speaker   = 0;
  int    vibration = 0;
  int    button    = 0;
  int    activeIdx = -1;          // 현재 출력 중인 역 인덱스(-1=없음)
  bool   remoteActive = false;    // 원격 수동 선택 중?
  String mode    = "AUTO";        // AUTO / REMOTE
  String monitor = "대기 중";
  String station = "-";           // 이번 역 역명
} state;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long lastStatusPush = 0;
int lastActiveIdx = -1;

/* =====================================================================
 *  WebSocket: 상태 브로드캐스트
 * ===================================================================== */
void broadcastStatus() {
  StaticJsonDocument<384> doc;
  doc["type"]      = "status";
  doc["motor"]     = state.motor;
  doc["speaker"]   = state.speaker;
  doc["vibration"] = state.vibration;
  doc["button"]    = state.button;
  doc["activeIdx"] = state.activeIdx;
  doc["mode"]      = state.mode;
  doc["monitor"]   = state.monitor;
  doc["station"]   = state.station;

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

/* WebSocket: ACK 회신 */
void sendAck(AsyncWebSocketClient* client, const char* cmd, int value, bool ok) {
  StaticJsonDocument<128> doc;
  doc["type"]  = "ack";
  doc["cmd"]   = cmd;
  doc["value"] = value;
  doc["ok"]    = ok;
  String out;
  serializeJson(doc, out);
  client->text(out);
}

/* =====================================================================
 *  해당 역 프로필대로 출력 (모터/스피커/진동/모니터)
 *  - 짧은 블로킹 패턴 (시연용). activeIdx 가 바뀔 때 1회 실행.
 * ===================================================================== */
void applyStation(int idx) {
  if (idx < 0 || idx >= STATION_COUNT) return;
  StationProfile p = STATIONS[idx];

  state.activeIdx = idx;
  state.station   = p.name;
  state.monitor   = p.monitor;
  broadcastStatus();

  // ① 스피커: 역별 고유 주파수로 짧게
  state.speaker = 1;
  tone(PIN_SPEAKER, p.toneFreq, 300);

  // ② 진동: 역별 횟수만큼 펄스
  for (int i = 0; i < p.vibrationPulses; i++) {
    state.vibration = 1;
    digitalWrite(PIN_VIBRATION, HIGH);
    delay(150);
    digitalWrite(PIN_VIBRATION, LOW);
    delay(120);
  }
  state.vibration = 0;

  // ③ 모터: 역별 시간만큼 회전
  state.motor = 1;
  digitalWrite(PIN_MOTOR, HIGH);
  delay(p.motorMs);
  digitalWrite(PIN_MOTOR, LOW);
  state.motor = 0;

  state.speaker = 0;
  broadcastStatus();
}

/* =====================================================================
 *  WebSocket: 명령 처리
 *   { "cmd":"station", "value":<0~4> }  역 수동 선택(원격 우선)
 *   { "cmd":"auto" }                    자동 시간표 모드로 복귀
 *   { "cmd":"refresh" }                 시간표 다시 받기
 * ===================================================================== */
void fetchAllTimetables();   // 전방 선언

void handleCommand(AsyncWebSocketClient* client, const String& msg) {
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, msg)) { sendAck(client, "unknown", 0, false); return; }

  const char* cmd = doc["cmd"] | "";
  int value       = doc["value"] | 0;
  bool ok         = true;

  if (strcmp(cmd, "station") == 0) {
    if (value >= 0 && value < STATION_COUNT) {
      state.remoteActive = true;      // 원격 우선 ON
      state.mode = "REMOTE";
      lastActiveIdx = value;
      applyStation(value);            // 즉시 그 역 출력
    } else ok = false;
  }
  else if (strcmp(cmd, "auto") == 0) {
    state.remoteActive = false;       // 자동 복귀
    state.mode = "AUTO";
    lastActiveIdx = -1;               // 다음 판정에서 다시 트리거되도록
  }
  else if (strcmp(cmd, "refresh") == 0) {
    fetchAllTimetables();
  }
  else ok = false;

  sendAck(client, cmd, value, ok);
  broadcastStatus();
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] 접속 #%u\n", client->id());
      broadcastStatus();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] 해제 #%u\n", client->id());
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len &&
          info->opcode == WS_TEXT) {
        String m = String((char*)data).substring(0, len);
        handleCommand(client, m);
      }
      break;
    }
    default: break;
  }
}

/* =====================================================================
 *  현재 시각 유틸
 * ===================================================================== */
// 자정 기준 '초'로 현재 시각 반환 (NTP 동기화 필요)
long nowSecOfDay(struct tm* out = nullptr) {
  struct tm t;
  if (!getLocalTime(&t)) return -1;
  if (out) *out = t;
  return t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec;
}

// 요일 태그: 평일=1, 토=2, 휴일(일)=3
int weekTagNow() {
  struct tm t;
  if (!getLocalTime(&t)) return 1;
  if (t.tm_wday == 0) return 3;   // 일요일
  if (t.tm_wday == 6) return 2;   // 토요일
  return 1;                       // 평일
}

/* =====================================================================
 *  시간표 API 호출 (역 1개) → arrTimes 채우기
 * ===================================================================== */
void fetchTimetable(int idx) {
  if (WiFi.status() != WL_CONNECTED) return;
  arrCount[idx] = 0;

  int wk = weekTagNow();
  HTTPClient http;
  String url = "http://openapi.seoul.go.kr:8088/";
  url += SUBWAY_API_KEY;
  url += "/json/SearchSTNTimeTableByFRCodeService/1/";
  url += String(MAX_TIMES);
  url += "/";
  url += STATIONS[idx].code;                 // 역코드 0205~0209
  url += "/";
  url += String(wk);                         // 요일
  url += "/";
  url += String(TRAIN_DIRECTION);            // 상/하행
  url += "/";

  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();

    // ARRIVETIME 만 파싱 (메모리 절약)
    StaticJsonDocument<256> filter;
    filter["SearchSTNTimeTableByFRCodeService"]["row"][0]["ARRIVETIME"] = true;

    DynamicJsonDocument doc(20480);
    if (!deserializeJson(doc, payload, DeserializationOption::Filter(filter))) {
      JsonArray rows = doc["SearchSTNTimeTableByFRCodeService"]["row"];
      for (JsonObject r : rows) {
        const char* at = r["ARRIVETIME"] | "";
        int hh, mm, ss;
        if (sscanf(at, "%d:%d:%d", &hh, &mm, &ss) == 3) {
          if (arrCount[idx] < MAX_TIMES)
            arrTimes[idx][arrCount[idx]++] = hh * 3600L + mm * 60L + ss;
        }
      }
      Serial.printf("[TT] %s(%s) %d개 도착시각 로드\n",
                    STATIONS[idx].name, STATIONS[idx].code, arrCount[idx]);
    } else {
      Serial.printf("[TT] %s JSON 파싱 오류\n", STATIONS[idx].name);
    }
  } else {
    Serial.printf("[TT] %s HTTP 오류 %d\n", STATIONS[idx].name, code);
  }
  http.end();
}

// 5개 역 전부 (부팅/새로고침 시 1회만 호출)
void fetchAllTimetables() {
  Serial.println("[TT] 시간표 로딩 시작...");
  for (int i = 0; i < STATION_COUNT; i++) {
    fetchTimetable(i);
    delay(300);   // 초당 호출 제한 회피
  }
  timetableReady = true;
  Serial.println("[TT] 시간표 로딩 완료");
}

/* =====================================================================
 *  이번 역 판정: 현재 시각에 가장 가까운 도착시각을 가진 역
 *   - 0209(한양대)→0205 역순으로 검사, 동점이면 높은 코드 우선
 *   - 최소 차이가 STATION_WINDOW 이내여야 "이번 역"으로 인정
 *  반환: 역 인덱스(0~4) 또는 -1
 * ===================================================================== */
int findCurrentStation(long nowSec) {
  int  best = -1;
  long bestDiff = 0x7fffffff;

  for (int s = STATION_COUNT - 1; s >= 0; s--) {      // 높은 코드부터
    for (int i = 0; i < arrCount[s]; i++) {
      long d = labs((long)arrTimes[s][i] - nowSec);
      if (d < bestDiff) { bestDiff = d; best = s; }   // 동점은 먼저 잡힌(높은코드) 유지
    }
  }
  return (bestDiff <= STATION_WINDOW) ? best : -1;
}

/* =====================================================================
 *  물리 버튼 감시
 * ===================================================================== */
void checkButton() {
  int pressed = (digitalRead(PIN_BUTTON) == LOW) ? 1 : 0;
  if (pressed != state.button) {
    state.button = pressed;
    Serial.printf("[BTN] %s\n", pressed ? "눌림" : "뗌");
    broadcastStatus();
  }
}

/* =====================================================================
 *  setup / loop
 * ===================================================================== */
void setup() {
  Serial.begin(115200);

  pinMode(PIN_MOTOR,     OUTPUT);
  pinMode(PIN_SPEAKER,   OUTPUT);
  pinMode(PIN_VIBRATION, OUTPUT);
  pinMode(PIN_BUTTON,    INPUT_PULLUP);

  // ── STA 모드: 공유기/핫스팟에 접속 (인터넷 필요) ──
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] 연결 중");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  Serial.print("[WiFi] 연결됨! 접속 주소 → http://");
  Serial.println(WiFi.localIP());   // ← 이 IP를 브라우저에 입력

  // ── NTP 시각 동기화 (KST, UTC+9) ──
  configTime(9 * 3600, 0, "pool.ntp.org", "time.google.com");
  Serial.print("[NTP] 시각 동기화");
  struct tm t;
  while (!getLocalTime(&t)) { delay(400); Serial.print("."); }
  Serial.println();
  Serial.printf("[NTP] 현재: %04d-%02d-%02d %02d:%02d:%02d\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);

  // ── 웹서버/웹소켓 ──
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });
  server.begin();
  Serial.println("[HTTP] 웹서버 시작");

  // ── 시간표 1회 로딩 ──
  fetchAllTimetables();
}

void loop() {
  ws.cleanupClients();
  checkButton();

  unsigned long now = millis();
  if (now - lastStatusPush >= STATUS_PUSH_INTERVAL) {
    lastStatusPush = now;

    // 자동 모드일 때만 시간표로 이번 역 판정
    if (!state.remoteActive && timetableReady) {
      long sec = nowSecOfDay();
      if (sec >= 0) {
        int idx = findCurrentStation(sec);
        if (idx != lastActiveIdx) {     // 역이 바뀌는 순간에만 출력 트리거
          lastActiveIdx = idx;
          if (idx >= 0) {
            applyStation(idx);          // 새 역 도착 → 안내 출력
          } else {
            state.activeIdx = -1;
            state.station   = "역 사이 이동 중";
            state.monitor   = "다음 역 안내 대기";
          }
        }
      }
    }

    state.monitor = (WiFi.status() == WL_CONNECTED) ? state.monitor : "WiFi 끊김";
    broadcastStatus();
  }
}
