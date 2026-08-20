# DotJabi — ESP32 지하철 점자·음성·진동 안내 프로토타입

ESP32 기반 지하철 손잡이 부착형 안내 모듈입니다. 현재 외부 API 대신 Serial Monitor의 `1`~`5` 명령으로 서울 지하철 2호선 내선순환 5개 역의 도착 이벤트를 발생시킵니다.

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
├── api.h/.cpp              # 향후 API를 위한 현재 stub
├── README.md
└── build/                   # ESP32 Dev Module 컴파일 검증 산출물
```

Arduino IDE는 폴더와 대표 `.ino` 파일의 이름이 같아야 하므로 현재 작업 폴더에 맞춰 `esp32_braille.ino`를 사용합니다.

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

현재 소스는 ESP32 board package 3.3.11, AccelStepper 1.64.0, DFRobotDFPlayerMini 1.0.6, MD_MAX72XX 3.5.1 조합의 `ESP32 Dev Module` 대상으로 실제 컴파일을 통과했습니다. `build/`는 이 검증에서 생성된 산출물이며 소스 수정 뒤에는 Arduino IDE에서 다시 빌드하십시오.

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

| Serial | 역 | Track | 진동 | 논리 위치 | 4096 step 기준 |
|---:|---|---:|:---:|---:|---:|
| 1 | 동대문역사공원 | 1 | ON | 0° | 0 |
| 2 | 신당 | 2 | ON | 72° | 819 |
| 3 | 상왕십리 | 3 | ON | 144° | 1638 |
| 4 | 왕십리 | 4 | **OFF** | 216° | 2458 |
| 5 | 한양대 | 5 | ON | 288° | 3277 |

위 step은 `MOTOR_STEPS_PER_REV`와 위치 인덱스에서 계산되므로 역 데이터에 magic number로 반복되지 않습니다.

Home sensor가 없으므로 부팅할 때 모터의 물리 위치를 반드시 동대문역사공원 위치(0°)에 맞춰 두어야 합니다. 펌웨어는 부팅 위치를 `0 step`이라고 가정할 뿐 절대 위치를 측정하지 못합니다. 기구가 slip하거나 전원이 차단된 상태에서 움직이면 위치 기준이 어긋납니다.

28BYJ-48의 실제 1회전 step은 제품과 감속기 편차가 있습니다. 한 바퀴가 맞지 않으면 `config.h`의 `MOTOR_STEPS_PER_REV`를 보정합니다. 모터가 회전하지 않고 떨기만 하면 `motor.cpp`의 HALF4WIRE 핀 순서(IN1, IN3, IN2, IN4)를 실제 ULN2003 보드에 맞게 조정합니다.

## 한글 매트릭스 표시

전체 Unicode 글꼴이나 UTF-8 parser를 사용하지 않습니다. `matrix_display.cpp`에는 필요한 15개 음절만 8×8 row bitmap으로 들어 있고, 각 역은 `GlyphId` 배열을 직접 가리킵니다.

- `신당`, `상왕십리`, `왕십리`, `한양대`: 32 pixel 안에서 정적 중앙 정렬
- `동대문역사공원`: 56-column buffer를 만들고 32-column 창을 1 pixel씩 이동
- 정적 표시 시간, scroll 간격, 반복 횟수는 모두 `config.h`에서 조정

FC-16 모듈의 조립 방향에 따라 글자가 좌우/상하 반전될 수 있습니다. 이때 다음 설정만 바꿉니다.

```cpp
#define MATRIX_HARDWARE_TYPE MD_MAX72XX::FC16_HW
constexpr bool MATRIX_REVERSE_COLUMNS = false;
constexpr bool MATRIX_FLIP_VERTICAL = false;
```

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
7. **통합:** `1`~`5` 각각에서 네 출력이 함께 시작되는지 확인합니다. `4`에는 진동이 없어야 합니다.
8. **재안내:** `3` 입력 후 버튼을 눌러 matrix/audio/vibration은 재시작하고 stepper target은 변하지 않는지 `status`로 확인합니다. `4`에서도 재안내 진동은 없어야 합니다.

DFPlayer 초기화가 실패해도 오류만 기록하고 matrix, motor, vibration, button은 계속 동작합니다.

## 주요 설정

`config.h`에서 주로 조정할 값:

```cpp
BUTTON_DEBOUNCE_MS
BUTTON_LED_ON_MS
VIBRATION_DURATION_MS
MATRIX_SCROLL_INTERVAL_MS
MATRIX_STATIC_DISPLAY_MS
MATRIX_SCROLL_REPEAT
MATRIX_INTENSITY
DFPLAYER_VOLUME
MOTOR_STEPS_PER_REV
MOTOR_MAX_SPEED
MOTOR_ACCELERATION
```

모든 timer 비교는 unsigned `millis()` 차이를 사용하므로 약 49일 후 rollover에도 안전합니다. Serial 명령은 고정 크기 32-byte buffer를 사용하며 런타임 `String` 할당을 하지 않습니다.

## 향후 API 연결

현재 `api.cpp`는 의도적으로 아무 외부 통신도 하지 않는 stub입니다. 출력 모듈은 입력 출처를 알지 못합니다. 향후 API 구현은 수신 데이터를 `StationId`로 변환하고 `station_notification.h`의 `notifyStation(station)`만 호출하면 됩니다. API 코드에서 matrix/audio/motor GPIO를 직접 제어하지 않는 구조를 유지하십시오.
