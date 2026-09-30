#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

// ---- Battery monitoring ----
#define BATTERY_PIN 7
// Board divider ratio for 2S battery on IO7
#define VOLTAGE_DIVIDER_RATIO 3.571
#define BATTERY_CAPACITY_MAH 4300 // Capacity in mAh (4300mAh for 2S setup)
#define MIN_VOLTAGE 6.4           // 0% cutoff voltage for 2S pack
#define MAX_VOLTAGE 8.4           // 100% full voltage for 2S pack

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float readBatteryPercentage() {
  uint32_t raw_mv = analogReadMilliVolts(BATTERY_PIN);
  float total_voltage = (raw_mv * VOLTAGE_DIVIDER_RATIO) / 1000.0;
  float pct = mapFloat(total_voltage, MIN_VOLTAGE, MAX_VOLTAGE, 0.0, 100.0);
  return constrain(pct, 0.0, 100.0);
}

// ---- Heartbeat ----
const unsigned long HEARTBEAT_INTERVAL = 30000; // 30s
unsigned long lastHeartbeatTime = 0;

// ---- PN532 RFID reader ----
#define PN532_SCK   12
#define PN532_MISO  13
#define PN532_MOSI  14
#define PN532_SS    8
#define PN532_RST   16

#define BUZZER_PIN  21

Adafruit_PN532 nfc(PN532_SS);

// RFID health via active ping, not firmware version -- duplicate/clone
// PN532 boards often report version 0 or garbage from getFirmwareVersion()
// even when fully functional. SAMConfig() is a real command/ACK exchange
// the reader must handle correctly to work at all, so it's a reliable
// clone-safe ping. Pinged every 10s from rfidTask (Core 0); loop()
// (Core 1) reads the result for the heartbeat.
volatile bool rfidPingOk = false;
unsigned long lastRfidPing = 0;
const unsigned long RFID_PING_INTERVAL = 10000; // 10s

// Dedup cache: prevents repeated triggers from the same physical tap.
const int DEDUP_SIZE = 10;
struct DedupEntry {
  String uid;
  unsigned long lastSeen;
};
DedupEntry dedupCache[DEDUP_SIZE];
int dedupIndex = 0;

bool isDuplicateTap(const String& uid) {
  for (int i = 0; i < DEDUP_SIZE; i++) {
    if (dedupCache[i].uid == uid && millis() - dedupCache[i].lastSeen < 30000) {
      return true;
    }
  }
  return false;
}
void updateDedup(const String& uid) {
  dedupCache[dedupIndex].uid = uid;
  dedupCache[dedupIndex].lastSeen = millis();
  dedupIndex = (dedupIndex + 1) % DEDUP_SIZE;
}

// FreeRTOS queue: the RFID polling task (Core 0) pushes UIDs here.
// loop() (Core 1) drains it. This keeps tap detection immune to
// blocking AT/HTTP calls in sendAT()/httpPostJson().
QueueHandle_t rfidQueue;
#define RFID_QUEUE_LEN 50
#define RFID_UID_MAXLEN 32
TaskHandle_t rfidTaskHandle = nullptr;

// Guards Serial writes so output from rfidTask (Core 0) can't
// interleave mid-line with output from loop()/setup() (Core 1).
SemaphoreHandle_t serialMutex = nullptr;

void safePrint(const String& s) {
  if (serialMutex && xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) {
    Serial.print(s);
    xSemaphoreGive(serialMutex);
  } else {
    Serial.print(s);
  }
}
void safePrintln(const String& s) {
  if (serialMutex && xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) {
    Serial.println(s);
    xSemaphoreGive(serialMutex);
  } else {
    Serial.println(s);
  }
}
void safePrintln() {
  safePrintln("");
}

