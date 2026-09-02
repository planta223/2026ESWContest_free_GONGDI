#include "wifi_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <time.h>

#include "api.h"
#include "audio.h"
#include "button.h"
#include "config.h"
#include "index_html.h"
#include "matrix_display.h"
#include "motor.h"
#include "station_data.h"
#include "station_notification.h"
#include "vibration.h"

namespace {

constexpr size_t WEB_COMMAND_JSON_CAPACITY = 128;
constexpr size_t WEB_STATUS_JSON_CAPACITY = 512;
constexpr size_t WEB_LOG_JSON_CAPACITY = 512;
constexpr size_t WEB_LOG_LINE_BUFFER_SIZE = 192;
constexpr uint8_t WEB_LOG_LINES_PER_UPDATE = 4;
constexpr uint32_t WEB_REBOOT_DELAY_MS = 750;

enum class WebCommandType : uint8_t {
  STATION,
  AUTO_MODE,
  REFRESH,
  REBOOT,
  STATUS,
  INVALID
};

struct PendingWebCommand {
  WebCommandType type;
  int16_t value;
  uint32_t clientId;
};

AsyncWebServer server(WEB_SERVER_PORT);
AsyncWebSocket webSocket(WEB_SOCKET_PATH);

StaticQueue_t commandQueueStorage;
uint8_t commandQueueBuffer[WIFI_COMMAND_QUEUE_LENGTH * sizeof(PendingWebCommand)] = {};
QueueHandle_t commandQueue = nullptr;

bool wasConnected = false;
uint32_t lastReconnectAt = 0;
uint32_t lastStatusPushAt = 0;
bool rebootScheduled = false;
uint32_t rebootScheduledAt = 0;

const char* commandName(WebCommandType type) {
  switch (type) {
    case WebCommandType::STATION:
      return "station";
    case WebCommandType::AUTO_MODE:
      return "auto";
    case WebCommandType::REFRESH:
      return "refresh";
    case WebCommandType::REBOOT:
      return "reboot";
    case WebCommandType::STATUS:
      return "status";
    default:
      return "unknown";
  }
}

void queueCommand(WebCommandType type, int16_t value, uint32_t clientId) {
  if (commandQueue == nullptr) {
    return;
  }
  const PendingWebCommand command = {type, value, clientId};
  if (xQueueSend(commandQueue, &command, 0) != pdTRUE) {
#if DEBUG_WIFI
    Serial.println(F("[WIFI] Web command queue full; command dropped"));
#endif
  }
}

void parseWebCommand(uint32_t clientId, const uint8_t* data, size_t length) {
  StaticJsonDocument<WEB_COMMAND_JSON_CAPACITY> document;
  if (deserializeJson(document, data, length)) {
    queueCommand(WebCommandType::INVALID, 0, clientId);
    return;
  }

  const char* command = document["cmd"] | "";
  const int16_t value = document["value"] | -1;
  if (strcmp(command, "station") == 0) {
    queueCommand(WebCommandType::STATION, value, clientId);
  } else if (strcmp(command, "auto") == 0) {
    queueCommand(WebCommandType::AUTO_MODE, value, clientId);
  } else if (strcmp(command, "refresh") == 0) {
    queueCommand(WebCommandType::REFRESH, value, clientId);
  } else if (strcmp(command, "reboot") == 0) {
    queueCommand(WebCommandType::REBOOT, value, clientId);
  } else {
    queueCommand(WebCommandType::INVALID, value, clientId);
  }
}

void onWebSocketEvent(AsyncWebSocket*,
                      AsyncWebSocketClient* client,
                      AwsEventType type,
                      void* argument,
                      uint8_t* data,
                      size_t length) {
  if (type == WS_EVT_CONNECT) {
#if DEBUG_WIFI
    Serial.printf("[WIFI] WebSocket connected: #%lu\n",
                  static_cast<unsigned long>(client->id()));
#endif
    queueCommand(WebCommandType::STATUS, 0, client->id());
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
#if DEBUG_WIFI
    Serial.printf("[WIFI] WebSocket disconnected: #%lu\n",
                  static_cast<unsigned long>(client->id()));
#endif
    return;
  }

  if (type != WS_EVT_DATA) {
    return;
  }

  const AwsFrameInfo* frame = static_cast<AwsFrameInfo*>(argument);
  if (frame == nullptr
      || !frame->final
      || frame->index != 0
      || frame->len != length
      || frame->opcode != WS_TEXT) {
    queueCommand(WebCommandType::INVALID, 0, client->id());
    return;
  }

  // Parsing and queueing are the callback's only jobs. No station notification
  // or hardware module is touched from the AsyncWebSocket task context.
  parseWebCommand(client->id(), data, length);
}

void sendAck(const PendingWebCommand& command, bool ok) {
  StaticJsonDocument<WEB_COMMAND_JSON_CAPACITY> document;
  document["type"] = "ack";
  document["cmd"] = commandName(command.type);
  if (command.type == WebCommandType::STATION) {
    document["value"] = command.value;
  }
  document["ok"] = ok;

  char output[WIFI_ACK_JSON_BUFFER_SIZE] = {};
  serializeJson(document, output, sizeof(output));
  webSocket.text(command.clientId, output);
}

const char* apiStatusText() {
  if (WiFi.status() != WL_CONNECTED) {
    return "Wi-Fi 연결 대기";
  }
  if (apiIsRefreshing()) {
    return "시간표 갱신 중";
  }
  if (!apiIsTimetableReady()) {
    return "NTP/시간표 대기";
  }
  return "시간표 준비";
}

void broadcastStatus() {
  const StationId station = currentStationId();
  const StationInfo* stationInfo = getStationInfo(station);

  StaticJsonDocument<WEB_STATUS_JSON_CAPACITY> document;
  document["type"] = "status";
  document["motor"] = motorIsMoving();
  document["speaker"] = audioIsReady();
  document["matrix"] = matrixIsActive();
  document["vibration"] = vibrationIsActive();
  document["button"] = buttonLedIsOn();
  document["activeIdx"] = stationInfo == nullptr
                              ? -1
                              : static_cast<int>(stationNumber(station) - 1);
  document["mode"] = apiIsAutoMode() ? "AUTO" : "REMOTE";
  document["monitor"] = apiStatusText();
  document["station"] = stationInfo == nullptr ? "안내 대기" : stationInfo->name;
  document["wifi"] = WiFi.status() == WL_CONNECTED;
  document["timetableReady"] = apiIsTimetableReady();
  document["refreshing"] = apiIsRefreshing();
  document["realtime"] = apiUsesRealtime();
  document["realtimePolling"] = apiIsRealtimePolling();
  document["realtimeNextSec"] = apiRealtimeSecondsUntilNextPoll();
  document["apiRemaining"] = apiRealtimeRemainingRequests();

  char output[WIFI_STATUS_JSON_BUFFER_SIZE] = {};
  serializeJson(document, output, sizeof(output));
  webSocket.textAll(output);
}

bool executeCommand(const PendingWebCommand& command) {
  switch (command.type) {
    case WebCommandType::STATION: {
      if (command.value < 0 || command.value >= STATION_COUNT) {
        return false;
      }
      const StationId station = stationIdFromNumber(static_cast<uint8_t>(command.value + 1));
      if (station == StationId::INVALID) {
        return false;
      }
      apiSetAutoMode(false);
      notifyStation(station);
      return true;
    }

    case WebCommandType::AUTO_MODE:
      apiSetAutoMode(true);
      return true;

    case WebCommandType::REFRESH:
      apiRequestRefresh();
      return true;

    case WebCommandType::REBOOT:
      if (rebootScheduled) {
        return false;
      }
      rebootScheduled = true;
      rebootScheduledAt = millis();
#if DEBUG_GLOBAL
      Serial.println(F("[SYSTEM] Remote reboot requested"));
#endif
      return true;

    case WebCommandType::STATUS:
      return true;

    default:
      return false;
  }
}

void broadcastPendingLogs() {
  if (webSocket.count() == 0) {
    return;
  }

  char line[WEB_LOG_LINE_BUFFER_SIZE] = {};
  for (uint8_t sent = 0;
       sent < WEB_LOG_LINES_PER_UPDATE && debugLogPopLine(line, sizeof(line));
       ++sent) {
    StaticJsonDocument<WEB_LOG_JSON_CAPACITY> document;
    document["type"] = "log";
    document["line"] = line;

    char output[WEB_LOG_JSON_CAPACITY] = {};
    serializeJson(document, output, sizeof(output));
    webSocket.textAll(output);
  }
}

void consumeWebCommands() {
  PendingWebCommand command = {};
  while (commandQueue != nullptr
         && xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
    const bool ok = executeCommand(command);
    if (command.type != WebCommandType::STATUS) {
      sendAck(command, ok);
    }
    broadcastStatus();
  }
}

}  // namespace

