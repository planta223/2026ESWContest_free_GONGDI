# DotJabi — ESP32 지하철 점자·음성·진동 안내 프로토타입

ESP32 기반 지하철 손잡이 부착형 안내 모듈입니다. Serial Monitor, 웹 원격 선택, 서울 지하철 시간표/실시간 위치 API 자동 판정이 모두 `notifyStation(StationId)` 경계로 합쳐집니다.

하나의 역 이벤트가 다음 출력을 각각 시작합니다.

- MAX7219 8×32: 전용 8×8 한글 비트맵 표시
- DFPlayer Mini: `/mp3/0001.mp3`~`0005.mp3` 재생
- 진동모터: 역 데이터에 따른 1초 진동(왕십리 제외)
- 28BYJ-48: 다섯 개의 가상 점자 위치 중 해당 위치로 이동

스텝모터, 매트릭스 스크롤, 진동, 버튼 debounce, 버튼 LED는 `delay()` 없이 `loop()`에서 함께 갱신됩니다.

## 프로젝트 파일

```text
esp32_braille/
├── esp32_braille.ino       # setup/loop, 안내 통합, Serial 명령
├── config.h                # GPIO와 조정 가능 값
├── station_data.h/.cpp     # 역·음원·진동·모터·Glyph 데이터
├── station_notification.h  # 모든 입력 source의 공통 안내 이벤트 경계
├── motor.h/.cpp            # AccelStepper non-blocking 구동
├── vibration.h/.cpp        # millis 기반 진동 타이머
├── audio.h/.cpp            # UART2 DFPlayer 제어
├── matrix_display.h/.cpp   # 전용 한글 glyph와 pixel scroll
├── button.h/.cpp           # polling debounce와 LED 타이머
├── api.h/.cpp              # background HTTP, 시간표/실시간 AUTO 역 판정
├── api_usage.h/.cpp        # realtime 일일 호출 횟수 NVS 관리
├── wifi_manager.h/.cpp     # Wi-Fi/NTP/HTTP/WebSocket와 command queue
├── index_html.h            # PROGMEM 웹 디버깅 UI
├── legacy/esp32_main.ino   # 이식 전 reference; sketch compile 대상에서 제외
├── mp3/                    # DFPlayer용 0001~0005 음원
├── doc/                    # 결선·핀아웃 참고 자료
├── THIRD_PARTY_NOTICES.md
├── LICENSES/
├── README.md
└── build/                  # 이전 ESP32 compile 산출물(소스 아님)
```

Arduino IDE는 폴더와 대표 `.ino` 파일의 이름이 같아야 하므로 현재 작업 폴더에 맞춰 `esp32_braille.ino`를 사용합니다. `legacy/`는 sketch root나 `src/`가 아니므로 그 안의 중복 `setup()/loop()`는 빌드되지 않습니다.

`wifi_manager`라는 파일명은 의도적입니다. Windows의 대소문자 비구분 파일 시스템에서 로컬 `wifi.h`는 ESP32 코어의 `<WiFi.h>`와 충돌합니다.

## 하드웨어

- Happy House ESP32 DevKitC WROOM-32D V4 CP2102
- DFRobot DFPlayer Mini(DFR0299), microSD, 8Ω 1W 스피커
- 5V 28BYJ-48 스텝모터, ULN2003 드라이버
- MAX7219 8×32 LED Dot Matrix(FC-16 계열 초기 설정)
- PN-VM102 코인형 진동모터, NPN 트랜지스터, flyback diode
- Push button, LED, 저항, 별도 5V 전원 및 약 3V DC-DC converter

## GPIO 핀 맵

| 기능 | ESP32 GPIO | 연결 |
|---|---:|---|
| MAX7219 DIN | 23 | Matrix DIN |
| MAX7219 CLK | 18 | Matrix CLK |
| MAX7219 CS | 27 | Matrix CS/LOAD |
| DFPlayer RX2 | 16 | DFPlayer TX → ESP32 |
| DFPlayer TX2 | 17 | ESP32 → 1kΩ → DFPlayer RX |
| ULN2003 IN1 | 25 | Stepper IN1 |
| ULN2003 IN2 | 26 | Stepper IN2 |
| ULN2003 IN3 | 32 | Stepper IN3 |
| ULN2003 IN4 | 33 | Stepper IN4 |
| 진동 제어 | 14 | 1kΩ → NPN Base |
| 재안내 버튼 | 21 | 버튼 반대편은 GND, `INPUT_PULLUP` |
| 버튼 LED | 22 | 220~330Ω → LED → GND |