// Push a scanned card UID through the same dedup/beep/queue pipeline.
void pushUid(const String& uid) {
  if (isDuplicateTap(uid)) {
    digitalWrite(BUZZER_PIN, HIGH); delay(80);
    digitalWrite(BUZZER_PIN, LOW);  delay(80);
    digitalWrite(BUZZER_PIN, HIGH); delay(80);
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  updateDedup(uid);
  digitalWrite(BUZZER_PIN, HIGH); delay(80);
  digitalWrite(BUZZER_PIN, LOW);

  safePrintln("Card Tap: " + uid);

  char payload[RFID_UID_MAXLEN];
  uid.toCharArray(payload, RFID_UID_MAXLEN);
  xQueueSend(rfidQueue, payload, 0);
}

// PN532 RFID reader
String uidToString(uint8_t *uid, uint8_t length) {
  String str = "";
  for (int i = 0; i < length; i++) {
    if (uid[i] < 0x10)
      str += "0";
    str += String(uid[i], HEX);
    if (i < length - 1)
      str += ":";
  }
  str.toUpperCase();
  return str;
}

void rfidTask(void* param) {
  for (;;) {
    if (millis() - lastRfidPing >= RFID_PING_INTERVAL) {
      lastRfidPing = millis();
      rfidPingOk = nfc.SAMConfig();
      if (!rfidPingOk) {
        safePrintln("⚠️ RFID ping FAILED");
      }
    }

    uint8_t uid[7];
    uint8_t uidLength;

    bool success = nfc.readPassiveTargetID(
                      PN532_MIFARE_ISO14443A,
                      uid,
                      &uidLength);

    if (success) {
      String cardUID = uidToString(uid, uidLength);
      pushUid(cardUID);
      vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

String serverHost_1 = "http://103.207.1.87:4016";    
String serverHost_2= "http://103.207.1.84:4016"; 
 
String trackingURL = "";
String stopsURL = "";
String gpsData = "";
float lastLat = 0.0;
float lastLon = 0.0;
float currentSpeed = 0.0;
float lastUploadedLat = 0.0;
float lastUploadedLon = 0.0;
bool internetReady = false;
bool uploadInProgress = false;
unsigned long lastUploadTime = 0;
unsigned long lastValidGpsTime = 0;
unsigned long lastGpsUpdateTime = 0;
// const char* busID = "31";
const char* deviceID = "ESP32S3-5";

int uploadFailCount = 0;
int loopCounter = 0;
struct StudentRecord {
  String cardUID;
  unsigned long firstScanTime;
  unsigned long lastScanTime;
  String status;
  bool geofenceProcessed;
};

const int MAX_STUDENTS = 100;
StudentRecord students[MAX_STUDENTS];
int studentCount = 0;
unsigned long SCAN_TIMEOUT = 30000;

struct PendingScan {
  String cardUID;
  unsigned long scanTime;
  String status;
};

const int MAX_PENDING = 500;
PendingScan pendingScans[MAX_PENDING];
int pendingCount = 0;

const int MAX_STOPS = 30;
struct StopInfo {
  String stop_name;
  float latitude;
  float longitude;
};

StopInfo stops[MAX_STOPS];
int stopCount = 0;
unsigned long lastStopFetchTime = 0;
const unsigned long STOP_FETCH_INTERVAL = 60000; // 1 minute

void clearSerialBuffer() {
    unsigned long start = millis();
    while (millis() - start < 300) {
        while (Serial2.available()) {
            Serial2.read();
        }
    }
}
String describeATCommand(const String& cmd) {
  if (cmd.startsWith("AT+CPIN?")) return "Check SIM card status";
  if (cmd.startsWith("AT+CSQ")) return "Check signal strength";
  if (cmd.startsWith("AT+CREG?")) return "Check network registration";
  if (cmd.startsWith("AT+CGATT")) return "Attach/detach from cellular network";
  if (cmd.startsWith("AT+NETOPEN")) return "Open network data connection";
  if (cmd.startsWith("AT+NETCLOSE")) return "Close network data connection";
  if (cmd.startsWith("AT+HTTPINIT")) return "Initialize HTTP service";
  if (cmd.startsWith("AT+HTTPPARA")) return "Set HTTP parameter";
  if (cmd.startsWith("AT+HTTPDATA")) return "Set HTTP POST data payload";
  if (cmd.startsWith("AT+HTTPACTION=0")) return "Send HTTP GET request";
  if (cmd.startsWith("AT+HTTPACTION=1")) return "Send HTTP POST request";
  if (cmd.startsWith("AT+HTTPTERM")) return "Terminate HTTP service";
  if (cmd.startsWith("AT+HTTPREAD")) return "Read HTTP response data";
  if (cmd.startsWith("AT+CGPSINFO")) return "Get GPS position from NMEA";
  if (cmd.startsWith("AT+CGPS=")) return "Enable/disable GPS";
  if (cmd.startsWith("AT+CGNSSPWR")) return "Set GNSS power mode";
  if (cmd.startsWith("AT+CGNSSLOADAZ")) return "Load GNSS assistance data";
  if (cmd.startsWith("AT+CFUN")) return "Set phone functionality level";
  if (cmd.startsWith("AT+CGATT=0")) return "Detach from cellular network";
  if (cmd.startsWith("AT+CGATT=1")) return "Attach to cellular network";
  if (cmd.startsWith("AT+CGNSSPWR=0")) return "Power off GNSS";
  if (cmd.startsWith("AT+CGNSSPWR=1")) return "Power on GNSS";
  if (cmd.startsWith("AT+CGPS=1")) return "Enable GPS positioning";
  if (cmd.startsWith("AT+CGPS=0")) return "Disable GPS positioning";
  if (cmd.startsWith("AT+CGPIAUTH")) return "Set PDP authentication";
  if (cmd.startsWith("AT+CGDCONT")) return "Define PDP context";
  if (cmd.startsWith("AT+CIPSHUT")) return "Shut down TCP/IP connection";
  if (cmd.startsWith("AT+ACSTATUS")) return "Get AC adapter status";
  return "";
}

String sendAT(String cmd, int waitTime = 3000) {
  String response = "";
  clearSerialBuffer();
  Serial2.println(cmd);
  unsigned long start = millis();
  while (millis() - start < waitTime) {
    while (Serial2.available()) {
      char c = Serial2.read();
      response += c;
    }
    
    if (response.indexOf("OK") != -1 ||
        response.indexOf("ERROR") != -1 ||
        response.indexOf("DOWNLOAD") != -1 ||
        response.indexOf("+HTTPACTION:") != -1) {
      break;
    }
  }
String desc = describeATCommand(cmd);
  if (desc.length() > 0) {
    safePrintln(">> " + cmd + " (" + desc + ") [RESP]: " + response);
  } else {
    safePrintln(">> " + cmd + " [RESP]: " + response);
  }
  return response;
  }
String escapeJsonString(const String& input) {
  String escaped = "";
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\b') {
      escaped += "\\b";
    } else if (c == '\f') {
      escaped += "\\f";
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else if (c == '\t') {
      escaped += "\\t";
    } else {
      escaped += c;
    }
  }
  return escaped;
}

int findStudentIndexByUID(const String& cardUID) {
  for (int i = 0; i < studentCount; i++) {
    if (students[i].cardUID == cardUID) {
      return i;
    }
  }
  return -1;
}

String buildStudentsJson() {
  String payload = "[";
  for (int i = 0; i < studentCount; i++) {
    if (i > 0) payload += ",";
    payload += "{";
    payload += "\"card_uid\":\"" + escapeJsonString(students[i].cardUID) + "\",";
    payload += "\"status\":\"" + escapeJsonString(students[i].status) + "\",";
    payload += "\"scan_time\":" + String(students[i].lastScanTime);
    payload += "}";
  }
  payload += "]";
  return payload;
}

float toRadians(float degrees) {
  return degrees * PI / 180.0;
}

float calculateDistanceKm(float lat1, float lon1, float lat2, float lon2) {
  float dLat = toRadians(lat2 - lat1);
  float dLon = toRadians(lon2 - lon1);
  float a = sin(dLat / 2) * sin(dLat / 2) +
            cos(toRadians(lat1)) * cos(toRadians(lat2)) *
            sin(dLon / 2) * sin(dLon / 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return 6371.0 * c;
}

String getNextStopName(float latitude, float longitude) {
  if (stopCount == 0) {
    return "";
  }
  float bestDistance = 1e9;
  int bestIndex = -1;
  for (int i = 0; i < stopCount; i++) {
    float distanceKm = calculateDistanceKm(latitude, longitude, stops[i].latitude, stops[i].longitude);
    if (distanceKm < bestDistance) {
      bestDistance = distanceKm;
      bestIndex = i;
    }
  }
  if (bestIndex >= 0) {
    return stops[bestIndex].stop_name;
  }
  return "";
}

bool fetchStopsFromServer() {
  if (millis() - lastStopFetchTime < STOP_FETCH_INTERVAL) {
    return true;
  }
  lastStopFetchTime = millis();

  safePrintln("🔄 Fetching route stops from server");
  String url = stopsURL;
  sendAT("AT+HTTPTERM", 3000);
  String httpInit = sendAT("AT+HTTPINIT", 5000);
  if (httpInit.indexOf("OK") == -1) {
    safePrintln("❌ HTTPINIT failed for stop fetch");
    return false;
  }

  sendAT("AT+HTTPPARA=\"CID\",1", 3000);
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 5000);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 3000);

  String actionResp = sendAT("AT+HTTPACTION=0", 15000);
  if (actionResp.indexOf(",200,") == -1) {
    safePrintln("❌ Stop fetch failed");
    sendAT("AT+HTTPTERM", 3000);
    return false;
  }

  String readResp = sendAT("AT+HTTPREAD", 15000);
  int okIndex = readResp.lastIndexOf("OK");
  if (okIndex != -1) {
    readResp = readResp.substring(0, okIndex);
  }

  sendAT("AT+HTTPTERM", 3000);

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, readResp);
  if (err) {
    safePrintln("❌ JSON parse failed for stops");
    return false;
  }

  stopCount = 0;
  for (JsonObject point : doc.as<JsonArray>()) {
    if (stopCount >= MAX_STOPS) {
      break;
    }
    stops[stopCount].stop_name = point["stop_name"].as<String>();
    stops[stopCount].latitude = point["latitude"].as<float>();
    stops[stopCount].longitude = point["longitude"].as<float>();
    stopCount++;
  }

  safePrintln("✅ Loaded " + String(stopCount) + " stops");
  return true;
}

String determineStudentStatus(String cardUID) {
  unsigned long currentTime = millis();
  int studentIndex = -1;

  for (int i = 0; i < studentCount; i++) {
    if (students[i].cardUID == cardUID) {
      studentIndex = i;
      break;
    }
  }

  if (studentIndex == -1) {
    if (studentCount < MAX_STUDENTS) {
      students[studentCount].cardUID = cardUID;
      students[studentCount].firstScanTime = currentTime;
      students[studentCount].lastScanTime = currentTime;
      students[studentCount].status = "BOARDED";
      students[studentCount].geofenceProcessed = false;
      safePrint("🔵 NEW - BOARDED | Index: ");
      safePrintln(String(studentCount));
      studentCount++;
      return "BOARDED";
    }
    return "ERROR";
  }

  StudentRecord& student = students[studentIndex];
  unsigned long timeSinceLastScan = currentTime - student.lastScanTime;

  if (timeSinceLastScan < SCAN_TIMEOUT) {
    safePrint("⏱ IGNORE (within ");
    safePrint(String(SCAN_TIMEOUT / 1000));
    safePrint("s) | Status: ");
    safePrintln(student.status);
    student.lastScanTime = currentTime;
    return student.status;
  }

  student.lastScanTime = currentTime;
  safePrint("✅ TAP | Prev: ");
  safePrint(student.status);
  safePrint(" -> ");

  if (student.status == "Reached School") {
    student.status = "BOARDED";
    student.geofenceProcessed = true;
  } else if (student.status == "BOARDED") {
    student.status = "Reached Home";
    student.geofenceProcessed = false;
  } else {
    student.status = "BOARDED";
    student.geofenceProcessed = false;
  }

  safePrintln(student.status);
  return student.status;
}

float calculateSpeed(float lat1, float lon1, float lat2, float lon2, unsigned long timeDiffMs) {
  if (timeDiffMs == 0) return 0.0;

  float R = 6371.0;
  float dLat = (lat2 - lat1) * PI / 180.0;
  float dLon = (lon2 - lon1) * PI / 180.0;

  float a = sin(dLat / 2.0) * sin(dLat / 2.0) +
            cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
            sin(dLon / 2.0) * sin(dLon / 2.0);
  float c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  float distance = R * c;

  float timeHours = timeDiffMs / 3600000.0;
  float speed = distance / timeHours;
  return speed;
}

bool recoverInternet() {
  safePrintln("🔄 RECOVERING INTERNET");
  sendAT("AT+HTTPTERM", 3000);
  sendAT("AT+NETCLOSE", 5000);
  delay(2000);
  sendAT("AT+CGATT=0", 5000);
  delay(3000);
  sendAT("AT+CGATT=1", 10000);
  delay(3000);
  String netOpen = sendAT("AT+NETOPEN", 15000);
  if (netOpen.indexOf("OK") == -1 && netOpen.indexOf("+NETOPEN: 0") == -1) {
    safePrintln("❌ NETOPEN FAILED");
    return false;
  }
  delay(3000);
  sendAT("AT+HTTPTERM", 3000);
  String httpInit = sendAT("AT+HTTPINIT", 5000);
  if (httpInit.indexOf("OK") == -1) {
    safePrintln("❌ HTTPINIT FAILED");
    return false;
  }
  sendAT("AT+HTTPPARA=\"CID\",1", 3000);
  internetReady = true;
  safePrintln("✅ INTERNET RESTORED");
  sendAT("AT+CGNSSPWR=0", 3000);
  delay(2000);
  sendAT("AT+CGNSSPWR=1", 3000);
  delay(2000);
  sendAT("AT+CGPS=1", 3000);
  delay(3000);
  return true;
}

bool checkAndRecoverNetwork() {
  String cpinCheck = sendAT("AT+CPIN?", 2000);
  if (cpinCheck.indexOf("READY") == -1) {
    safePrintln("⚠️ SIM FAILURE DETECTED");
    sendAT("AT+CFUN=0", 5000);
    delay(3000);
    sendAT("AT+CFUN=1", 8000);
    delay(5000);
    cpinCheck = sendAT("AT+CPIN?", 3000);
    if (cpinCheck.indexOf("READY") != -1) {
      safePrintln("✅ SIM REINSERTED SUCCESSFULLY");
      recoverInternet();
      return true;
    }
    safePrintln("❌ SIM STILL NOT DETECTED");
 return false;
  }
  String csqCheck = sendAT("AT+CSQ", 1000);
  int csqIdx = csqCheck.indexOf("+CSQ:");
  if (csqIdx != -1) {
      String rssiRaw =
      csqCheck.substring(
        csqIdx + 5,
        csqCheck.indexOf(",", csqIdx)
      );
  rssiRaw.trim();
    int rssiVal = rssiRaw.toInt();
    String signalStr;
    if (rssiVal == 99) {
      signalStr = "NO SIGNAL";
    }
    else if (rssiVal < 10) {
      signalStr = "POOR";
    }
    else if (rssiVal < 15) {
      signalStr = "FAIR";
    }
    else {
      signalStr = "GOOD";
    }
    safePrintln(
      "📶 Signal: " +
      signalStr +
      " (" + rssiRaw + ")"
    );
  }
  String cregCheck = sendAT("AT+CREG?", 2000);
  if (cregCheck.indexOf(",1") == -1 && cregCheck.indexOf(",5") == -1) {
    safePrintln("📡 NETWORK LOST");
    recoverInternet();
    delay(3000);
    cregCheck = sendAT("AT+CREG?", 2000);
    if (cregCheck.indexOf(",1") == -1 &&
        cregCheck.indexOf(",5") == -1) {
   return false;
    }
  }
 return true;
}
bool httpPostJson(String url, String json) {
  sendAT("AT+HTTPTERM", 2000);
  String initResp = sendAT("AT+HTTPINIT", 5000);
  if (initResp.indexOf("OK") == -1) {
    safePrintln("❌ HTTPINIT FAILED for " + url);
    return false;
  }

  sendAT("AT+HTTPPARA=\"CID\",1", 3000);
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 5000);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 3000);

  String dataCmd = "AT+HTTPDATA=" + String(json.length()) + ",10000";
  String dataResp = sendAT(dataCmd, 5000);
  if (dataResp.indexOf("DOWNLOAD") == -1) {
    safePrintln("❌ HTTPDATA FAILED");
    sendAT("AT+HTTPTERM", 3000);
    return false;
  }

  delay(200);
  Serial2.print(json);
  delay(300);
  Serial2.println();

  safePrintln(">> AT+HTTPACTION=1");
  Serial2.println("AT+HTTPACTION=1");

  String actionResult = "";
  bool success = false;
  unsigned long startWait = millis();
  while (millis() - startWait < 10000) {
    while (Serial2.available()) {
      char c = Serial2.read();
      actionResult += c;
      if (actionResult.indexOf(",200,") != -1 || actionResult.indexOf(",201,") != -1) {
        success = true;
        break;
      }
      if (actionResult.indexOf(",400,") != -1 || actionResult.indexOf(",500,") != -1) {
        break;
      }
    }
    if (success) break;
  }

  safePrintln("[ASYNC ACTION RESP]: " + actionResult);
  sendAT("AT+HTTPTERM", 3000);
  return success;
}

