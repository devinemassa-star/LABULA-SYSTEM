// ==================== LIBRARIES ====================
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include <time.h>

// ==================== WIFI CREDENTIALS ====================
#define WIFI_SSID "Fundi@Work Admin"
#define WIFI_PASSWORD "Bots@admin25"

// ==================== FIREBASE CONFIGURATION ====================
#define FIREBASE_HOST "labula-5bb9c-default-rtdb.firebaseio.com"
#define FIREBASE_TOKEN "57P1q4qN3rmehxkLwljEburFrEXbmGx1Q64GI9a7"
#define WEB_API_KEY "AIzaSyBkDTfedl5zozyudPtq_mMCfVlxXnMzcsY"

// ==================== PATHS ====================
#define BASE_PATH "/labula_secret_2024/LABULA"

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
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long FIREBASE_RETRY_DELAY = 5000;

// ==================== GLOBAL OBJECTS ====================
Servo ventServo;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ==================== STATE VARIABLES ====================
int gasValue = 0;
bool manualOverride = false;
bool lastSwitchReading = false;
bool remoteManualOverride = false;
bool firebaseConnected = false;
String currentStatus = "NORMAL";
unsigned long lastSendTime = 0;
unsigned long lastDebounceTime = 0;
unsigned long lastFirebaseCheck = 0;

// ==================== FIREBASE HELPERS ====================
bool ensureFirebaseReady() {
  if (firebaseConnected && Firebase.ready()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected");
    return false;
  }

  Serial.print("🔄 Connecting to Firebase");
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_TOKEN;
  config.api_key = WEB_API_KEY;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  Firebase.begin(&config, &auth);
  if (Firebase.ready()) {
    firebaseConnected = true;
    Serial.println(" ✅");
    return true;
  } else {
    Serial.printf(" ❌ (%s)\n", fbdo.errorReason().c_str());
    firebaseConnected = false;
    return false;
  }
}

