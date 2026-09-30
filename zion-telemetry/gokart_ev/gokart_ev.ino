#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10
// The modem UART is not the same object on every chip. The S2 (and C3/C6)
// expose only UART0 and UART1, so MODEM_UART does not even exist there and the
// sketch fails to compile; the S3 and the original ESP32 have three and do.
// Selecting on the target keeps one source valid for both boards instead of
// silently editing 44 call sites whenever the hardware changes.
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6)
  #define MODEM_UART Serial1
#else
  #define MODEM_UART Serial2
#endif
const char *FIREBASE_DB =
  "https://zion-racing-telemetry-123c0-default-rtdb.asia-southeast1.firebasedatabase.app";
// ---- Battery monitoring ----
// Ported verbatim from battery_logic.ino. The define names, the mapFloat
// mapping and readBatteryPercentage() below are byte-for-byte the reference,
// so the percentage can only differ if the ADC input differs.
#define BATTERY_PIN 7
// Board divider ratio for 2S battery on IO7
#define VOLTAGE_DIVIDER_RATIO 3.571
#define BATTERY_CAPACITY_MAH 4300 // Capacity in mAh (4300mAh for 2S setup)
#define MIN_VOLTAGE 6.4           // 0% cutoff voltage for 2S pack
#define MAX_VOLTAGE 8.4           // 100% full voltage for 2S pack
float batteryPct = -1.0;
float batteryVolts = 0.0;
unsigned int batteryPinMv = 0;

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float readBatteryPercentage() {
  uint32_t raw_mv = analogReadMilliVolts(BATTERY_PIN);
  float total_voltage = (raw_mv * VOLTAGE_DIVIDER_RATIO) / 1000.0;
  float pct = mapFloat(total_voltage, MIN_VOLTAGE, MAX_VOLTAGE, 0.0, 100.0);
  return constrain(pct, 0.0, 100.0);
}

// Thin wrapper: the sketch also needs volts and the raw pin value for the
// dashboard payload and the serial line, so cache them alongside the percentage.
void readBattery() {
  batteryPinMv = analogReadMilliVolts(BATTERY_PIN);
  batteryVolts = (batteryPinMv * VOLTAGE_DIVIDER_RATIO) / 1000.0;
  batteryPct = readBatteryPercentage();
}

// Which attenuation suits this divider is a property of the board, not
// something to assume. Sweep the full range once at boot and print what the pin
// actually measures at each setting: the lowest value that is not pinned at the
// rail is the honest reading, and it turns a guessed divider ratio into a
// measured one.
void batteryAdcSweep() {
  Serial.println("---- BATTERY ADC SWEEP (GPIO" + String(BATTERY_PIN) + ") ----");
  // The S3 exposes 0/2.5/6/11 dB only; 12 and 13 dB are not defined for it.
  const adc_attenuation_t atts[] = {ADC_0db, ADC_2_5db, ADC_6db, ADC_11db};
  const char *names[] = {"  0dB", "2.5dB", "  6dB", "11dB"};
  for (int i = 0; i < 4; i++) {
    analogSetPinAttenuation(BATTERY_PIN, atts[i]);
    delay(60);
    uint32_t acc = 0;
    for (int n = 0; n < 16; n++) {
      acc += analogReadMilliVolts(BATTERY_PIN);
    }
    uint32_t mv = acc / 16;
    Serial.print("  " + String(names[i]) + " -> " + String(mv, 1) +
                 " mV   (raw count " + String(analogRead(BATTERY_PIN)) + ")");
    if (mv > 3090 || mv < 20) {
      Serial.println("  <== SATURATED, unusable");
    }
    else {
      Serial.println("");
    }
  }
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  Serial.println("Measure the pack with a multimeter, then set");
  Serial.println("VOLTAGE_DIVIDER_RATIO = real volts / pin mV x 1000");
  Serial.println("--------------------------------------------------");
}

// ---- identity: change these 4 lines to re-purpose this board ----
const char *DEVICE_NAME = "gokart_ev";
const char *FIREBASE_PATH = "/zion/gokart_ev/live.json";
const char *FIREBASE_START_PATH = "/zion/gokart_ev/startPoint.json";
const char *FIREBASE_RACE_PATH = "/zion/gokart_ev/race/";
const char *FIREBASE_TRACK_PATH = "/zion/gokart_ev/track/";
// -----------------------------------------------------------------
const float LAP_ENTER_RADIUS_M = 5.0;
const float LAP_EXIT_RADIUS_M = 10.0;
const unsigned long LAP_MIN_TIME = 20000;
const unsigned long STARTPOINT_POLL = 30000;
const char *FIREBASE_SECRET =
  "vbHx5u6gQCFfSUa3sPhEhP3tbLVf574nEQFUIR22";