현재 핀 맵에서 기능 간 GPIO 충돌은 없습니다. GPIO 16/17은 ESP32 UART2에만 사용하고 MAX7219는 지정 핀의 software SPI 생성자를 사용합니다.

## 전원 및 회로 연결

```text
PC USB ── ESP32

외부 안정화 5V
 ├── MAX7219 VCC
 ├── DFPlayer VCC
 ├── ULN2003/28BYJ-48 5V
 └── DC-DC 약 3V ── 진동모터(+)

ESP32 GND ── 외부 5V GND ── DC-DC GND ── 모든 모듈 GND
```

Stepper, MAX7219, DFPlayer 또는 진동모터를 ESP32의 3.3V 핀에서 공급하지 마십시오. 외부 전원과 ESP32의 GND는 반드시 공통으로 연결합니다. 외부 5V의 전류 용량은 모터의 기동/정지 전류까지 고려해야 합니다.

진동모터는 GPIO에 직접 연결하지 않습니다.

```text
GPIO14 ── 1kΩ ── NPN Base
                   Collector ── 진동모터(-)
ESP32/공통 GND ── Emitter
약 3V ─────────── 진동모터(+)
```

- 모터 양단에 flyback diode를 역방향으로 연결합니다. Cathode는 Motor(+) 쪽입니다.
- NPN Base-GND 사이에 약 10kΩ pulldown 추가를 권장합니다.
- GPIO17과 DFPlayer RX 사이에는 약 1kΩ 직렬저항을 권장합니다.

MAX7219는 5V 전원에서 동작하지만 ESP32 신호는 3.3V입니다. 프로토타입은 직결을 먼저 시험하도록 되어 있습니다. 표시가 불안정하면 DIN/CLK/CS에 74AHCT125 같은 3.3V→5V logic level shifter를 추가하십시오.

## Arduino 환경과 라이브러리

Arduino Library Manager에서 다음 정확한 이름으로 설치합니다.

- `AccelStepper` by Mike McCauley
- `DFRobotDFPlayerMini` by DFRobot
- `MD_MAX72XX` by MajicDesigns
- `ArduinoJson` by Benoit Blanchon
- `ESP Async WebServer` by ESP32Async
- `Async TCP` by ESP32Async

`WiFi`, `HTTPClient`, `Network`, `Preferences`(NVS), FreeRTOS와 SNTP/time API는 ESP32 board package에서 제공됩니다.

권장 Arduino IDE 설정:

| 항목 | 값 |
|---|---|
| Board package | `esp32` by Espressif Systems |
| Board | `ESP32 Dev Module` |
| Upload Speed | `921600` 또는 연결이 불안정하면 `115200` |
| CPU Frequency | `240MHz (WiFi/BT)` |
| Flash Frequency | `80MHz` |
| Flash Mode | `QIO` (보드에 따라 `DIO`) |
| Partition Scheme | `Default 4MB with spiffs` |
| Serial Monitor | `115200 baud` |

프로젝트 폴더를 통째로 연 뒤 `esp32_braille.ino`를 컴파일/업로드합니다. PlatformIO 전용 파일은 필요하지 않습니다.

현재 소스는 ESP32 board package 3.3.11, AccelStepper 1.64.0, DFRobotDFPlayerMini 1.0.6, MD_MAX72XX 3.5.1, ArduinoJson 6.21.5, ESP Async WebServer 3.12.0, Async TCP 3.5.0 조합의 `ESP32 Dev Module` 대상으로 실제 컴파일을 통과했습니다.

## microSD 음원 배치

DFPlayer가 인식할 카드(FAT32 권장)에 다음 경로와 이름으로 복사합니다.