bool uploadTodatabase(float latitude, float longitude, String cardUID, String studentStatus) {
  if (uploadInProgress) {
    safePrintln("⚠️ Previous upload still active");
    return false;
  }
  uploadInProgress = true;

  String json = "{";
  json += "\"device_id\":\"" + escapeJsonString(String(deviceID)) + "\",";
  // json += "\"bus_number\":\"" + escapeJsonString(String(busID)) + "\",";
  json += "\"lat\":" + String(latitude, 6) + ",";
  json += "\"lon\":" + String(longitude, 6) + ",";
  json += "\"speed\":" + String(currentSpeed > 10.0 ? currentSpeed : 0.0, 2) + ",";
  json += "\"card_uid\":\"" + escapeJsonString(cardUID) + "\",";
  json += "\"status\":\"" + escapeJsonString(studentStatus) + "\"";
  json += "}";

  safePrintln("================================");
  safePrintln("Sending JSON to both servers:");
  safePrintln(json);

  bool ok1 = httpPostJson(serverHost_1 + "/tracking", json);
  if (ok1) safePrintln("✅ Server 1 SUCCESS");
  else safePrintln("❌ Server 1 FAILED");

  bool ok2 = httpPostJson(serverHost_2 + "/tracking", json);
  if (ok2) safePrintln("✅ Server 2 SUCCESS");
  else safePrintln("❌ Server 2 FAILED");

  bool anySuccess = ok1 || ok2;

  if (anySuccess) {
    safePrintln("✅ MSSQL SERVER SUCCESS");
    uploadFailCount = 0;
    lastUploadTime = millis();
    lastUploadedLat = latitude;
    lastUploadedLon = longitude;
    clearSerialBuffer();
  } else {
    safePrintln("❌ MSSQL SERVER FAILED");
  }

  uploadInProgress = false;
  return anySuccess;
}

