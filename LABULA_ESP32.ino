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

// ==================== MQ2 CALIBRATION VALUES ====================
#define RL_VALUE 5.0                    // Load resistance in kilo ohms
#define R0_VALUE 167.3449               // Calibrated R0 value in kilo ohms
#define GAS_RATIO_THRESHOLD 2.0         // RS/R0 ratio below this = gas detected
// When ratio < 2.0, gas is present. Lower ratio = higher concentration.

// ==================== CALIBRATION ====================
const int GAS_THRESHOLD = 1500;
const unsigned long SEND_INTERVAL = 5000;
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long RECONNECT_INTERVAL = 10000;

// ==================== GLOBAL OBJECTS ====================
Servo ventServo;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ==================== STATE VARIABLES ====================
int gasValue = 0;
float rsRatio = 9.83;
bool manualOverride = false;
bool lastSwitchReading = false;
bool remoteManualOverride = false;
bool remoteFanOn = false;
bool remoteVentOpen = false;
bool firebaseConnected = false;
String currentStatus = "NORMAL";
unsigned long lastSendTime = 0;
unsigned long lastDebounceTime = 0;
unsigned long lastReconnectAttempt = 0;

// ==================== MQ2 HELPERS ====================
float readMQ2Resistance() {
  int adcValue = analogRead(MQ2_PIN);
  float voltage = adcValue * (3.3 / 4095.0);
  
  if (voltage <= 0.01) voltage = 0.01;
  
  float RS = (3.3 - voltage) / voltage * RL_VALUE;
  return RS;
}

float readGasRatio() {
  float RS = readMQ2Resistance();
  return RS / R0_VALUE;
}

// ==================== OUTPUT CONTROL ====================
void applyOutputs(bool gasAlarm, bool localOverride, bool remoteOverride, bool remoteFan, bool remoteVent) {
  bool fanOn = remoteFan || gasAlarm || localOverride || remoteOverride;
  bool ventOpen = remoteVent || gasAlarm || localOverride || remoteOverride;

  digitalWrite(RELAY_PIN, fanOn ? LOW : HIGH);
  ventServo.write(ventOpen ? 180 : 0);
  digitalWrite(LED_PIN, (gasAlarm || localOverride || remoteOverride) ? HIGH : LOW);
  digitalWrite(BUZZER_PIN, (gasAlarm || localOverride || remoteOverride) ? HIGH : LOW);
}

