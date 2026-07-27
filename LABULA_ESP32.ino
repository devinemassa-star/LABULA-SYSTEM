// ==================== LIBRARIES ====================
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>

// ==================== WIFI CREDENTIALS ====================
#define WIFI_SSID "Fundi@Work Admin"
#define WIFI_PASSWORD "Bots@admin25"

// ==================== FIREBASE CONFIGURATION ====================
#define FIREBASE_HOST "https://labula-5bb9c-default-rtdb.firebaseio.com/"
#define FIREBASE_TOKEN "57P1q4qN3rmehxkLwljEburFrEXbmGx1Q64GI9a7"
#define WEB_API_KEY "AIzaSyBkDTfedl5zozyudPtq_mMCfVlxXnMzcsY"

// ==================== PIN DEFINITIONS ====================
#define MQ2_PIN      34
#define RELAY_PIN    33
#define SERVO_PIN    21
#define BUZZER_PIN   26
#define LED_PIN      25
#define SWITCH_PIN   32

// ==================== CALIBRATION ====================
const int GAS_THRESHOLD = 1500;
const unsigned long SEND_INTERVAL = 2000;

// ==================== GLOBAL OBJECTS ====================
Servo ventServo;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ==================== GLOBAL VARIABLES ====================
int gasValue = 0;
bool alarmActive = false;
bool manualOverride = false;
bool lastManualState = false;
String currentStatus = "NORMAL";
unsigned long lastSendTime = 0;

// ==================== HELPERS ====================
bool readBoolFromFirebase(const String &path, bool defaultValue = false) {
  if (!Firebase.ready() || WiFi.status() != WL_CONNECTED) {
    return defaultValue;
  }

  if (!Firebase.RTDB.get(&fbdo, path)) {
    return defaultValue;
  }

  if (fbdo.dataType() == "boolean") {
    return fbdo.boolData();
  }

  if (fbdo.dataType() == "string") {
    String value = fbdo.stringData();
    value.toLowerCase();
    return value == "true" || value == "1";
  }

  return defaultValue;
}

void applyOutputs(bool gasAlarm, bool remoteManualOverride, bool remoteFanOn, bool remoteVentOpen) {
  bool fanShouldBeOn = gasAlarm || remoteManualOverride || remoteFanOn;
  bool ventShouldBeOpen = gasAlarm || remoteManualOverride || remoteVentOpen;

  // Relay is active LOW
  digitalWrite(RELAY_PIN, fanShouldBeOn ? LOW : HIGH);

  if (ventShouldBeOpen) {
    ventServo.write(180);
  } else {
    ventServo.write(0);
  }

  if (gasAlarm || remoteManualOverride) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n==================================");
  Serial.println("🔥 LABULA GAS MONITORING SYSTEM");
  Serial.println("==================================\n");

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  ventServo.setPeriodHertz(50);
  ventServo.attach(SERVO_PIN, 500, 2400);
  ventServo.write(0);

  Serial.print("📶 Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("📡 IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi connection failed!");
  }

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_TOKEN;
  config.api_key = WEB_API_KEY;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  Firebase.begin(&config, &auth);

  Serial.println("✅ System Ready!\n");
  Serial.println("==================================");
  Serial.println("📊 Monitoring gas levels...\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  gasValue = analogRead(MQ2_PIN);
  manualOverride = (digitalRead(SWITCH_PIN) == LOW);

  bool gasAlarm = (gasValue >= GAS_THRESHOLD);

  String basePath = "/labula_secret_2024/LABULA";
  bool remoteManualOverride = readBoolFromFirebase(basePath + "/control/manualOverride", false);
  bool remoteFanOn = readBoolFromFirebase(basePath + "/fanOn", false);
  bool remoteVentOpen = readBoolFromFirebase(basePath + "/ventOpen", false);

  bool alarmTriggered = gasAlarm || manualOverride || remoteManualOverride;

  if (alarmTriggered && !alarmActive) {
    alarmActive = true;
    currentStatus = "ALARM";
    Serial.println("🚨 ALARM ACTIVATED!");
  } else if (!alarmTriggered && alarmActive) {
    alarmActive = false;
    currentStatus = "NORMAL";
    Serial.println("✅ System back to NORMAL");
  } else if (alarmTriggered && alarmActive && (manualOverride || remoteManualOverride) && !lastManualState) {
    Serial.println("🔧 Manual Override ACTIVATED");
  }

  if (remoteManualOverride) {
    currentStatus = "MANUAL_OVERRIDE";
  } else if (gasAlarm) {
    currentStatus = "ALARM";
  } else {
    currentStatus = "NORMAL";
  }

  lastManualState = manualOverride || remoteManualOverride;

  applyOutputs(gasAlarm, remoteManualOverride, remoteFanOn, remoteVentOpen);

  if (millis() - lastSendTime >= SEND_INTERVAL) {
    sendDataToFirebase();
    lastSendTime = millis();
  }

  printDebugInfo();
  delay(100);
}

// ==================== SEND DATA TO FIREBASE ====================
void sendDataToFirebase() {
  if (!Firebase.ready() || WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Firebase/WiFi not ready!");
    return;
  }

  String basePath = "/labula_secret_2024/LABULA";

  Firebase.RTDB.setInt(&fbdo, basePath + "/gasLevel", gasValue);
  Firebase.RTDB.setString(&fbdo, basePath + "/status", currentStatus);
  Firebase.RTDB.setString(&fbdo, basePath + "/control/switchState", manualOverride ? "PRESSED" : "RELEASED");

  bool fanState = (digitalRead(RELAY_PIN) == LOW);
  Firebase.RTDB.setBool(&fbdo, basePath + "/fanOn", fanState);

  bool ventState = (ventServo.read() > 90);
  Firebase.RTDB.setBool(&fbdo, basePath + "/ventOpen", ventState);
}

// ==================== DEBUG ====================
void printDebugInfo() {
  Serial.print("Gas: ");
  Serial.print(gasValue);
  Serial.print(" | Status: ");
  Serial.print(currentStatus);
  Serial.print(" | Override: ");
  Serial.print(manualOverride ? "ON" : "OFF");
  Serial.print(" | Fan: ");
  Serial.print(digitalRead(RELAY_PIN) == LOW ? "ON" : "OFF");
  Serial.print(" | Vent: ");
  Serial.println(ventServo.read() > 90 ? "OPEN" : "CLOSED");
}