void wifiBegin() {
  commandQueue = xQueueCreateStatic(
      WIFI_COMMAND_QUEUE_LENGTH,
      sizeof(PendingWebCommand),
      commandQueueBuffer,
      &commandQueueStorage);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastReconnectAt = millis();

  // Both Wi-Fi association and SNTP synchronization proceed asynchronously.
  configTime(NTP_GMT_OFFSET_SEC,
             NTP_DAYLIGHT_OFFSET_SEC,
             NTP_SERVER_1,
             NTP_SERVER_2);

  webSocket.onEvent(onWebSocketEvent);
  server.addHandler(&webSocket);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.begin();

#if DEBUG_WIFI
  Serial.printf("[WIFI] STA connecting to %s\n", WIFI_SSID);
  Serial.printf("[WIFI] HTTP / and WebSocket %s ready\n", WEB_SOCKET_PATH);
#endif
}

void wifiUpdate() {
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected != wasConnected) {
    wasConnected = connected;
#if DEBUG_WIFI
    if (connected) {
      Serial.printf("[WIFI] Connected: http://%s/\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println(F("[WIFI] Disconnected"));
    }
#endif
    if (connected) {
      apiRequestRefresh();
    }
  }

  if (!connected
      && static_cast<uint32_t>(now - lastReconnectAt) >= WIFI_RECONNECT_INTERVAL_MS) {
    lastReconnectAt = now;
#if DEBUG_WIFI
    // ESP32 core 3.x performs reconnects from its Wi-Fi event handler when
    // auto-reconnect is enabled. Calling begin() or reconnect() here races that
    // in-progress connection and produces "sta is connecting" errors.
    Serial.printf("[WIFI] Waiting for auto-reconnect (status=%d)\n",
                  static_cast<int>(WiFi.status()));
#endif
  }

  consumeWebCommands();
  webSocket.cleanupClients();
  broadcastPendingLogs();

  if (static_cast<uint32_t>(now - lastStatusPushAt) >= STATUS_PUSH_INTERVAL) {
    lastStatusPushAt = now;
    broadcastStatus();
  }

  if (rebootScheduled
      && static_cast<uint32_t>(now - rebootScheduledAt) >= WEB_REBOOT_DELAY_MS) {
#if DEBUG_GLOBAL
    Serial.println(F("[SYSTEM] Rebooting now"));
#endif
    broadcastPendingLogs();
    delay(50);
    ESP.restart();
  }
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}
