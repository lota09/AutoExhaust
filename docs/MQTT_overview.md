# 뱀부랩 X1C MQTT 프로토콜 개요

`ui.txt`와 `report_idle.txt` 파일을 분석하여, ESP32 외부 배기 팬 제어 프로젝트를 위한 뱀부랩 X1C의 MQTT 프로토콜 내용을 정리했습니다.

## 1. 연결 정보 (Connection Details)
- **브로커 IP**: `192.168.50.209` (출처: `ui.txt`)
- **포트**: `8883` (보안 MQTT / MQTTS)
- **SSL/TLS**: 필수 (자가 서명된 인증서 사용, 검증 과정은 건너뛰어야 함)
- **사용자 이름 (Username)**: `bblp`
- **비밀번호 (Password)**: `[LAN Access Code]` (프린터 설정에서 확인한 8자리 코드)

## 2. 토픽 구조 (Topic Structure)
프린터는 자신의 시리얼 번호가 포함된 특정 토픽으로 상태 정보를 발행합니다.

- **메인 상태 토픽**: `device/YOUR_PRINTER_SN/report`
  - *참고: `YOUR_PRINTER_SN`는 `ui.txt`에서 확인된 프린터의 시리얼 번호입니다.*
- **데이터 형식**: JSON

## 3. 팬 제어를 위한 주요 JSON 필드
전송되는 데이터는 거대한 JSON 객체이며, 상태 정보는 주로 `print` 객체 안에 들어있습니다.

### A. 출력 상태 (`print.gcode_state`)
프린터가 현재 작동 중인지 판단하는 가장 핵심적인 필드입니다.

- **경로**: `print` -> `gcode_state`
- **확인된 값 (대기 시)**: `"FINISH"` (`report_idle.txt` 기준)
- **예상되는 값**:
  - `"RUNNING"`: 출력 진행 중. **(팬 가동 조건)**
  - `"FINISH"`: 출력 완료.
  - `"IDLE"`: 대기 상태.
  - `"PAUSE"`: 일시 정지.
  - `"PREPARE"`: 준비 중 (캘리브레이션, 베드 레벨링 등).

### B. 진행 단계 (`print.mc_print_stage`)
출력 단계를 숫자로 표현한 값입니다.
- **경로**: `print` -> `mc_print_stage`
- **확인된 값**: `"1"`

### C. 상세 상태 정보 (Advanced Status)
User request에 따라 남은 시간, 레이어 정보, 조명 상태, 소재 정보 등을 추가하여 상세하게 기술했습니다.

#### 1. 시간 및 진행률
- **남은 시간**: `print.mc_remaining_time` (분 단위 정수값)
- **진행률 (%)**: `print.mc_percent`
- **현재 레이어**: `print.layer_num`
- **총 레이어**: `print.total_layer_num`

#### 2. 내부 조명 (Lights)
프린터 내부 조명 상태를 확인하여 수면 모드 등을 구현할 때 유용합니다.
- **경로**: `print.lights_report` (배열 형태)
- **구조 예시**:
  ```json
  "lights_report": [
    { "node": "chamber_light", "mode": "on" },
    { "node": "work_light", "mode": "off" }
  ]
  ```
- **node**: `chamber_light` (챔버등), `work_light` (노즐부 조명)
- **mode**: `on`, `off`, `flashing`

#### 3. 사용 중인 소재 (Material)
현재 장착되거나 사용 중인 필라멘트 정보입니다.
- **AMS 트레이 정보**: `print.ams.ams[0].tray` (배열)
  - 각 트레이 객체(`{...}`) 안에 상세 정보가 있습니다.
  - `tray_type`: 소재 종류 (예: "PLA", "ABS", "PETG")
  - `tray_sub_brands`: 세부 브랜드 (예: "PLA Basic")
  - `tray_color`: 색상 코드 (RGBA Hex값, 예: "161616FF")
  - `remain`: 남은 양 추정치 (퍼센트 혹은 미터 단위)
- **외부 스풀 (External Spool)**: `print.vt_tray`
  - AMS를 통하지 않고 후면 거치대 등을 사용할 때의 정보입니다. 구조는 AMS 트레이와 유사합니다.

### D. 내부 팬 상태 (Cooling)
프린터 자체에 내장된 팬들의 상태 정보입니다. (숫자가 아닌 문자열로 전송됨)
- `print.big_fan1_speed`: 보조(Aux) 팬 속도
- `print.big_fan2_speed`: 챔버(Chamber) 팬 속도
- `print.cooling_fan_speed`: 파츠 쿨링 팬 속도
- `print.heatbreak_fan_speed`: 핫엔드 팬 속도

## 4. ESP32 제어 시나리오 (Strategy)
자동 외부 배기 팬 시스템 구현을 위한 단계는 다음과 같습니다:

1.  **연결 (Connect)**: ESP32가 `192.168.50.209`의 `8883` 포트로 MQTTS(SSL) 접속을 시도합니다.
2.  **구독 (Subscribe)**: `device/YOUR_PRINTER_SN/report` 토픽을 구독합니다.
3.  **파싱 (Parse)**: 수신된 JSON 데이터를 해석합니다.
4.  **로직 (Logic)**:
    - `print.gcode_state` 값을 지속적으로 모니터링합니다.
    - **IF** `gcode_state`가 `"RUNNING"` (또는 상황에 따라 `"PREPARE"`) 이면
    - **THEN** 외부 팬 릴레이/MOSFET을 **ON** 합니다.
    - **ELSE** (`"FINISH"`, `"IDLE"` 등일 경우)
    - **THEN** 외부 팬을 **OFF** 합니다 (필요시 잔류 가스 배출을 위한 지연 OFF 기능 추가).
