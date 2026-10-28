# 3D 프린터 연동 배기 시스템 개발 프로젝트 보고서

## 1. MQTT 개념 (Concept)

MQTT(Message Queuing Telemetry Transport)는 제한된 통신 대역폭과 낮은 전력 소모를 갖는 IoT 장치에 최적화된 경량 메시징 프로토콜입니다.

*   **발행/구독(Publish/Subscribe) 모델**: 클라이언트가 직접 서로 통신하지 않고 '브로커(Broker)'를 통해 메시지를 주고받습니다.
*   **브로커 (Broker)**: 메시지 중계자 역할을 하며, Bambu Lab X1C 프린터는 내부에 이 브로커 기능을 탑재하고 있습니다.
*   **토픽 (Topic)**: 메시지의 주제(주소)입니다. 구독자는 특정 토픽을 구독함으로써 원하는 정보를 수신합니다.
*   **페이로드 (Payload)**: 실제 전송되는 데이터로, 본 프로젝트에서는 JSON 형식을 사용합니다.

## 2. 뱀부랩 X1C(X1C) MQTT 사양

프린터 내부 브로커에 접속하기 위한 사양은 다음과 같습니다.

*   **프로토콜**: MQTTS (Secure MQTT over SSL/TLS)
*   **포트**: 8883 (일반 MQTT 포트인 1883이 아님)
*   **암호화**: SSL/TLS 사용 (단, 사설 인증서(Self-signed)를 사용하므로 클라이언트에서 인증서 검증을 건너뛰도록 설정해야 함)
*   **인증 정보**:
    *   **사용자명(Username)**: `bblp` (고정)
    *   **비밀번호(Password)**: LAN Access Code (프린터 설정 화면에서 확인 가능)
*   **토픽 구조**: `device/[프린터시리얼번호]/report`

## 3. MQTT 연결 과정 (Connection Flow)

ESP32 마이크로컨트롤러가 프린터 정보를 수신하는 과정은 다음과 같습니다.

1.  **WiFi 연결**: 로컬 네트워크에 접속합니다.
2.  **MQTTS 접속 시도**:
    *   `WiFiClientSecure` 라이브러리를 사용합니다.
    *   `setInsecure()` 함수를 호출하여 프린터의 사설 인증서 오류를 무시하고 접속을 허용합니다.
3.  **MQTT 인증**: `bblp` 계정과 LAN Access Code로 로그인합니다.
4.  **구독(Subscribe)**: `device/00M.../report` 토픽을 구독하여 1초 간격으로 상태 메시지를 수신합니다.

## 4. MQTT 메시지 및 주요 JSON 필드 분석

수신된 메시지는 거대한 JSON 객체이며, `print` 키 내부에 배기 팬 제어에 필요한 핵심 정보들이 포함되어 있습니다. (참고 파일: `report_idle.txt`)

### 4.1. 출력 상태 (`print.gcode_state`)
시스템 자동화의 핵심 트리거가 되는 필드입니다.
*   **`RUNNING`**: 실제 출력이 진행 중인 상태. (배기 팬 **ON** 조건)
*   **`FINISH`**: 출력이 완료된 상태.
*   **`IDLE`**: 대기 상태. (배기 팬 **OFF** 조건)
*   **`PAUSE`**: 일시 정지 상태.
*   **`PREPARE`**: 캘리브레이션 등 준비 동작 중.

### 4.2. 온도 정보 (Temperatures)
프린터의 각 부위별 온도 데이터입니다.
*   **노즐 온도**: `print.nozzle_temper` (예: 23.0)
*   **베드 온도**: `print.bed_temper` (예: 19.0)
*   **챔버 온도**: `print.device.ctc.info.temp` (내부 센서값)

### 4.3. 진행 정보 (Progress)
출력 진행 상황을 모니터링하기 위한 데이터입니다.
*   **남은 시간**: `print.mc_remaining_time` (분 단위)
*   **진행률**: `print.mc_percent` (%)
*   **현재 레이어**: `print.layer_num`
*   **총 레이어**: `print.total_layer_num`

### 4.4. 기타 정보
*   **내부 조명**: `print.lights_report` (배열 형태, `chamber_light`, `work_light` 상태 확인 가능)
*   **소재 정보(AMS)**: `print.ams.ams[0].tray` 배열에서 장착된 필라멘트의 종류(`tray_type`), 색상(`tray_color`), 남은 양 등을 확인할 수 있습니다.