```text
/mp3/0001.mp3  이번 역은 동대문역사공원역입니다.
/mp3/0002.mp3  이번 역은 신당역입니다.
/mp3/0003.mp3  이번 역은 상왕십리역입니다.
/mp3/0004.mp3  이번 역은 왕십리역입니다.
/mp3/0005.mp3  이번 역은 한양대역입니다.
```

저장소의 `mp3/0_동대문역사공원.mp3` 같은 원본 이름은 참고용이며 DFPlayer의 `/mp3/000N.mp3` 규칙과 다릅니다. microSD에 복사할 때 위 이름으로 변경해야 합니다. 파일시스템의 정렬 문제를 줄이려면 빈 카드에 `0001`부터 순서대로 복사하는 것이 좋습니다.

## 역 데이터와 모터 위치

| Serial | 역 | Track | 진동 | 이전 역에서의 간격 | 누적 목표 step |
|---:|---|---:|:---:|---:|---:|
| 1 | 동대문역사공원 | 1 | ON | - | 0 |
| 2 | 신당 | 2 | ON | 819 | 819 |
| 3 | 상왕십리 | 3 | ON | 819 | 1638 |
| 4 | 왕십리 | 4 | **OFF** | 820 | 2458 |
| 5 | 한양대 | 5 | ON | 819 | 3277 |

동대문역사공원은 항상 `0 step`입니다. 나머지 네 역의 목표는 `config.h`의 `MOTOR_INTERVAL_TO_*_STEPS` 4개를 앞에서부터 누적해 계산합니다. 기본값은 기존 등간격 목표와 같지만, 각 구간을 독립적으로 보정할 수 있습니다.

Home sensor가 없으므로 부팅할 때 모터의 물리 위치를 반드시 동대문역사공원 위치(0°)에 맞춰 두어야 합니다. 펌웨어는 부팅 위치를 `0 step`이라고 가정할 뿐 절대 위치를 측정하지 못합니다. 기구가 slip하거나 전원이 차단된 상태에서 움직이면 위치 기준이 어긋납니다.

28BYJ-48의 실제 이동 step은 제품과 감속기 편차가 있습니다. 각 역 위치가 맞지 않으면 `config.h`의 해당 `MOTOR_INTERVAL_TO_*_STEPS`를 보정합니다. 모터가 회전하지 않고 떨기만 하면 `motor.cpp`의 HALF4WIRE 핀 순서(IN1, IN3, IN2, IN4)를 실제 ULN2003 보드에 맞게 조정합니다.

## 한글 매트릭스 표시

전체 Unicode 글꼴이나 UTF-8 parser를 사용하지 않습니다. `matrix_display.cpp`에는 필요한 15개 음절만 8×8 row bitmap으로 들어 있고, 각 역은 `GlyphId` 배열을 직접 가리킵니다. 한글 bitmap은 8×8 전용 픽셀 폰트인 Dalmoori를 기반으로 하며 관련 고지는 `THIRD_PARTY_NOTICES.md`에 있습니다.

- 계산된 전체 폭이 32 pixel 이하인 역명: 정적 중앙 정렬
- 계산된 전체 폭이 32 pixel을 초과하는 역명: 32-column 창을 1 pixel씩 이동
- 각 8×8 한글 글리프는 기본적으로 반시계 방향 90° 회전
- 정적 역명은 다음 역 전환까지 계속 표시, 긴 역명은 지속적으로 반복 scroll
- scroll 간격과 밝기는 `config.h`에서 조정

`MATRIX_GLYPH_SPACING_CELLS`는 인접 글자 사이에만 `n`개의 빈 pixel column을 삽입합니다. 즉 높이 8 pixel인 간격 영역의 크기는 `8×n`이고 전체 폭은 `8*G + n*(G-1)`입니다. 값 0은 기존의 간격 없는 출력과 같습니다. scroll content는 RAM buffer로 펼치지 않고 필요한 column을 즉시 계산합니다.

FC-16 모듈의 조립 방향에 따라 글자가 좌우/상하 반전될 수 있습니다. 이때 다음 설정만 바꿉니다.

