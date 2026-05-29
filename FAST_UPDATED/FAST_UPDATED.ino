#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10
String gpsData = "";
String simStatus = "UNKNOWN";
String netStatus = "UNKNOWN";
String gpsStatus = "WAITING";
String signalStrengthStr = "UNKNOWN";
float lastLat = 0.0;
float lastLon = 0.0;
float lastUploadedLat = 0.0;
float lastUploadedLon = 0.0;
bool internetReady = false;
bool uploadInProgress = false;
unsigned long lastUploadTime = 0;
int uploadFailCount = 0;
String sendAT(String cmd, int waitTime = 3000) {

  String response = "";

  Serial2.println(cmd);

  unsigned long start = millis();

  while (millis() - start < waitTime) {

    while (Serial2.available()) {

      char c = Serial2.read();

      response += c;
    }

    if (response.indexOf("OK") != -1 ||
        response.indexOf("ERROR") != -1 ||
        response.indexOf("DOWNLOAD") != -1) {
      break;
    }
  }

  Serial.println(">> " + cmd + " [RESP]: " + response);

  return response;
}
bool recoverInternet() {

  Serial.println("🔄 FULL INTERNET RECOVERY");

  sendAT("AT+HTTPTERM", 3000);

  sendAT("AT+NETCLOSE", 5000);

  delay(2000);

  sendAT("AT+CGATT=0", 5000);

  delay(3000);

  sendAT("AT+CGATT=1", 10000);

  delay(3000);

  String netOpen = sendAT("AT+NETOPEN", 15000);

  if (netOpen.indexOf("OK") == -1 &&
      netOpen.indexOf("+NETOPEN: 0") == -1) {

    Serial.println("❌ NETOPEN FAILED");

    return false;
  }

  delay(3000);

  sendAT("AT+HTTPTERM", 3000);

  String httpInit = sendAT("AT+HTTPINIT", 5000);

  if (httpInit.indexOf("OK") == -1) {

    Serial.println("❌ HTTPINIT FAILED");

    return false;
  }

  sendAT("AT+HTTPPARA=\"CID\",1", 3000);

  internetReady = true;

  Serial.println("✅ INTERNET RESTORED");

  return true;
}
bool checkAndRecoverNetwork() {

  String cpinCheck = sendAT("AT+CPIN?", 2000);

  if (cpinCheck.indexOf("READY") == -1) {

    simStatus = "FAIL";

    signalStrengthStr = "NO SIGNAL";

    Serial.println("⚠️ SIM FAILURE DETECTED");

    // ==================================================
    // TRY SIM RECOVERY
    // ==================================================

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

  if (cregCheck.indexOf(",1") == -1 &&
      cregCheck.indexOf(",5") == -1) {

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
bool uploadToFirebase(float latitude, float longitude) {

  if (uploadInProgress) {

    Serial.println("⚠️ Previous upload still active");

    return false;
  }

  uploadInProgress = true;

  // ==================================================
  // MOVEMENT FILTER
  // ==================================================

  // if (abs(latitude - lastUploadedLat) < 0.00003 &&
  //     abs(longitude - lastUploadedLon) < 0.00003) {

  //   Serial.println("🚌 Bus stationary - skipping upload");

  //   uploadInProgress = false;

  //   return true;
  // }

  String json = "{";

  json += "\"lat\":" + String(latitude, 6) + ",";
  json += "\"lon\":" + String(longitude, 6) + ",";
  json += "\"sim\":\"" + simStatus + "\",";
  json += "\"net\":\"" + netStatus + "\",";
  json += "\"gps\":\"" + gpsStatus + "\",";
  json += "\"signal\":\"" + signalStrengthStr + "\"";

  json += "}";

  String url =
  "https://bus-tracking-eae81-default-rtdb.asia-southeast1.firebasedatabase.app/gps.json";

  sendAT("AT+HTTPTERM", 2000);

  String initResp = sendAT("AT+HTTPINIT", 5000);

  if (initResp.indexOf("OK") == -1) {

    Serial.println("❌ HTTPINIT FAILED");

    uploadInProgress = false;

    return false;
  }

  sendAT("AT+HTTPPARA=\"CID\",1", 3000);

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

    sendAT("AT+HTTPTERM", 3000);

    uploadInProgress = false;

    return false;
  }

  delay(200);

  Serial2.print(json);

  delay(1000);

  Serial2.println();

  Serial.println(json);

  Serial.println(">> AT+HTTPACTION=1");

  Serial2.println("AT+HTTPACTION=1");

  String actionResult = "";

  bool success = false;

  unsigned long startWait = millis();

  while (millis() - startWait < 7000) {

    while (Serial2.available()) {

      char c = Serial2.read();

      actionResult += c;

      if (actionResult.indexOf(",200,") != -1 ||
          actionResult.indexOf(",201,") != -1) {

        success = true;

        break;
      }

      if (actionResult.indexOf(",400,") != -1 ||
          actionResult.indexOf(",500,") != -1) {

        break;
      }
    }

    if (success) break;
  }

  Serial.println("[ASYNC ACTION RESP]: " + actionResult);

  sendAT("AT+HTTPTERM", 3000);

  if (success) {

    Serial.println("✅ FIREBASE SUCCESS");

    uploadFailCount = 0;

    lastUploadTime = millis();

    lastUploadedLat = latitude;
    lastUploadedLon = longitude;

    uploadInProgress = false;

    return true;
  }

  Serial.println("❌ FIREBASE FAILED");

  uploadInProgress = false;

  return false;
}
void setup() {

  Serial.begin(115200);

  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  pinMode(MODEM_PWRKEY, OUTPUT);

  digitalWrite(MODEM_PWRKEY, LOW);

  delay(8000);

  Serial.println("🚀 BUS TRACKER STARTING");

  sendAT("AT", 2000);

  sendAT("AT+CPIN?", 2000);

  sendAT("AT+CGATT=1", 5000);

  sendAT("AT+NETOPEN", 10000);

  sendAT("AT+CGNSSPWR=1", 3000);

  sendAT("AT+CGNSSLOADAZ=1", 3000);

  recoverInternet();

  Serial.println("✅ SYSTEM READY");
}
void loop() {

  if (!checkAndRecoverNetwork()) {

    Serial.println("🛑 NETWORK UNAVAILABLE");

    delay(4000);

    return;
  }
  Serial2.println("AT+CGPSINFO");

  delay(800);

  gpsData = "";

  while (Serial2.available()) {

    gpsData += char(Serial2.read());
  }

  int startIdx = gpsData.indexOf(":");

  bool gpsValid = false;

  float latitude = lastLat;
  float longitude = lastLon;

  if (startIdx != -1) {

    String data = gpsData.substring(startIdx + 2);

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

        if (latDir == "S") latitude *= -1;

        if (lonDir == "W") longitude *= -1;

        if (latitude != 0.0 &&
            longitude != 0.0) {

          gpsValid = true;

          lastLat = latitude;
          lastLon = longitude;
        }
      }
    }
  }
  if (gpsValid) {

    gpsStatus = "RECEIVED";

    Serial.println(
      "📍 LIVE GPS: " +
      String(latitude, 6) + "," +
      String(longitude, 6)
    );
  }
  else {

    gpsStatus = "LAST_KNOWN";

    Serial.println(
      "⚠️ USING LAST KNOWN GPS: " +
      String(latitude, 6) + "," +
      String(longitude, 6)
    );
  }
  if (latitude == 0.0 || longitude == 0.0) {

    Serial.println("❌ NO VALID GPS AVAILABLE");

    Serial.println("⏳ WAITING FOR FIRST GPS FIX...");

    delay(3000);

    return;
}
  bool success = uploadToFirebase(latitude, longitude);

  if (!success) {

    uploadFailCount++;

    Serial.println("⚠️ Upload failed");

    recoverInternet();

    delay(2000);

    success = uploadToFirebase(latitude, longitude);

    if (!success) {

      Serial.println("❌ RETRY FAILED");
    }
  }

  Serial.println("----------------------------------------");
  delay(2000);
}