bool sendHeartbeat() {
  float chargePct = readBatteryPercentage();
  bool rfidOk = rfidPingOk;

  String json = "{";
  json += "\"device_id\":\"" + escapeJsonString(String(deviceID)) + "\",";
  json += "\"esp32_status\":\"Working\",";
  json += "\"charge\":" + String(chargePct, 1) + ",";
  json += "\"rfid_status\":\"" + String(rfidOk ? "Working" : "Not Working") + "\"";
  json += "}";

  safePrintln("💓 Heartbeat: " + json);

  bool ok1 = httpPostJson(serverHost_1 + "/heartbeat", json);
  bool ok2 = httpPostJson(serverHost_2 + "/heartbeat", json);

  if (ok1 || ok2) {
    safePrintln("✅ Heartbeat sent");
  } else {
    safePrintln("❌ Heartbeat FAILED (both servers)");
  }
  return ok1 || ok2;
}

void addPendingScan(String cardUID, String status) {
  if (pendingCount < MAX_PENDING) {
    pendingScans[pendingCount].cardUID = cardUID;
    pendingScans[pendingCount].scanTime = millis();
    pendingScans[pendingCount].status = status;
    pendingCount++;
    safePrintln("📝 Queued scan #" + String(pendingCount) + " | UID: " + cardUID);
  } else {
    for (int i = 1; i < MAX_PENDING; i++) {
      pendingScans[i - 1] = pendingScans[i];
    }
    pendingScans[MAX_PENDING - 1].cardUID = cardUID;
    pendingScans[MAX_PENDING - 1].scanTime = millis();
    pendingScans[MAX_PENDING - 1].status = status;
    safePrintln("📝 Queue full - replaced oldest | UID: " + cardUID);
  }
}

