/*
  ESP8266 (Wemos D1 R1) Bambu Lab X1C Auto Exhaust System

  기능:
  - WiFi (호환성 강화 모드) 및 MQTTS 연결
  - 뱀부랩 X1C 상태 모니터링 (JSON Filter를 이용한 메모리 최적화)
  - 출력이 시작되면(RUNNING) 팬/LED ON
  - 출력이 끝나면 지연 시간 후 OFF
*/

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

// ==========================================
// [설정 구역] 사용자 환경에 맞게 수정하세요
// ==========================================
const char *target_ssid = "TUF-AX5400-D510";
const char *target_password = "YOUR_WIFI_PASSWORD";

const char *mqtt_server = "192.168.50.209";
const int mqtt_port = 8883;
const char *mqtt_user = "bblp";
const char *mqtt_pass = "YOUR_ACCESS_CODE";
const char *printer_serial = "YOUR_PRINTER_SN";

// 핀 설정 (Wemos D1 R1 기준)
const int RELAY_PIN = D5;        // 배기 팬 제어
const int LED_PIN = D6;          // 조명 LED (PWM 지원)
const int FAN_OFF_DELAY = 10000; // 10초 지연
// ==========================================

WiFiClientSecure askClient;
PubSubClient client(askClient);

// 메모리 절약을 위한 JSON 필터 설정
StaticJsonDocument<200> filter;
void setupFilter() {
  filter["print"]["gcode_state"] = true;
  filter["print"]["nozzle_temper"] = true;
  filter["print"]["bed_temper"] = true;
}

// 상태 관리 변수
bool isPrinting = false;
unsigned long finishTime = 0;
bool waitingTurnOff = false;

void setup_wifi() {
  delay(10);
  Serial.println("\nWiFi Connecting (Enhanced Mode)...");

  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);

  WiFi.begin(target_ssid, target_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void updateActuators(bool state) {
  if (state) {
    digitalWrite(RELAY_PIN, HIGH);
    analogWrite(LED_PIN, 1023); // 최대 밝기 (0~1023)
    Serial.println(">>> [SYSTEM] Fan & LED ON <<<");
  } else {
    digitalWrite(RELAY_PIN, LOW);
    analogWrite(LED_PIN, 0);
    Serial.println(">>> [SYSTEM] Fan & LED OFF <<<");
  }
}

void callback(char *topic, byte *payload, unsigned int length) {
  // 힙 메모리를 사용하되, 파편화를 방지하기 위해 768바이트로 최적화
  DynamicJsonDocument doc(768);

  // 파싱 전 시스템에 숨쉴 틈을 줌 (WDT 리셋 방지)
  yield();
  DeserializationError error = deserializeJson(
      doc, payload, length, DeserializationOption::Filter(filter));
  yield();

  if (error) {
    Serial.print("JSON Filtered Parse Error: ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject printData = doc["print"];
  const char *state = printData["gcode_state"] | "UNKNOWN";
  float nTemp = printData["nozzle_temper"] | 0.0;
  float bTemp = printData["bed_temper"] | 0.0;

  Serial.printf("[Status] %s | N:%.1f B:%.1f\n", state, nTemp, bTemp);

  if (strcmp(state, "RUNNING") == 0 || strcmp(state, "PAUSE") == 0) {
    if (!isPrinting) {
      isPrinting = true;
      waitingTurnOff = false;
      updateActuators(true);
    }
    waitingTurnOff = false;
  } else {
    if (isPrinting) {
      isPrinting = false;
      waitingTurnOff = true;
      finishTime = millis();
      Serial.println(">>> Print finished. Shutdown timer started.");
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    int currentState = client.state();
    if (currentState != 0 && currentState != -1) {
      Serial.print("\nDisconnected! Last state: ");
      Serial.println(currentState);
    }

    Serial.print("MQTT Connecting (FreeHeap:");
    Serial.print(ESP.getFreeHeap());
    Serial.print(")...");

    String clientId = "ESP8266-AUX-";
    clientId += String(random(0xffff), HEX);

    askClient.setInsecure();
    // [생존을 위한 축소] 16KB는 너무 큽니다. 데이터가 12.2KB이므로
    // 14KB(14336)로 타협하여 램 2KB를 확보합니다.
    askClient.setBufferSizes(14336, 512);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("Success.");
      char topic[64];
      sprintf(topic, "device/%s/report", printer_serial);
      client.subscribe(topic);
      // 구독 직후 데이터 처리를 위해 짧게 대기
      delay(500);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
    yield();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  updateActuators(false);

  setupFilter();
  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // [생존을 위한 축소] MQTT 버퍼도 14KB(14336)로 맞춥니다. 총 4KB의 여유 램이
  // 생깁니다.
  if (!client.setBufferSize(14336)) {
    Serial.println("!!! CRITICAL: RAM SHORTAGE FOR 14KB BUFFER !!!");
  }
}

void loop() {
  if (!client.connected())
    reconnect();
  client.loop();

  // 시스템 백그라운드 작업(WiFi/TCP)을 위해 명시적으로 양보
  yield();

  if (waitingTurnOff && (millis() - finishTime > FAN_OFF_DELAY)) {
    waitingTurnOff = false;
    updateActuators(false);
  }

  // 너무 빠른 루프 방지 및 배경 작업 허용
  delay(1);
}