```cpp
#define MATRIX_HARDWARE_TYPE MD_MAX72XX::FC16_HW
constexpr bool MATRIX_REVERSE_COLUMNS = true;
constexpr bool MATRIX_FLIP_VERTICAL = false;
constexpr bool MATRIX_ROTATE_GLYPHS_CCW = true;
```

`MATRIX_ROTATE_GLYPHS_CCW=false`로 바꾸면 원래 글리프 방향으로 되돌릴 수 있습니다. 이 회전은 각 8×8 글자에 적용되므로 역명 순서와 8×32 scroll 방향은 유지됩니다.

SZH-EKAD-115은 `FC16_HW`, 장치 수 4가 맞습니다. MD_MAX72XX에서 FC-16 chain의 column 번호는 화면 오른쪽부터 증가하므로, 이 프로젝트의 일반적인 좌→우 좌표를 맞추기 위해 `MATRIX_REVERSE_COLUMNS=true`가 기본값입니다. 글자 순서까지 좌우가 뒤집혀 보이는 하드웨어 revision에서만 이 값을 바꾸십시오.

## Serial 디버그 명령

Serial Monitor에서 newline 유무와 관계없이 다음을 입력할 수 있습니다.

```text
help

1                 전체 동대문역사공원 안내
2                 전체 신당 안내
3                 전체 상왕십리 안내
4                 전체 왕십리 안내(진동 없음)
5                 전체 한양대 안내

motor 1           첫 역 위치(0 step)로 이동
motor 2
motor 3
motor 4
motor 5

vib               설정 시간 동안 진동
audio 1           /mp3/0001.mp3 재생
audio 2..5
matrix 1          긴 역명 smooth scrolling 시험
matrix 2..5       정적 역명 시험
status            현재 역과 모든 모듈 상태
```

`motor 1..5`의 명령 번호는 Serial 역 번호와 같고, 내부의 zero-based 논리 위치는 각각 Position 0..4입니다.

## 권장 시험 순서

1. **전원만 점검:** 멀티미터로 5V/약 3V 및 모든 공통 GND를 확인합니다.
2. **Stepper:** `motor 1`~`motor 5`를 순서대로 입력해 위치와 방향을 확인합니다.
3. **Vibration:** `vib`를 입력해 1초 뒤 자동으로 꺼지는지 확인합니다.
4. **Audio:** `audio 1`~`audio 5`로 파일 번호와 볼륨을 확인합니다.
5. **Matrix:** `matrix 2`로 방향을 확인한 뒤 `matrix 1`로 pixel scroll을 확인합니다.
6. **Button:** 아직 역을 고르지 않고 누르면 LED만 1초 켜지고 `[BUTTON] No current station`이 출력되어야 합니다.
7. **통합:** 기본 설정에서 `1`~`5`를 입력하면 matrix와 stepper만 즉시 시작하고 audio/vibration은 시작하지 않아야 합니다.
8. **재안내:** `3` 입력 후 버튼을 눌러 matrix/audio/vibration은 재시작하고 stepper target은 변하지 않는지 `status`로 확인합니다. `4`에서도 재안내 진동은 없어야 합니다.

DFPlayer 초기화가 실패해도 오류만 기록하고 matrix, motor, vibration, button은 계속 동작합니다.

## 주요 설정

`config.h`에서 주로 조정할 값:

`AUDIO_BUTTON_ONLY_MODE`와 `VIBRATION_BUTTON_ONLY_MODE`를 `true`로 두면 역 전환 때는 해당 모듈을 시작하지 않고, 물리 버튼을 누를 때만 현재 역 안내를 실행합니다. 각 값을 `false`로 바꾸면 기존처럼 역 전환 시점에도 자동 실행됩니다.