// ==================== FIREBASE CONNECTION ====================
void connectFirebase() {
  if (firebaseConnected && Firebase.ready()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  Serial.print("🔄 Connecting to Firebase");
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_TOKEN;
  config.api_key = WEB_API_KEY;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);
  delay(3000);

  if (Firebase.ready()) {
    firebaseConnected = true;
    Serial.println(" ✅");
  } else {
    firebaseConnected = false;
    Serial.printf(" ❌ (%s)\n", fbdo.errorReason().c_str());
  }
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

  delay(2000);

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
  connectFirebase();

  Serial.println("✅ System Ready!\n");
  Serial.println("==================================");
  Serial.println("📊 Monitoring gas levels...\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  // Always read local sensors regardless of connectivity
  gasValue = analogRead(MQ2_PIN);
  rsRatio = readGasRatio();
  bool gasAlarm = (rsRatio < GAS_RATIO_THRESHOLD);

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

  // Try to reconnect Firebase every 10 seconds if disconnected
  if (!firebaseConnected && (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL)) {
    connectFirebase();
    lastReconnectAttempt = millis();
  }

  // If Firebase is not connected, skip network operations but keep local control working
  if (!firebaseConnected || !Firebase.ready()) {
    static unsigned long lastFailMsg = 0;
    if (millis() - lastFailMsg >= 5000) {
      Serial.println("⚠️ Firebase not connected, using local mode...");
      lastFailMsg = millis();
    }
    
    applyOutputs(gasAlarm, manualOverride, false, false, false);
    
    delay(100);
    return;
  }

  // Read remote settings from Firebase
  static unsigned long lastRemoteRead = 0;
  if (millis() - lastRemoteRead >= 2000) {
    if (Firebase.RTDB.get(&fbdo, String(BASE_PATH) + "/control/manualOverride")) {
      if (fbdo.dataType() == "boolean") {
        remoteManualOverride = fbdo.boolData();
      } else if (fbdo.dataType() == "string") {
        String value = fbdo.stringData();
        value.toLowerCase();
        remoteManualOverride = (value == "true" || value == "1");
      }
    } else {
      Serial.printf("⚠️ Read failed manualOverride: %s\n", fbdo.errorReason().c_str());
      firebaseConnected = false;
    }

    if (Firebase.RTDB.get(&fbdo, String(BASE_PATH) + "/fanOn")) {
      if (fbdo.dataType() == "boolean") {
        remoteFanOn = fbdo.boolData();
      } else if (fbdo.dataType() == "string") {
        String value = fbdo.stringData();
        value.toLowerCase();
        remoteFanOn = (value == "true" || value == "1");
      }
    } else {
      Serial.printf("⚠️ Read failed fanOn: %s\n", fbdo.errorReason().c_str());
      firebaseConnected = false;
    }

    if (Firebase.RTDB.get(&fbdo, String(BASE_PATH) + "/ventOpen")) {
      if (fbdo.dataType() == "boolean") {
        remoteVentOpen = fbdo.boolData();
      } else if (fbdo.dataType() == "string") {
        String value = fbdo.stringData();
        value.toLowerCase();
        remoteVentOpen = (value == "true" || value == "1");
      }
    } else {
      Serial.printf("⚠️ Read failed ventOpen: %s\n", fbdo.errorReason().c_str());
      firebaseConnected = false;
    }

    lastRemoteRead = millis();
  }

  bool anyOverride = manualOverride || remoteManualOverride;

  // Update status
  if (gasAlarm && !anyOverride) {
    currentStatus = "ALARM";
  } else if (anyOverride) {
    currentStatus = "MANUAL_OVERRIDE";
  } else {
    currentStatus = "NORMAL";
  }

  // Apply outputs (remote fan/vent control works)
  applyOutputs(gasAlarm, manualOverride, remoteManualOverride, remoteFanOn, remoteVentOpen);

  // Send data to Firebase periodically
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    unsigned long startSend = millis();
    bool ok = true;

    ok &= Firebase.RTDB.setInt(&fbdo, String(BASE_PATH) + "/gasLevel", gasValue);
    delay(200);
    
    ok &= Firebase.RTDB.setString(&fbdo, String(BASE_PATH) + "/status", currentStatus);
    delay(200);
    
    ok &= Firebase.RTDB.setString(&fbdo, String(BASE_PATH) + "/control/switchState", manualOverride ? "PRESSED" : "RELEASED");
    delay(200);

    bool fanState = (digitalRead(RELAY_PIN) == LOW);
    bool ventState = (ventServo.read() > 90);

    ok &= Firebase.RTDB.setBool(&fbdo, String(BASE_PATH) + "/fanOn", fanState);
    delay(200);
    
    ok &= Firebase.RTDB.setBool(&fbdo, String(BASE_PATH) + "/ventOpen", ventState);

    if (ok) {
      lastSendTime = millis();
      Serial.printf("📤 Sent to Firebase in %lums\n", millis() - startSend);
    } else {
      Serial.println("⚠️ Firebase write failed");
      firebaseConnected = false;
    }
  }

  // Debug output
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 3000) {
    Serial.printf("Gas ADC: %d | RS/R0: %.2f | Status: %s | Switch: %s | Remote: %s | Fan: %s | Vent: %s | rFan: %s | rVent: %s | Firebase: %s\n",
      gasValue,
      rsRatio,
      currentStatus.c_str(),
      manualOverride ? "PRESSED" : "RELEASED",
      remoteManualOverride ? "ON" : "OFF",
      digitalRead(RELAY_PIN) == LOW ? "ON" : "OFF",
      ventServo.read() > 90 ? "OPEN" : "CLOSED",
      remoteFanOn ? "ON" : "OFF",
      remoteVentOpen ? "OPEN" : "CLOSED",
      firebaseConnected ? "OK" : "FAIL"
    );
    lastDebug = millis();
  }

  delay(100);
}
