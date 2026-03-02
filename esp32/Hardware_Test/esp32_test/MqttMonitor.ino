/*
  ESP32-C6 Bambu Lab X1C MQTT Monitor

  이 코드는 다음 기능을 수행합니다:
  1. WiFi 연결
  2. MQTTS(SSL)로 프린터 브로커(8883 포트)에 접속 (인증서 검증 무시)
  3. 상태 메시지를 수신하여 JSON 파싱
  4. 주요 정보를 시리얼 모니터에 출력 (챔버/노즐/베드 온도, 상태, 남은 시간,
  레이어 정보)

  [필수 라이브러리 설치 필요]
  아두이노 라이브러리 매니저에서 다음을 검색해서 설치하세요:
  1. "PubSubClient" by Nick O'Leary
  2. "ArduinoJson" by Benoit Blanchon (버전 6 이상 권장)
*/

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ==========================================
// [설정 구역] 사용자 환경에 맞게 수정하세요
// ==========================================
const char *target_ssid = "YOUR_WIFI_SSID";         // 와이파이 이름
const char *target_password = "YOUR_WIFI_PASSWORD"; // 와이파이 비밀번호

const char *mqtt_server = "192.168.50.209"; // 프린터 IP 주소 (ui.txt)
const int mqtt_port = 8883;                 // MQTTS 포트
const char *mqtt_user = "bblp";             // 고정값
const char *mqtt_pass = "YOUR_ACCESS_CODE"; // 프린터 LAN Access Code (8자리)

const char *printer_serial = "YOUR_PRINTER_SN"; // ui.txt에 있던 시리얼 번호
// ==========================================

// MQTT 토픽 생성용 버퍼
char mqtt_topic_report[64];

WiFiClientSecure askClient;
PubSubClient client(askClient);

// JSON 문서를 담을 버퍼 (매우 큰 JSON이므로 충분히 크게 잡아야 함. ESP32-C6는
// 램이 넉넉하므로 30KB 할당) 필요시 DynamicJsonDocument를 더 크게 늘리세요.
DynamicJsonDocument doc(30720);

unsigned long lastMsgTime = 0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(target_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(target_ssid, target_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT 메시지 수신 시 호출되는 콜백 함수
void callback(char *topic, byte *payload, unsigned int length) {
  // 간단한 디버그
  // Serial.print("Message arrived [");
  // Serial.print(topic);
  // Serial.print("] Length: ");
  // Serial.println(length);

  // payload를 바로 파싱 시도
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // --- 데이터 추출 및 출력 ---

  // JSON 구조가 "print" 객체 안에 정보가 있는지 확인
  if (!doc.containsKey("print")) {
    return; // "print" 키가 없는 메시지는 무시 (가끔 다른게 올 수 있음)
  }

  JsonObject printData = doc["print"];

  // 1. 주요 상태 (gcode_state)
  const char *state = printData["gcode_state"] | "UNKNOWN";

  // 2. 온도 정보 (Temperatures)
  // 챔버 온도는 "device" -> "ctc" -> "info" -> "temp" 구조일 수도 있고, 최상위
  // "bed_temper" 처럼 있을 수도 있음. report_idle.txt 분석 결과: "bed_temper":
  // 19 (베드) "nozzle_temper": 23 (노즐) "device" -> "ctc" -> "info" -> "temp":
  // 24 (챔버 추정, 혹은 별도 센서)

  float nozzleTemp = printData["nozzle_temper"] | 0.0;
  float bedTemp = printData["bed_temper"] | 0.0;
  // 챔버 온도는 보통 print.chamber_temper 로 오는데, 로그에는 없어서 ctc(혹은
  // info) 값 참조 시도 report_idle.txt 라인 183: "ctc": { "info": { "temp": 24
  // } ... }
  float chamberTemp = printData["device"]["ctc"]["info"]["temp"] | 0.0;

  // 3. 진행 정보 (Progress)
  int remainingTime = printData["mc_remaining_time"] | -1;
  int currentLayer = printData["layer_num"] | 0;
  int totalLayer = printData["total_layer_num"] | 0;
  int percent = printData["mc_percent"] | 0;

  // --- 시리얼 출력 ---
  Serial.println("\n============ Printer Status ============");
  Serial.printf("State      : %s\n", state);
  Serial.printf("Temp       : Nozzle[%.1f C]  Bed[%.1f C]  Chamber[%.1f C]\n",
                nozzleTemp, bedTemp, chamberTemp);

  if (strcmp(state, "RUNNING") == 0 || strcmp(state, "PAUSE") == 0) {
    Serial.printf("Progress   : %d %% (Time Left: %d min)\n", percent,
                  remainingTime);
    Serial.printf("Layer      : %d / %d\n", currentLayer, totalLayer);
  } else {
    Serial.println("Printer is IDLE or FINISHED.");
  }
  Serial.println("========================================");
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Client ID 생성 (중복 방지용 랜덤값)
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    // 중요: 인증서 검증 무시 설정 (Self-Signed Cert 때문에 필수)
    askClient.setInsecure();

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      // 토픽 구독
      // device/[SERIAL]/report
      sprintf(mqtt_topic_report, "device/%s/report", printer_serial);
      client.subscribe(mqtt_topic_report);
      Serial.printf("Subscribed to: %s\n", mqtt_topic_report);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();

  // MQTT 서버 설정
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // [중요 수정] 뱀부랩 메시지가 매우 크기 때문에(수 KB), 기본 버퍼(256B)로는
  // 수신이 불가능합니다. 버퍼 사이즈를 20KB(20480)로 늘려서 큰 JSON도 문제없이
  // 받도록 설정합니다.
  if (!client.setBufferSize(20480)) {
    Serial.println("Memory allocation failed for MQTT buffer!");
  } else {
    Serial.println("MQTT Buffer size increased to 20480 bytes.");
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
