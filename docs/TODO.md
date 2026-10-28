# 📋 AutoExhaust 2.0 TODO

## ✅ 2026-10-28 펌웨어 개편 (구현 완료, 실기 검증 필요)
- [x] 슬립 전면 폐기 (`STATE_SLEEP`, light sleep, modem sleep)
- [x] WiFi: TX 11dBm (설치 위치 RSSI 약 -77), 끊김 사유 로그, 30초 수동 재접속, 5분 재부팅
- [x] MQTT `connect()`에 사용자/비밀번호 전달, 접속 직후 pushall
- [x] 상태 머신 재구성: BOOT/IDLE/RUNNING/COOLDOWN/MANUAL/OFFLINE/FAULT (`AutoExhaust_Flow.md`)
- [x] `PREPARE` 상태 포함 (ESP8266 로그에서 실제로 확인)
- [x] 전원 예산: 조명 상한 70%, 소프트 램프, 팬 25kHz + 킥스타트
- [x] 저전압/과열 FAULT 래치, 전압 측정을 `analogReadMilliVolts` 평균으로 변경
- [x] 버튼: 뗄 때 동작 (짧게 / 2초 / 10초)
- [x] LED: 내장 LED는 WiFi 상태 전용, RGB 표시안 적용 (이중 제어 버그 해결)
- [x] 웹 설정값 NVS 저장, REST API (`docs/API.md`)
- [x] 파티션 Minimal SPIFFS (1.9MB APP): **USB로 1회 플래시 필요**

## ✅ 2026-10-28 2차
- [x] 버튼 10초 재부팅 제거 (기계식 스위치로 대체)
- [x] OTA 자동 롤백 + `ota.sh` (롤백 시험 완료)
- [x] 웹 UI 새로 작성: 2초 실시간 갱신, `/api/*` 기반, 라이트/다크 테마

## 🔍 실기 검증
- [x] MQTT 인증 접속 (부팅 후 약 6초)
- [x] 챔버 조명 `ledctrl` 명령 (프린터가 `lights_report`로 OFF/ON 확인)
- [x] API: 설정 상한 적용, 수동 모드 ON/OFF, 오류 응답, OTA (새 파티션)
- [ ] 출력 1회 전체 사이클: RUNNING → COOLDOWN 5분 → IDLE
- [ ] OFFLINE: 출력 중 공유기 끄기 → 팬 유지 → 복구 후 상태 복귀
- [ ] 버튼 3종 동작, 부팅 중 버튼 누름 무시
- [x] 콜드 부트 / 최종 12V 전원 경로에서 WiFi 접속 (2026-10-28: WiFi 5.1초, MQTT 7초, RSSI -45~-77)
- [ ] 부하 시 전압이 +0.15V 오르는 현상: GND 배선 전압차 추정 → 멀티미터로 A 노드 실측 비교

## ⚡ 측정 후 값 확정 (`Config.h`)
- [ ] 최대 부하 시 12V 전압 → `VOLT_GOOD` / `VOLT_LOW` / `VOLT_FAULT`
- [ ] 센싱 전압 실측과 `/api/status`의 `supply_v` 비교 → `DIVIDER_RATIO` 보정
- [ ] 팬 실측 전류, 저속 기동 한계 → `MIN_FAN_DUTY`
- [ ] 조명 70%에서 총 전류가 1.2A 이하인지 → `MAX_LIGHT_DUTY`

## 🏠 SmartThings 연동
- [ ] SmartThings 앱에서 팬, LED, 프린터(MQTT로 가능한 동작)를 조작
  - 대상: 팬 ON/OFF·세기, 조명 ON/OFF·밝기, 수동 환기, 프린터 챔버 조명, 상태 표시(출력 상태, 노즐/베드 온도)
  - 후보 방식:
    - **Edge Driver (권장 검토)**: 허브에서 Lua 드라이버가 LAN으로 `/api/*`를 직접 호출합니다. 클라우드를 거치지 않지만 SmartThings 허브가 필요합니다.
    - Schema Connector: 클라우드에서 SmartThings와 장치를 연결합니다. 장치를 외부에 노출해야 해서 부담이 큽니다.
    - Home Assistant를 중계로 사용: 이미 HA가 있을 때만 해당됩니다.
  - 펌웨어 쪽에서 필요할 수 있는 것: 팬·조명을 각각 ON/OFF하는 API, 상태 변화를 알려 주는 방식(폴링 또는 SSDP/mDNS로 장치 검색)

## 💭 선택
- [ ] `SECRET_API_TOKEN`을 `secrets.h`에 정의해 API POST 인증 활성화
- [ ] 웹 로그 HTML escape (현재는 내부 문자열만 출력)