```cpp
BUTTON_DEBOUNCE_MS
BUTTON_LED_ON_MS
VIBRATION_DURATION_MS
VIBRATION_BUTTON_ONLY_MODE
MATRIX_SCROLL_INTERVAL_MS
MATRIX_INTENSITY
MATRIX_GLYPH_SPACING_CELLS
DFPLAYER_VOLUME
AUDIO_BUTTON_ONLY_MODE
MOTOR_STEPS_PER_REV
MOTOR_MAX_SPEED
MOTOR_ACCELERATION
MOTOR_INTERVAL_TO_SINDANG_STEPS
MOTOR_INTERVAL_TO_SANGWANGSIMNI_STEPS
MOTOR_INTERVAL_TO_WANGSIMNI_STEPS
MOTOR_INTERVAL_TO_HANYANG_UNIV_STEPS
WIFI_SSID / WIFI_PASS
WIFI_RECONNECT_INTERVAL_MS
NTP_SERVER_1 / NTP_SERVER_2
AUTO_API_SOURCE
SUBWAY_API_BASE_URL / SUBWAY_API_KEY
REALTIME_API_BASE_URL / REALTIME_API_KEY
REALTIME_API_POLL_INTERVAL_MS
REALTIME_API_TRANSITION_COOLDOWN_MS
REALTIME_API_DAILY_LIMIT
REALTIME_API_FALLBACK_TO_TIMETABLE
TRAIN_DIRECTION / DEPARTURE_UPDATE_DELAY_SEC[0..3] / STATION_WINDOW
API_REFRESH_INTERVAL_MS
STATUS_PUSH_INTERVAL
```

모든 timer 비교는 unsigned `millis()` 차이를 사용하므로 약 49일 후 rollover에도 안전합니다. Serial 명령은 고정 크기 32-byte buffer를 사용하며 런타임 `String` 할당을 하지 않습니다.

## Wi-Fi, API와 웹 UI

Wi-Fi 연결과 NTP는 setup에서 기다리지 않고 비동기로 진행됩니다. `AUTO_API_SOURCE`의 기본값은 기존 동작을 유지하는 `AutoApiSource::TIMETABLE`이며, `AutoApiSource::REALTIME`로 바꾸면 서울시 `realtimePosition`을 사용합니다. 두 모드 모두 HTTP 요청과 JSON 파싱은 `api.cpp`의 FreeRTOS worker가 수행하고, worker는 `notifyStation()`이나 HW 모듈을 호출하지 않습니다.

TIMETABLE 모드는 메인 `apiUpdate()`가 매초 `LEFTTIME + DEPARTURE_UPDATE_DELAY_SEC[출발역]`과 현재 시각을 비교합니다. REALTIME 모드는 기본 15초마다 추적 중인 `TRAIN_NO`의 현재 역과 `trainSttus`를 확인하며, 출발 상태(`2`) 또는 다음 역으로 전진한 사실이 확인되면 다음 역을 안내합니다. 역 전환 직후에는 기본 45초 동안 realtime polling을 쉬며, 실패하거나 일일 한도에 도달하면 준비된 시간표 cache로 fallback합니다.

`api_usage.cpp`는 realtime HTTP 요청을 실제로 시도하기 직전에 일일 사용량을 NVS에 기록합니다. 재부팅 후에도 횟수가 유지되고 로컬 날짜가 바뀌면 자동으로 0부터 다시 시작합니다. 시간표 API 요청은 별도 키와 fallback 경로이므로 realtime 일일 사용량에 포함하지 않습니다.

웹 UI는 `http://<ESP32-IP>/`의 `INDEX_HTML`이며 WebSocket endpoint는 `/ws`입니다.

```json
{"cmd":"station","value":0}
{"cmd":"auto"}
{"cmd":"refresh"}
```

WebSocket callback은 JSON을 고정 길이 FreeRTOS queue에 넣기만 합니다. `wifiUpdate()`가 메인 loop에서 `station`을 REMOTE 안내로 실행하고, `auto`로 API 자동 판정에 복귀하며, `refresh`로 background cache 갱신을 요청합니다. ACK는 `{"type":"ack",...}`이고 상태는 기존 `type=status`, `motor`, `speaker`, `vibration`, `button`, `activeIdx`, `mode`, `monitor`, `station` 필드를 유지하면서 `matrix`, `wifi`, `timetableReady`, `refreshing`을 추가합니다. `speaker`는 재생 여부가 아니라 실제 제공 가능한 `audioIsReady()` 즉 DFPlayer 준비 상태입니다.