bool readBoolFromFirebase(const String &path, bool defaultValue) {
  if (!ensureFirebaseReady()) {
    return defaultValue;
  }

  if (!Firebase.RTDB.get(&fbdo, path)) {
    Serial.printf("⚠️ Read failed %s: %s\n", path.c_str(), fbdo.errorReason().c_str());
    firebaseConnected = false;
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

bool writeToFirebase(const String &path, bool value) {
  if (!ensureFirebaseReady()) {
    return false;
  }

  if (!Firebase.RTDB.setBool(&fbdo, path, value)) {
    Serial.printf("⚠️ Write failed %s: %s\n", path.c_str(), fbdo.errorReason().c_str());
    firebaseConnected = false;
    return false;
  }
  return true;
}

bool writeToFirebase(const String &path, int value) {
  if (!ensureFirebaseReady()) {
    return false;
  }

  if (!Firebase.RTDB.setInt(&fbdo, path, value)) {
    Serial.printf("⚠️ Write failed %s: %s\n", path.c_str(), fbdo.errorReason().c_str());
    firebaseConnected = false;
    return false;
  }
  return true;
}

bool writeToFirebase(const String &path, const String &value) {
  if (!ensureFirebaseReady()) {
    return false;
  }

  if (!Firebase.RTDB.setString(&fbdo, path, value)) {
    Serial.printf("⚠️ Write failed %s: %s\n", path.c_str(), fbdo.errorReason().c_str());
    firebaseConnected = false;
    return false;
  }
  return true;
}

// ==================== OUTPUT CONTROL ====================
void applyOutputs(bool gasAlarm, bool localOverride, bool remoteOverride, bool remoteFan, bool remoteVent) {
  bool fanOn = gasAlarm || localOverride || remoteOverride || remoteFan;
  bool ventOpen = gasAlarm || localOverride || remoteOverride || remoteVent;

  digitalWrite(RELAY_PIN, fanOn ? LOW : HIGH);
  ventServo.write(ventOpen ? 180 : 0);
  digitalWrite(LED_PIN, (gasAlarm || localOverride || remoteOverride) ? HIGH : LOW);
  digitalWrite(BUZZER_PIN, (gasAlarm || localOverride || remoteOverride) ? HIGH : LOW);
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
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

  // ==================== WIFI CONNECTION ====================
  Serial.print("📶 Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ WiFi failed. Restarting...");
    ESP.restart();
  }

  Serial.println("\n✅ WiFi connected!");
  Serial.print("📡 IP: ");
  Serial.println(WiFi.localIP());

  // ==================== TIME SYNC ====================
  Serial.print("⏰ Syncing NTP");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  int syncAttempts = 0;
  while (now < 1000000000 && syncAttempts < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    syncAttempts++;
  }
  Serial.println("\n✅ Time synced!");

  // ==================== FIREBASE INIT ====================
  Serial.print("🔥 Connecting to Firebase");
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_TOKEN;
  config.api_key = WEB_API_KEY;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  Firebase.begin(&config, &auth);
  if (Firebase.ready()) {
    firebaseConnected = true;
    Serial.println(" ✅");
  } else {
    Serial.printf(" ❌ (%s)\n", fbdo.errorReason().c_str());
    Serial.println("⚠️ Will retry in loop...");
  }

  Serial.println("✅ System Ready!\n");
  Serial.println("==================================");
  Serial.println("📊 Monitoring gas levels...\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  // Always read local sensors regardless of connectivity
  gasValue = analogRead(MQ2_PIN);

  // Read physical switch with debouncing
  bool switchReading = (digitalRead(SWITCH_PIN) == LOW);
  if (switchReading != lastSwitchReading) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    manualOverride = switchReading;
  }
  lastSwitchReading = switchReading;

  // WiFi reconnection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("📶 WiFi lost, reconnecting...");
    WiFi.reconnect();
    delay(2000);
    firebaseConnected = false;
    return;
  }

  // Read remote settings from Firebase (with retry)
  static unsigned long lastRemoteRead = 0;
  if (millis() - lastRemoteRead >= 1000) {
    remoteManualOverride = readBoolFromFirebase(String(BASE_PATH) + "/control/manualOverride", false);
    lastRemoteRead = millis();
  }

  bool gasAlarm = (gasValue >= GAS_THRESHOLD);
  bool anyOverride = manualOverride || remoteManualOverride;

  // Update status
  if (gasAlarm && !anyOverride) {
    currentStatus = "ALARM";
  } else if (anyOverride) {
    currentStatus = "MANUAL_OVERRIDE";
  } else {
    currentStatus = "NORMAL";
  }

  // Apply outputs (local switch AND remote control both work)
  applyOutputs(gasAlarm, manualOverride, remoteManualOverride, false, false);

  // Send data to Firebase periodically
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    unsigned long startSend = millis();

    bool ok = true;
    ok &= writeToFirebase(String(BASE_PATH) + "/gasLevel", gasValue);
    delay(100);
    ok &= writeToFirebase(String(BASE_PATH) + "/status", currentStatus);
    delay(100);
    ok &= writeToFirebase(String(BASE_PATH) + "/control/switchState", manualOverride ? "PRESSED" : "RELEASED");
    delay(100);

    bool fanState = (digitalRead(RELAY_PIN) == LOW);
    bool ventState = (ventServo.read() > 90);

    ok &= writeToFirebase(String(BASE_PATH) + "/fanOn", fanState);
    delay(100);
    ok &= writeToFirebase(String(BASE_PATH) + "/ventOpen", ventState);

    if (ok) {
      lastSendTime = millis();
    } else {
      Serial.println("⚠️ Some Firebase writes failed, will retry next interval");
    }

    Serial.printf("📤 Sent to Firebase in %lums\n", millis() - startSend);
  }

  // Debug output
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 1000) {
    Serial.printf("Gas: %d | Status: %s | Switch: %s | Remote: %s | Fan: %s | Vent: %s | Firebase: %s\n",
      gasValue,
      currentStatus.c_str(),
      manualOverride ? "PRESSED" : "RELEASED",
      remoteManualOverride ? "ON" : "OFF",
      digitalRead(RELAY_PIN) == LOW ? "ON" : "OFF",
      ventServo.read() > 90 ? "OPEN" : "CLOSED",
      firebaseConnected ? "OK" : "FAIL"
    );
    lastDebug = millis();
  }

  delay(100);
}