const unsigned long UPDATE_INTERVAL = 1000;
String gpsData = "";
String simStatus = "UNKNOWN";
String netStatus = "UNKNOWN";
String gpsStatus = "WAITING";
String signalStrengthStr = "UNKNOWN";
float lastLat = 0.0;
float lastLon = 0.0;
float lastUploadedLat = 0.0;
float lastUploadedLon = 0.0;
float speedKmh = 0.0;
float prevFixLat = 0.0;
float prevFixLon = 0.0;
unsigned long prevFixTime = 0;
bool havePrevFix = false;
bool internetReady = false;
bool uploadInProgress = false;
unsigned long lastUploadTime = 0;
unsigned long lastCycleTime = 0;
unsigned long lastValidGpsTime = 0;
int uploadFailCount = 0;
long long timeOffset = 0;
bool timeSynced = false;
float startLat = 0.0;
float startLon = 0.0;
bool startPointKnown = false;
unsigned long lastStartPoll = 0;
bool insideLapZone = false;
bool lapArmed = false;
unsigned long lapStartTime = 0;
int lapCount = 0;
unsigned long lastLapMs = 0;
// A lap is a one-shot event: there is no "next cycle" to naturally re-send it.
// Hold the most recent unsaved lap here and keep retrying until it lands, so a
// dropped packet at the start line costs a delayed report rather than the lap.
// pendingLapValid is a separate flag rather than relying on pendingLapAt != 0,
// because a lap completed while millis() is still below 5 s would otherwise
// look like "nothing pending" and be dropped.
bool pendingLapValid = false;
int pendingLap = 0;
unsigned long pendingLapMs = 0;
unsigned long pendingLapAt = 0;
unsigned long pendingLapTries = 0;
const unsigned long LAP_RETRY_INTERVAL = 5000;
// ---- Offline track buffer ----
// A network outage must not erase the path the kart actually drove. Every cycle
// whose position could not be published is parked here and flushed to Firebase
// once the link returns, so the dashboard shows the real route including the
// gap, instead of a straight line jumping across the outage.
struct TrackPoint {
  float lat;
  float lon;
  unsigned long ms;
};
const unsigned int TRACK_CAPACITY = 320;   // ~5 min of 1 Hz data, 3.8 KB
const unsigned int TRACK_BATCH = 40;       // points per flush request
const unsigned int TRACK_MAX_BATCHES = 40; // ring over batch keys
TrackPoint trackQueue[TRACK_CAPACITY];
unsigned int trackHead = 0;    // oldest unread entry
unsigned int trackCount = 0;   // entries currently held
unsigned long trackDropped = 0;
unsigned int trackBatchSeq = 0;
unsigned long trackLastCycle = 0;   // guards against storing one point twice
unsigned long trackFlushAt = 0;
const unsigned long TRACK_FLUSH_INTERVAL = 5000;  // minimum gap between batches
const unsigned long TRACK_FLUSH_GAP = 500;        // spacing while draining
const char *MONTHS[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                         "Jul","Aug","Sep","Oct","Nov","Dec"};
// Drain stale URCs before sending a command. Waiting a fixed 300 ms every time
// cost >1 s per upload on its own, which made a 1 Hz cycle impossible. Instead
// stop as soon as the line has been quiet for QUIET_MS, keeping a hard cap for
// the case where the modem is streaming continuously.
void clearSerialBuffer() {
    const unsigned long QUIET_MS = 25, CAP_MS = 300;
    unsigned long start = millis();
    unsigned long lastData = start;
    while (millis() - start < CAP_MS) {
        bool got = false;
        while (MODEM_UART.available()) {
            MODEM_UART.read();
            got = true;
        }
        if (got) {
            lastData = millis();
        } else if (millis() - lastData >= QUIET_MS) {
            break;
        }
    }
}
bool httpSessionOpen = false;
unsigned long cycleStarted = 0;
unsigned long lastNetworkCheck = 0;
const unsigned long NETWORK_CHECK_INTERVAL = 15000;