void drainPendingScans(float latitude, float longitude) {
  int sent = 0;
  while (sent < pendingCount) {
    bool ok = uploadTodatabase(latitude, longitude,
      pendingScans[sent].cardUID, pendingScans[sent].status);
    if (ok) {
      sent++;
    } else {
      safePrintln("❌ Drain stopped at " + String(sent) + "/" + String(pendingCount));
      break;
    }
  }
  if (sent > 0) {
    int remaining = pendingCount - sent;
    for (int i = 0; i < remaining; i++) {
      pendingScans[i] = pendingScans[i + sent];
    }
    pendingCount = remaining;
    safePrintln("✅ Drained " + String(sent) + " scans | " + String(pendingCount) + " remaining");
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(PN532_RST, OUTPUT);
  digitalWrite(PN532_RST, HIGH);

  SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  nfc.begin();

  uint32_t pn532Version = nfc.getFirmwareVersion();
  if (!pn532Version) {
    // Note: clone/duplicate boards may legitimately report 0 here even
    // though they work fine, so this print is informational only --
    // it does NOT gate RFID health. The active SAMConfig() ping does.
    safePrintln("⚠️ PN532 firmware version unreadable (may be a clone board)");
  } else {
    safePrintln("✅ PN532 Ready");
  }
  rfidPingOk = nfc.SAMConfig();
  lastRfidPing = millis();

  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

  serialMutex = xSemaphoreCreateMutex();

  rfidQueue = xQueueCreate(RFID_QUEUE_LEN, RFID_UID_MAXLEN);
  // Pin the RFID polling task to Core 0. The rest of setup()/loop() (modem,
  // GPS, HTTP) runs on Core 1, where Arduino's own loopTask already lives.
  xTaskCreatePinnedToCore(rfidTask, "rfidTask", 4096, nullptr, 1, &rfidTaskHandle, 0);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(8000);
  safePrintln("🚀 BUS TRACKER STARTING");
  sendAT("AT", 2000);
  sendAT("AT+CPIN?", 2000);
  sendAT("AT+CGATT=1", 5000);
  sendAT("AT+NETOPEN", 10000);
  sendAT("AT+CGNSSPWR=1", 3000);
  sendAT("AT+CGPS=1", 3000);
  sendAT("AT+CGNSSLOADAZ=1", 3000);
  recoverInternet();
  trackingURL = serverHost_1 + "/tracking";
  stopsURL = serverHost_1 + "/points";
  lastValidGpsTime = millis(); // start the GPS loss timer from boot, not from 0
  safePrintln("✅ SYSTEM READY");
}
void loop() {
  loopCounter++;
  safePrintln("---- loop start (" + String(loopCounter) + ") ----");

  char rxBuf[RFID_UID_MAXLEN];
  while (xQueueReceive(rfidQueue, rxBuf, 0) == pdTRUE) {
    String uid = String(rxBuf);
    safePrint("📩 RFID UID: ");
    safePrintln(uid);
    String prevStatus = "";
    for (int i = 0; i < studentCount; i++) {
      if (students[i].cardUID == uid) {
        prevStatus = students[i].status;
        break;
      }
    }
    String rfidStatus = determineStudentStatus(uid);
    if (rfidStatus != prevStatus) {
      addPendingScan(uid, rfidStatus);
    }
  }

  if (loopCounter % 3 != 0) {
    safePrintln("⚡ Fast loop - " + String(pendingCount) + " pending");
    delay(300);
    return;
  }

  bool netOk = checkAndRecoverNetwork();
  if (!netOk) {
    safePrintln("📡 Network down - " + String(pendingCount) + " scans queued");
    delay(1000);
    return;
  }
  safePrintln("network: OK");

  if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    lastHeartbeatTime = millis();
    sendHeartbeat();
  }

  clearSerialBuffer();
  Serial2.println("AT+CGPSINFO");
  delay(1000);
  gpsData = "";
  while (Serial2.available()) {
    char c = Serial2.read();
    gpsData += c;
  }

  safePrintln("GPS RAW: " + gpsData);
  int startIdx = gpsData.indexOf("+CGPSINFO:");
  bool gpsValid = false;
  float latitude = lastLat;
  float longitude = lastLon;

  if (startIdx != -1) {
    String data = gpsData.substring(startIdx + 11);
    if (!data.startsWith(",") && data.indexOf(",,,,") == -1) {
      data.trim();
      int c1 = data.indexOf(',');
      int c2 = data.indexOf(',', c1 + 1);
      int c3 = data.indexOf(',', c2 + 1);
      int c4 = data.indexOf(',', c3 + 1);
      if (c1 != -1 && c2 != -1 && c3 != -1 && c4 != -1) {
        String latStr = data.substring(0, c1);
        String latDir = data.substring(c1 + 1, c2);
        String lonStr = data.substring(c2 + 1, c3);
        String lonDir = data.substring(c3 + 1, c4);
        latStr.trim();
        lonStr.trim();
        if (latStr.length() > 0 && lonStr.length() > 0) {
          latitude = latStr.substring(0, 2).toFloat() + latStr.substring(2).toFloat() / 60.0;
          longitude = lonStr.substring(0, 3).toFloat() + lonStr.substring(3).toFloat() / 60.0;
          if (latDir == "S") latitude *= -1;
          if (lonDir == "W") longitude *= -1;
          if (latitude != 0.0 && longitude != 0.0) {
            gpsValid = true;
            if (lastGpsUpdateTime > 0) {
              unsigned long timeDiffMs = millis() - lastGpsUpdateTime;
              currentSpeed = calculateSpeed(lastLat, lastLon, latitude, longitude, timeDiffMs);
              if (stopCount == 0 || millis() - lastStopFetchTime >= STOP_FETCH_INTERVAL) {
                if (fetchStopsFromServer()) {
                  lastStopFetchTime = millis();
                }
              }
              safePrint("🚀 Speed: ");
              safePrint(String(currentSpeed, 2));
              safePrintln(" km/h");
            }
            lastGpsUpdateTime = millis();
            lastLat = latitude;
            lastLon = longitude;
          }
        }
      }
    }
  }

  if (gpsValid) {
    lastValidGpsTime = millis();
    safePrintln("📍 LIVE GPS: " + String(latitude, 6) + "," + String(longitude, 6));
  } else {
    unsigned long gpsLostDuration = millis() - lastValidGpsTime;
    if (gpsLostDuration < 20000) {
      safePrintln("⚠️ USING LAST KNOWN GPS: " + String(latitude, 6) + "," + String(longitude, 6));
      if (stopCount == 0 || millis() - lastStopFetchTime >= STOP_FETCH_INTERVAL) {
        if (fetchStopsFromServer()) {
          lastStopFetchTime = millis();
        }
      }
    } else {
      safePrintln("❌ GPS SIGNAL LOST TOO LONG");
      safePrintln("⏳ WAITING FOR FRESH GPS FIX");
      delay(1000);
      return;
    }
  }

  if (latitude == 0.0 || longitude == 0.0) {
    safePrintln("❌ NO VALID GPS AVAILABLE");
    safePrintln("⏳ WAITING FOR FIRST GPS FIX...");
    delay(1000);
    return;
  }

  int drained = pendingCount;
  drainPendingScans(latitude, longitude);

  if (drained == 0) {
    bool success = uploadTodatabase(latitude, longitude, "", "");
    if (!success) {
      safePrintln("⚠️ GPS upload failed, retrying...");
      recoverInternet();
      delay(2000);
      uploadTodatabase(latitude, longitude, "", "");
    }
  }

  safePrintln("----------------------------------------");
  delay(1000);
}