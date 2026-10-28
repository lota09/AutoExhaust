기억하신 대로 외부 연결 핀은 7개입니다. 아래 내용은 [Config.h:19-27](Config.h#L19-L27)에 정의된 값을 기준으로 정리했습니다.

## ESP32-C3 SuperMini 핀 구성

| 기능 | 개수 | GPIO | 상수명 | 동작 방식 |
|---|---|---|---|---|
| 버튼 | 1 | **GPIO9** | `PIN_BUTTON` | `INPUT_PULLUP`, 눌리면 LOW (기본 BOOT 버튼과 같은 핀) |
| MOSFET 1 (배기팬) | 1 | **GPIO4** | `PIN_FAN` | LEDC PWM 5kHz / 8bit (0~255) |
| MOSFET 2 (조명) | 1 | **GPIO5** | `PIN_LIGHT` | LEDC PWM 5kHz / 8bit |
| RGB LED - R | 1 | **GPIO0** | `PIN_RGB_R` | LEDC PWM |
| RGB LED - G | 1 | **GPIO1** | `PIN_RGB_G` | LEDC PWM |
| RGB LED - B | 1 | **GPIO10** | `PIN_RGB_B` | LEDC PWM |
| 전압 센싱 | 1 | **GPIO3** (ADC1_CH3) | `PIN_PROBE` | `analogRead`, 12bit |

이 밖에 **GPIO8**은 보드에 내장된 LED(`PIN_BUILTIN`, LOW일 때 켜짐)로, 외부 배선은 없고 상태 표시에만 씁니다.

## 핀별 참고 사항

- **전압 센싱 (GPIO3)**: [Peripherals.cpp:37-40](Peripherals.cpp#L37-L40)에서 `raw × (3.1/4095) × 6`으로 계산합니다. 1:6 분압 저항을 거쳐 12V 전원을 읽도록 설계되어 있다는 뜻입니다(예: 50kΩ/10kΩ). `showPowerStatus()`는 이 값으로 RGB LED 색을 바꿉니다. 12.5V를 넘으면 초록, 11.5V를 넘으면 노랑, 그 아래는 빨강입니다.
- **버튼 (GPIO9)**: 부팅할 때 동작 모드를 정하는 스트래핑 핀입니다. 전원을 켜는 순간 버튼이 눌려 있으면 다운로드 모드로 들어갑니다. 또 [AutoExhaust.ino:104](AutoExhaust.ino#L104)에서 light sleep 상태를 깨우는 핀으로도 씁니다.
- **GPIO8**: 이것도 스트래핑 핀이라 외부 회로를 연결하지 않은 지금 구성이 안전합니다.
- **MOSFET (GPIO4/5)**: 원래 JTAG(MTMS/MTDI) 기능이 겹치는 핀이지만, C3는 USB-JTAG를 쓰기 때문에 문제없습니다. 로직 레벨 MOSFET의 게이트를 직접 구동하는 것으로 보입니다.
- **RGB LED**: R/G/B에 255를 쓸 때 켜지는 구조라서 공통 캐소드 LED로 보입니다.

같은 핀 배치가 [Integrated_test.ino](../Hardware_Test/Integrated_test/Integrated_test.ino#L21-L27)에도 똑같이 들어 있어서, 하드웨어 테스트를 할 때도 이 배선 그대로 쓰면 됩니다. 이어서 볼 부분을 말씀해 주세요. `AutoExhaust.ino`와 `Networking.cpp`에 아직 커밋하지 않은 변경 사항도 남아 있습니다.