// verbose defaults to false so the per-cycle AT traffic stays off the terminal.
// The modem conversation is only worth seeing when something is actually going
// wrong, so callers opt in explicitly during recovery and failure paths.
String sendAT(String cmd, int waitTime = 3000, bool verbose = false) {
  String response = "";
  clearSerialBuffer();
  MODEM_UART.println(cmd);
  unsigned long start = millis();
  while (millis() - start < waitTime) {
    while (MODEM_UART.available()) {
      char c = MODEM_UART.read();
      response += c;
    }
    
    if (response.indexOf("OK") != -1 ||
        response.indexOf("ERROR") != -1 ||
        response.indexOf("DOWNLOAD") != -1 ||
        response.indexOf("+HTTPACTION:") != -1) {
      break;
    }
  }
  if (verbose) {
    Serial.println(">> " + cmd + " [RESP]: " + response);
  }
  return response;
}
bool recoverInternet() {
  Serial.println("🔄 FULL INTERNET RECOVERY");
  // The modem is about to be reset, so any session we thought we had is gone.
  httpSessionOpen = false;
  closeHttpSession();
  // Recovery is exactly when the AT conversation is worth reading, so these few
  // calls opt in to verbose output. Everywhere else stays silent.
  sendAT("AT+NETCLOSE", 5000, true);
  delay(2000);
  sendAT("AT+CGATT=0", 5000, true);
  delay(3000);
  sendAT("AT+CGATT=1", 10000, true);
  delay(3000);
  String netOpen = sendAT("AT+NETOPEN", 15000, true);
  if (netOpen.indexOf("OK") == -1 &&
      netOpen.indexOf("+NETOPEN: 0") == -1) {
    Serial.println("❌ NETOPEN FAILED");
    return false;
  }
  delay(3000);
  if (!openHttpSession()) {
    Serial.println("❌ HTTPINIT FAILED");
    return false;
  }
  internetReady = true;
    Serial.println("✅ INTERNET RESTORED");
    // Cycle the GNSS engine after every network recovery. A modem that has just
    // re-attached often needs the engine nudged before it will search again.
    sendAT("AT+CGNSSPWR=0", 3000);
    delay(2000);
    String pwr = sendAT("AT+CGNSSPWR=1", 3000);
    delay(2000);
    // AT+CGPS=1 is not supported on every SIM7600 firmware and answers ERROR
    // harmlessly. Do not treat that as a failure.
    sendAT("AT+CGPS=1", 3000);
    if (pwr.indexOf("+CGNSSPWR: 1") == -1) {
      Serial.println("⚠️ GNSS power did not confirm: " + pwr);
    }
  delay(3000);
  lastNetworkCheck = millis();
 return true;
}
bool checkAndRecoverNetwork() {
  String cpinCheck = sendAT("AT+CPIN?", 2000);
  if (cpinCheck.indexOf("READY") == -1) {
    simStatus = "FAIL";
    signalStrengthStr = "NO SIGNAL";
    Serial.println("⚠️ SIM FAILURE DETECTED");
    sendAT("AT+CFUN=0", 5000);
    delay(3000);
    sendAT("AT+CFUN=1", 8000);
    delay(5000);
    cpinCheck = sendAT("AT+CPIN?", 3000);
    if (cpinCheck.indexOf("READY") != -1) {
      Serial.println("✅ SIM REINSERTED SUCCESSFULLY");
      simStatus = "OK";
      recoverInternet();
      return true;
    }
    Serial.println("❌ SIM STILL NOT DETECTED");
 return false;
  }
  simStatus = "OK";
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
    if (rssiVal == 99) {
      signalStrengthStr = "NO SIGNAL";
    }
    else if (rssiVal < 10) {
      signalStrengthStr = "POOR";
    }
    else if (rssiVal < 15) {
      signalStrengthStr = "FAIR";
    }
    else {
      signalStrengthStr = "GOOD";
    }
 Serial.println(
      "📶 Signal: " +
      signalStrengthStr +
      " (" + rssiRaw + ")"
    );
  }
  String cregCheck = sendAT("AT+CREG?", 2000);
  if (cregCheck.indexOf(",1") == -1 && cregCheck.indexOf(",5") == -1) {
    netStatus = "FAIL";
    Serial.println("📡 NETWORK LOST");
    recoverInternet();
    delay(3000);
    cregCheck = sendAT("AT+CREG?", 2000);
    if (cregCheck.indexOf(",1") == -1 &&
        cregCheck.indexOf(",5") == -1) {
   return false;
    }
  }
  netStatus = "OK";
 return true;
}
long long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long long era = (y >= 0 ? y : y - 399) / 400;
  long long yoe = y - era * 400;
  long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + doe - 719468LL;
}
long long parseHttpDate(const String &buf) {
  int c = buf.indexOf("Date:");
  if (c == -1) return 0;
  String s = buf.substring(c + 5);
  int comma = s.indexOf(',');
  if (comma == -1) return 0;
  s = s.substring(comma + 1);
  s.trim();
  int p = s.indexOf(' ');
  if (p < 0) return 0;
  int day = s.substring(0, p).toInt();
  s = s.substring(p + 1);
  p = s.indexOf(' ');
  if (p < 0) return 0;
  String mon = s.substring(0, p);
  s = s.substring(p + 1);
  p = s.indexOf(' ');
  if (p < 0) return 0;
  int year = s.substring(0, p).toInt();
  s = s.substring(p + 1);
  p = s.indexOf(':');
  if (p < 0) return 0;
  int hh = s.substring(0, p).toInt();
  s = s.substring(p + 1);
  p = s.indexOf(':');
  if (p < 0) return 0;
  int mm = s.substring(0, p).toInt();
  s = s.substring(p + 1);
  p = s.indexOf(' ');
  int ss = s.substring(0, p < 0 ? s.length() : p).toInt();
  int m = -1;
  for (int i = 0; i < 12; i++) {
    if (mon.startsWith(MONTHS[i])) { m = i + 1; break; }
  }
  if (m < 1 || day < 1 || year < 2020) return 0;
  long long days = daysFromCivil(year, m, day);
  return (days * 86400LL + hh * 3600LL + mm * 60LL + ss) * 1000LL;
}
bool syncTimeFromServer() {
  // HTTPHEAD needs an open session. It runs right after a successful upload,
  // so the session is normally up, but guard anyway rather than firing a
  // command at a closed context.
  if (!httpSessionOpen) {
    Serial.println("CLOCK: skipped, no HTTP session");
    return false;
  }
  clearSerialBuffer();
  MODEM_UART.println("AT+HTTPHEAD");
  delay(500);
  String buf = "";
  unsigned long start = millis();
  while (millis() - start < 2500) {
    while (MODEM_UART.available()) {
      buf += (char)MODEM_UART.read();
    }
  }
  long long serverMs = parseHttpDate(buf);
  if (serverMs <= 0) {
    Serial.println("CLOCK: could not read server date");
    return false;
  }
  timeOffset = serverMs - (long long)millis();
  timeSynced = true;
  Serial.println("CLOCK: UTC synced from network");
  return true;
}
float distanceMeters(float lat1, float lon1, float lat2, float lon2) {
  double toRad = 0.01745329252;
  double dLat = (lat2 - lat1) * toRad;
  double dLon = (lon2 - lon1) * toRad * cos((lat1 + lat2) * 0.00872664626);
  double a = dLat * dLat + dLon * dLon;
  return (float)(6371000.0 * sqrt(a));
}
float updateSpeed(float lat, float lon, bool freshFix) {
  if (!freshFix) return speedKmh;
  unsigned long nowMs = millis();
  if (!havePrevFix) {
    havePrevFix = true;
    prevFixLat = lat;
    prevFixLon = lon;
    prevFixTime = nowMs;
    return 0.0;
  }
  unsigned long dt = nowMs - prevFixTime;
  float dist = distanceMeters(prevFixLat, prevFixLon, lat, lon);
  prevFixLat = lat;
  prevFixLon = lon;
  prevFixTime = nowMs;
  if (dt < 2000) return speedKmh;
  float raw = (dist / (dt / 1000.0)) * 3.6;
  if (raw > 160.0) return speedKmh;
  speedKmh = speedKmh * 0.35 + raw * 0.65;
  if (speedKmh < 0.3) speedKmh = 0.0;
  return speedKmh;
}
float jsonNumber(const String &body, const String &key) {
  String k = "\"" + key + "\"";
  int at = body.indexOf(k);
  if (at == -1) return 0.0;
  int p = body.indexOf(':', at + k.length());
  if (p == -1) return 0.0;
  p++;
  while (p < (int)body.length() && body[p] == ' ') p++;
  int e = p;
  while (e < (int)body.length()) {
    char c = body[e];
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') e++;
    else break;
  }
  return body.substring(p, e).toFloat();
}
void resetLapCounting() {
  lapCount = 0;
  lapArmed = false;
  insideLapZone = false;
  lapStartTime = millis();
  lastLapMs = 0;
  // Deliberately NOT clearing pendingLap. Moving the start point must not throw
  // away a lap that has not reached Firebase yet; it still needs to be saved.
}
bool fetchStartPoint() {
  // Go through the shared session helpers so httpSessionOpen stays truthful.
  // Terminating the session by hand here used to leave the flag set to true
  // while the modem had actually shut it down, and every later upload then
  // failed on a dead session until a full reconnect happened.
  closeHttpSession();
  if (!openHttpSession()) {
    startPointKnown = false;
    Serial.println("START POINT: HTTPINIT failed");
    return false;
  }
  sendAT(
    "AT+HTTPPARA=\"URL\",\"" + String(FIREBASE_DB) +
    FIREBASE_START_PATH + "?auth=" + FIREBASE_SECRET + "\"",
    5000
  );
  clearSerialBuffer();
  MODEM_UART.println("AT+HTTPACTION=0");
  unsigned long waitStart = millis();
  String actionResult = "";
  bool ok = false;
  while (millis() - waitStart < 7000) {
    while (MODEM_UART.available()) {
      char c = MODEM_UART.read();
      actionResult += c;
      if (actionResult.indexOf(",200,") != -1) { ok = true; break; }
      if (actionResult.indexOf(",404,") != -1) break;
      if (actionResult.indexOf(",401,") != -1) break;
    }
    if (ok) break;
  }
  String body = "";
  if (ok) {
    clearSerialBuffer();
    MODEM_UART.println("AT+HTTPREAD=0,300");
    delay(500);
    unsigned long readStart = millis();
    while (millis() - readStart < 2500) {
      while (MODEM_UART.available()) {
        body += (char)MODEM_UART.read();
      }
    }
  }
  closeHttpSession();
  if (!ok) {
    if (startPointKnown) {
      startPointKnown = false;
      Serial.println("START POINT: lost, lap counting stopped");
    }
    return false;
  }
  float la = jsonNumber(body, "latitude");
  float lo = jsonNumber(body, "longitude");
  if (la == 0.0 || lo == 0.0) {
    startPointKnown = false;
    Serial.println("START POINT: invalid coordinates");
    return false;
  }
  if (!startPointKnown || la != startLat || lo != startLon) {
    Serial.println(
      "START POINT: " + String(la, 6) + "," + String(lo, 6)
    );
    startLat = la;
    startLon = lo;
    startPointKnown = true;
    resetLapCounting();
  }
  return true;
}
int checkLapZone(float lat, float lon) {
  if (!startPointKnown) return 0;
  float d = distanceMeters(lat, lon, startLat, startLon);
  if (d > LAP_EXIT_RADIUS_M) {
    insideLapZone = false;
    if (!lapArmed) {
      lapArmed = true;
      lapStartTime = millis();
    }
    return 0;
  }
  if (d >= LAP_ENTER_RADIUS_M) return 0;
  if (insideLapZone) return 0;
  insideLapZone = true;
  if (!lapArmed) return 0;
  if (millis() - lapStartTime < LAP_MIN_TIME) {
    Serial.println("LAP: crossing too soon, ignored");
    lapStartTime = millis();
    return 0;
  }
  lastLapMs = millis() - lapStartTime;
  lapStartTime = millis();
  lapArmed = false;
  lapCount++;
  Serial.println(
    "🏁 LAP " + String(lapCount) + " in " +
    String((float)lastLapMs / 1000.0, 2) + " s"
  );
  return lapCount;
}
// Save a completed lap into the pending slot. Only one lap is held at a time:
// in practice a retry clears long before another lap is completed, and if a
// second lap somehow arrives first the newer lap is the one worth keeping.
void queueLap(int n, unsigned long ms) {
  if (pendingLapValid && pendingLap != n) {
    Serial.println(
      "⚠️ LAP " + String(pendingLap) + " still unsaved, overwriting"
    );
  }
  pendingLapValid = true;
  pendingLap = n;
  pendingLapMs = ms;
  pendingLapAt = millis();
  pendingLapTries = 0;
}
void clearPendingLap() {
  pendingLapValid = false;
  pendingLap = 0;
  pendingLapMs = 0;
  pendingLapAt = 0;
  pendingLapTries = 0;
}
// Try to flush the pending lap, at most once per call so a failing modem cannot
// stall the 1 Hz telemetry cycle. Returns true while a lap is still outstanding.
bool servicePendingLap() {
  if (!pendingLapValid) return false;
  if (millis() - pendingLapAt < LAP_RETRY_INTERVAL) return true;
  pendingLapTries++;
  int n = pendingLap;
  unsigned long ms = pendingLapMs;
  if (uploadLapTime(n, ms)) {
    Serial.println(
      "✅ LAP " + String(n) + " saved on try " + String(pendingLapTries)
    );
    clearPendingLap();
    return false;
  }
  // Back off a little so a long outage does not queue up attempts.
  pendingLapAt = millis();
  if (pendingLapTries == 1 || pendingLapTries % 10 == 0) {
    Serial.println(
      "⏳ LAP " + String(n) + " still unsaved (try " +
      String(pendingLapTries) + ")"
    );
  }
  return true;
}
void printHttpError() {
  clearSerialBuffer();
  MODEM_UART.println("AT+HTTPREAD=0,400");
  delay(500);
  String buf = "";
  unsigned long start = millis();
  while (millis() - start < 2000) {
    while (MODEM_UART.available()) {
      buf += (char)MODEM_UART.read();
    }
  }
  Serial.println("[HTTP ERROR BODY]: " + buf);
}
bool openHttpSession() {
  if (httpSessionOpen) return true;
  String initResp = sendAT("AT+HTTPINIT", 5000);
  if (initResp.indexOf("OK") == -1) {
    Serial.println("❌ HTTPINIT FAILED");
    return false;
  }
  sendAT("AT+HTTPPARA=\"CID\",1", 3000);
  httpSessionOpen = true;
  return true;
}
void closeHttpSession() {
  if (!httpSessionOpen) return;
  sendAT("AT+HTTPTERM", 2000);
  httpSessionOpen = false;
}

// Store one position in the ring. Oldest data is overwritten when full, so a
// long outage degrades to "the last few minutes" rather than losing everything.
void queueTrackPoint(float lat, float lon, unsigned long ms) {
  if (trackCount == TRACK_CAPACITY) {
    trackHead = (trackHead + 1) % TRACK_CAPACITY;
    trackCount--;
    trackDropped++;
    if (trackDropped == 1 || trackDropped % 100 == 0) {
      Serial.println(
        "⚠️ TRACK BUFFER FULL, dropped " + String(trackDropped) + " old points"
      );
    }
  }
  unsigned int slot = (trackHead + trackCount) % TRACK_CAPACITY;
  trackQueue[slot].lat = lat;
  trackQueue[slot].lon = lon;
  trackQueue[slot].ms = ms;
  trackCount++;
}
void clearTrackPoint() {
  trackHead = (trackHead + 1) % TRACK_CAPACITY;
  trackCount--;
}
// Publish the oldest buffered positions as one JSON array, then drop them. The
// whole batch is sent in a single request: one request per point would take
// minutes to drain and would wreck the 1 Hz cadence.
bool flushTrackQueue() {
  if (trackCount == 0) return true;
  unsigned int n = trackCount < TRACK_BATCH ? trackCount : TRACK_BATCH;
  unsigned long stamp = timeSynced ? (unsigned long)
    ((long long)millis() + timeOffset) : 0;
  String body = "[";
  for (unsigned int i = 0; i < n; i++) {
    unsigned int slot = (trackHead + i) % TRACK_CAPACITY;
    if (i) body += ",";
    body += "[" + String(trackQueue[slot].lat, 6) + "," +
            String(trackQueue[slot].lon, 6);
    // Third element is the kart's own timebase when synced, so the frontend can
    // place buffered points correctly on the trail.
    if (stamp) body += "," + String((long long)trackQueue[slot].ms + timeOffset);
    body += "]";
  }
  body += "]";
  String url = String(FIREBASE_DB) + FIREBASE_TRACK_PATH +
    "b" + String(trackBatchSeq) + ".json?auth=" +
    FIREBASE_SECRET + "&x-http-method-override=PUT";
  if (!openHttpSession()) return false;
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 5000);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 3000);
  String dataResp = sendAT(
    "AT+HTTPDATA=" + String(body.length()) + ",15000", 5000
  );
  if (dataResp.indexOf("DOWNLOAD") == -1) {
    Serial.println("❌ TRACK FLUSH: HTTPDATA FAILED");
    closeHttpSession();
    return false;
  }
  delay(100);
  MODEM_UART.print(body);
  unsigned long sent = millis();
  while (millis() - sent < 400 && MODEM_UART.available()) MODEM_UART.read();
  delay(150);
  MODEM_UART.println();
  MODEM_UART.println("AT+HTTPACTION=1");
  unsigned long waitStart = millis();
  String actionResult = "";
  bool ok = false;
  while (millis() - waitStart < 8000) {
    while (MODEM_UART.available()) {
      char c = MODEM_UART.read();
      actionResult += c;
      if (actionResult.indexOf(",200,") != -1 ||
          actionResult.indexOf(",201,") != -1) { ok = true; break; }
      if (actionResult.indexOf(",40") != -1 ||
          actionResult.indexOf(",50") != -1) break;
    }
    if (ok) break;
  }
  if (!ok) {
    printHttpError();
    closeHttpSession();
    return false;
  }
  for (unsigned int i = 0; i < n; i++) clearTrackPoint();
  trackBatchSeq = (trackBatchSeq + 1) % TRACK_MAX_BATCHES;
  Serial.println(
    "📤 TRACK FLUSHED " + String(n) + " points, " +
    String(trackCount) + " still queued"
  );
  return true;
}
// One buffered position per failed cycle, with a one-per-cycle guard.
void serviceTrackQueue(float lat, float lon) {
  if (lat == 0.0 || lon == 0.0) return;
  if (millis() == trackLastCycle) return;
  trackLastCycle = millis();
  queueTrackPoint(lat, lon, millis());
}
// Drain buffered positions, at most one batch per cycle and never more often
// than TRACK_FLUSH_INTERVAL, so a large backlog cannot starve live telemetry.
void serviceTrackFlush() {
  if (trackCount == 0) return;
  if (millis() - trackFlushAt < TRACK_FLUSH_INTERVAL) return;
  if (uploadInProgress) return;
  trackFlushAt = millis();
  if (flushTrackQueue()) {
    // Keep going next cycle while a backlog remains.
    trackFlushAt = millis() - TRACK_FLUSH_INTERVAL + TRACK_FLUSH_GAP;
  }
}
bool uploadToFirebase(float latitude, float longitude) {
  if (uploadInProgress) {
    Serial.println("⚠️ Previous upload still active");
   return false;
  }
  uploadInProgress = true;
  String json = "{";
  json += "\"name\":\"" + String(DEVICE_NAME) + "\"";
  json += ",\"latitude\":" + String(latitude, 6) + ",";
  json += "\"longitude\":" + String(longitude, 6);
  json += ",\"speed\":" + String(speedKmh, 1);
  if (batteryPct >= 0.0) {
    json += ",\"soc\":" + String(batteryPct, 1);
    json += ",\"voltage\":" + String(batteryVolts, 2);
  }
  if (timeSynced) {
    json += ",\"timestamp\":" +
      String((long long)millis() + timeOffset);
  }
  if (startPointKnown && lapCount > 0) {
    json += ",\"lap\":" + String(lapCount);
    json += ",\"lapElapsed\":" +
      String((float)(millis() - lapStartTime) / 1000.0, 2);
  }
  json += ",\"sim\":\"" + simStatus + "\"";
  json += ",\"net\":\"" + netStatus + "\"";
  json += ",\"gps\":\"" + gpsStatus + "\"";
  json += ",\"signal\":\"" + signalStrengthStr + "\"";
  // Let the dashboard show that positions are held on the kart and not lost.
  json += ",\"queued\":" + String(trackCount);
  if (trackDropped) json += ",\"dropped\":" + String(trackDropped);
  json += "}";
  String url =
  String(FIREBASE_DB) + FIREBASE_PATH + "?auth=" +
  FIREBASE_SECRET + "&x-http-method-override=PUT";
  if (!openHttpSession()) {
    uploadInProgress = false;
    return false;
  }
  sendAT(
    "AT+HTTPPARA=\"URL\",\"" + url + "\"",
    5000
  );
  sendAT(
    "AT+HTTPPARA=\"CONTENT\",\"application/json\"",
    3000
  );
  String dataCmd =
    "AT+HTTPDATA=" + String(json.length()) + ",10000";
  String dataResp = sendAT(dataCmd, 5000);
  if (dataResp.indexOf("DOWNLOAD") == -1) {
    Serial.println("❌ HTTPDATA FAILED");
    closeHttpSession();
    uploadInProgress = false;
    return false;
  }
  delay(100);
  MODEM_UART.print(json);
  // Wait for the modem to consume the payload. Polling for the echoed prompt
  // response is far quicker than a fixed sleep, but fall back to a short grace
  // period so a silent modem cannot stall the cycle.
  unsigned long sentAt = millis();
  while (millis() - sentAt < 400 && MODEM_UART.available()) {
    MODEM_UART.read();
  }
  delay(150);
  MODEM_UART.println();
  MODEM_UART.println("AT+HTTPACTION=1");
  String actionResult = "";
  bool success = false;
  unsigned long startWait = millis();
  while (millis() - startWait < 5000) {
    while (MODEM_UART.available()) {
      char c = MODEM_UART.read();
      actionResult += c;
      if (actionResult.indexOf(",200,") != -1 ||
          actionResult.indexOf(",201,") != -1) {
        success = true;
        break;
      }
      if (actionResult.indexOf(",40") != -1 ||
          actionResult.indexOf(",50") != -1) {
        break;
      }
    }
    if (success) break;
  }
  if (success && !timeSynced) {
    syncTimeFromServer();
  }
  if (!success) {
    // The error body lives in the session, so it has to be read BEFORE the
    // session is terminated. Reading it afterwards always returns empty.
    printHttpError();
    closeHttpSession();
  }
  if (success) {
    uploadFailCount = 0;
    lastUploadTime = millis();
    lastUploadedLat = latitude;
    lastUploadedLon = longitude;
    clearSerialBuffer();
    uploadInProgress = false;
    return true;
  }
  Serial.println("❌ FIREBASE FAILED");
  uploadInProgress = false;
  return false;
}
bool uploadLapTime(int n, unsigned long ms) {
  String body = String((float)ms / 1000.0, 2);
  String url = String(FIREBASE_DB) + FIREBASE_RACE_PATH +
    "lap" + String(n) + ".json?auth=" +
    FIREBASE_SECRET + "&x-http-method-override=PUT";
  if (!openHttpSession()) {
    Serial.println("❌ LAP TIME: HTTPINIT FAILED");
    return false;
  }
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 5000);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 3000);
  String dataResp = sendAT(
    "AT+HTTPDATA=" + String(body.length()) + ",10000", 5000
  );
  if (dataResp.indexOf("DOWNLOAD") == -1) {
    Serial.println("❌ LAP TIME: HTTPDATA FAILED");
    closeHttpSession();
    return false;
  }
  delay(100);
  MODEM_UART.print(body);
  unsigned long sentLap = millis();
  while (millis() - sentLap < 400 && MODEM_UART.available()) {
    MODEM_UART.read();
  }
  delay(150);
  MODEM_UART.println();
  // No drain here: the payload was just written, so anything pending is its
  // echo. Draining first would risk discarding the tail of the body.
  MODEM_UART.println("AT+HTTPACTION=1");
  unsigned long waitStart = millis();
  String actionResult = "";
  bool ok = false;
  while (millis() - waitStart < 5000) {
    while (MODEM_UART.available()) {
      char c = MODEM_UART.read();
      actionResult += c;
      if (actionResult.indexOf(",200,") != -1) { ok = true; break; }
      if (actionResult.indexOf(",40") != -1 || actionResult.indexOf(",50") != -1) break;
    }
    if (ok) break;
  }
  if (!ok) {
    // Read the failure reason while the session is still open, otherwise the
    // body is discarded and an unsaveable lap can never be diagnosed.
    printHttpError();
    closeHttpSession();
  }
  Serial.println(ok ? "✅ LAP TIME SAVED" : "❌ LAP TIME FAILED");
  return ok;
}
void setup() {
  Serial.begin(115200);
  MODEM_UART.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  // The 2S divider on IO7 swings up to ~2.4 V, well above the ADC's default
  // measurable range. Without an explicit attenuation the raw read saturates
  // low, so the pack reports ~5 V and 0% no matter what its real charge is.
  // These two calls are the whole reason the reading is sane; do not drop them.
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(8000);
  Serial.println("🚀 " + String(DEVICE_NAME) + " STARTING");
  sendAT("AT", 2000);
  sendAT("AT+CPIN?", 2000);
  sendAT("AT+CGATT=1", 5000);
  sendAT("AT+NETOPEN", 10000);
  sendAT("AT+CGNSSPWR=1", 3000);
  sendAT("AT+CGPS=1", 3000);
  sendAT("AT+CGNSSLOADAZ=1", 3000);
  recoverInternet();
  Serial.println("✅ SYSTEM READY");
}
// Dump the raw GNSS engine state. With no fix for this long it is impossible to
// tell "engine off", "no satellites" and "wrong command" apart from the outside,
// because the position payload is empty in all three cases.
unsigned long lastGnssDiag = 0;
unsigned long lastGpsLostMsg = 0;
void diagnoseGnss() {
  Serial.println("──── GNSS DIAGNOSTIC ────");
  String pwr = sendAT("AT+CGNSSPWR?", 3000);
  String inf = sendAT("AT+CGNSINF", 3000);
  String loc = sendAT("AT+CGNSSLOC?", 3000);
  String gps = sendAT("AT+CGPSINFO", 4000);
  // Serial.println("CGNSSPWR? -> " + pwr);
  // Serial.println("CGNSINF   -> " + inf);
  // Serial.println("CGNSSLOC? -> " + loc);
  // Serial.println("CGPSINFO  -> " + gps);
  // Satellites in view is the field that actually matters. 00 means the engine
  // is on but has not seen anything, which is an antenna or sky-view problem
  // rather than a modem or code problem.
  int sat = -1;
  int run = 0;
  for (int i = 0; i < inf.length(); i++) {
    if (inf.charAt(i) == ',' && ++run == 2) {
      sat = inf.substring(i + 1).toInt();
      break;
    }
  }
  if (pwr.indexOf("+CGNSSPWR:") != -1 && pwr.indexOf(",1") == -1) {
    Serial.println("=> GNSS ENGINE IS OFF. Check antenna power and the module's GNSS pin.");
  } else if (sat == 0) {
    Serial.println("=> Engine is ON but sees 0 satellites. Needs a clear sky view and a working active antenna.");
  } else if (sat > 0) {
    Serial.println("=> " + String(sat) + " satellite(s) in view. A fix should follow within a minute or two.");
  } else {
    Serial.println("=> Could not read satellite count from CGNSINF. Firmware may not support it.");
  }
  Serial.println("──────────────────────────");
  lastGnssDiag = millis();
}
// One line per cycle is the entire normal output. It carries position, charge
// and, most importantly, whether the sample actually reached Firebase, so the
// operator never has to read modem traffic to know the kart is reporting.
// ASCII only, so it survives any terminal encoding.
void printTelemetry(float lat, float lon, bool freshFix, bool netUp,
                    bool sent) {
  Serial.print("[1Hz] ");
  Serial.print(String(lat, 5) + ", " + String(lon, 5));
  Serial.print("  |  batt ");
  if (batteryPct < 0) {
    Serial.print("??");
  }
  else {
    Serial.print(String((int)(batteryPct + 0.5f)) + "%");
  }
  Serial.print(" (" + String(batteryVolts, 2) + "V)");
  Serial.print("  |  " + String(speedKmh, 1) + " km/h");
  Serial.print("  |  " + String(freshFix ? "fix OK" : "fix LOST"));
  // The one thing the operator actually needs: did this sample land?
  if (!netUp) {
    Serial.print("  |  FIREBASE SKIPPED (no network, buffered " +
                 String(trackCount) + ")");
  }
  else if (sent) {
    Serial.println("  |  FIREBASE OK");
  }
  else {
    Serial.println("  |  FIREBASE FAIL (buffered " + String(trackCount) + ")");
  }
}

// Diagnostics are on demand rather than automatic: the terminal stays clean
// unless a human asks for detail. Type 'b' in the monitor for the ADC sweep,
// 'g' for the GNSS engine state.
void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'b' || c == 'B') {
      Serial.println();
      batteryAdcSweep();
    }
    if (c == 'g' || c == 'G') {
      Serial.println();
      diagnoseGnss();
    }
  }
}
void loop() {
  handleSerialCommands();
  if (millis() - lastCycleTime < UPDATE_INTERVAL) {
    delay(20);
    return;
  }
  lastCycleTime = millis();
  cycleStarted = lastCycleTime;
  // CPIN/CSQ/CREG are three extra blocking AT round-trips. Only worth
  // paying for periodically, not on every 1 s cycle.
  // Note: an outage must NOT return early here. The GPS read below is what
  // fills the offline track buffer, so bailing out would record nothing and
  // the driven path would be lost for good.
  bool networkUp = true;
  if (millis() - lastNetworkCheck > NETWORK_CHECK_INTERVAL) {
    lastNetworkCheck = millis();
    networkUp = checkAndRecoverNetwork();
    if (!networkUp) {
      Serial.println("🛑 NETWORK UNAVAILABLE - buffering positions");
    }
  }
  clearSerialBuffer();
  MODEM_UART.println("AT+CGPSINFO");
  // Poll for the response instead of a fixed delay, so a fast modem reply
  // does not cost a full 2 s every cycle.
  gpsData = "";
  unsigned long gpsWait = millis();
  while (millis() - gpsWait < 1500) {
    while (MODEM_UART.available()) {
      char c = MODEM_UART.read();
      gpsData += c;
      if (gpsData.indexOf("OK") != -1) break;
    }
    if (gpsData.indexOf("OK") != -1) break;
  }
  // Collect any trailing bytes once the line goes quiet, rather than always
  // sleeping a flat 100 ms that buys nothing on a fast reply.
  unsigned long tail = millis(), lastByte = tail;
  while (millis() - tail < 200) {
    bool got = false;
    while (MODEM_UART.available()) {
      gpsData += (char)MODEM_UART.read();
      got = true;
    }
    if (got) {
      lastByte = millis();
    } else if (millis() - lastByte >= 25) {
      break;
    }
  }
  int startIdx = gpsData.indexOf("+CGPSINFO:");
  bool gpsValid = false;
  float latitude = lastLat;
  float longitude = lastLon;
  if (startIdx != -1) {
    String data =
      gpsData.substring(startIdx + 11);
    if (data.startsWith(",") ||
        data.indexOf(",,,,") != -1) {
      gpsValid = false;
      Serial.println("❌ EMPTY GPS RESPONSE");
    }
    else {
      data.trim();
      int c1 = data.indexOf(',');
      int c2 = data.indexOf(',', c1 + 1);
      int c3 = data.indexOf(',', c2 + 1);
      int c4 = data.indexOf(',', c3 + 1);
      if (c1 != -1 &&
          c2 != -1 &&
          c3 != -1 &&
          c4 != -1) {
        String latStr = data.substring(0, c1);
        String latDir = data.substring(c1 + 1, c2);
        String lonStr = data.substring(c2 + 1, c3);
        String lonDir = data.substring(c3 + 1, c4);
        latStr.trim();
        lonStr.trim();
        if (latStr.length() > 0 &&
            lonStr.length() > 0) {
          latitude =
            latStr.substring(0, 2).toFloat() +
            latStr.substring(2).toFloat() / 60.0;
            longitude =
            lonStr.substring(0, 3).toFloat() +
            lonStr.substring(3).toFloat() / 60.0;
            if (latDir == "S")
            latitude *= -1;
            if (lonDir == "W")
            longitude *= -1;
          if (latitude != 0.0 &&
              longitude != 0.0) {
            gpsValid = true;
         lastLat = latitude;
            lastLon = longitude;
          }
        }
      }
    }
  }
  if (gpsValid) {
    gpsStatus = "RECEIVED";
  lastValidGpsTime = millis();
  }
  else {
    unsigned long gpsLostDuration =
      millis() - lastValidGpsTime;
    havePrevFix = false;                 // no fresh position to derive speed from
  if (gpsLostDuration < 20000) {
    gpsStatus = "LAST_KNOWN";
    }
    // Rate-limit the chatter, but still report charge every cycle so the pack
    // is never invisible just because the sky is blocked.
    if (millis() - lastGpsLostMsg > 30000) {
      lastGpsLostMsg = millis();
      Serial.println("❌ NO GPS FIX - waiting. Type 'g' in the monitor for GNSS detail.");
    }
    readBattery();
    printTelemetry(latitude, longitude, false, networkUp, false);
    delay(3000);
    return;
  }
  if (latitude == 0.0 ||
      longitude == 0.0) {
    if (millis() - lastGpsLostMsg > 30000) {
      lastGpsLostMsg = millis();
      Serial.println("❌ NO GPS FIX YET - waiting. Type 'g' in the monitor for GNSS detail.");
    }
    readBattery();
    // No coordinates yet, so report the zeroes honestly rather than inventing
    // a position. The battery field is the useful part here.
    printTelemetry(0.0, 0.0, false, networkUp, false);
    delay(3000);
    return;
  }
  readBattery();
  updateSpeed(latitude, longitude, gpsValid);
  // Skip the start-point poll while the link is down. It would only burn modem
  // time and fail, and the existing start point is already known.
  if (networkUp && millis() - lastStartPoll > STARTPOINT_POLL) {
    lastStartPoll = millis();
    fetchStartPoint();
  }
  int completedLap = checkLapZone(latitude, longitude);
  bool success = false;
  if (networkUp) {
    success = uploadToFirebase(latitude, longitude);
    if (!success) {
      uploadFailCount++;
      // A single blip is usually a transient cell failure: just retry. Only tear
      // the PDP context down after repeated failures, because recoverInternet()
      // blocks for roughly 30 s and would destroy the 1 Hz cadence on every
      // momentary dropout.
      if (uploadFailCount >= 3) {
        uploadFailCount = 0;
        recoverInternet();
      }
      success = uploadToFirebase(latitude, longitude);
    }
  }
  // Printed after the upload so the result is known. One line per cycle is the
  // whole point: the operator should never have to read modem traffic to learn
  // whether the kart is reporting.
  printTelemetry(latitude, longitude, gpsValid, networkUp, success);
  // Whatever happened, keep this position. If it never reached Firebase it has
  // to be replayed later or the driven path is lost for the outage window.
  if (!success) serviceTrackQueue(latitude, longitude);
  if (completedLap > 0 && lastLapMs > 0) {
    // Park it in the pending slot instead of posting once and hoping. A failure
    // now costs one missed cycle rather than the whole lap.
    queueLap(completedLap, lastLapMs);
  }
  // Then attempt a flush. Ordered after the telemetry upload so the live feed
  // keeps priority, and rate-limited to one attempt per cycle.
  servicePendingLap();
  // Only replay buffered positions once the link is genuinely working again.
  if (success) serviceTrackFlush();
  // Pace the next cycle to 1 Hz. The blocking AT calls above determine the
  // real floor, so if a cycle overran, this returns immediately.
  unsigned long spent = millis() - cycleStarted;
  if (spent < UPDATE_INTERVAL) delay(UPDATE_INTERVAL - spent);
}

