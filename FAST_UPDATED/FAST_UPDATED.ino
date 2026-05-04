#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10

String gpsData = "";
String simStatus = "UNKNOWN";
String netStatus = "UNKNOWN";
String gpsStatus = "WAITING";
unsigned long lastStatusCheck = 0;

// Send AT command with intelligent wait for completion strings
String sendAT(String cmd, int waitTime = 2000) { 
  String response = "";
  Serial2.println(cmd);
  
  unsigned long start = millis();
  while (millis() - start < waitTime) {
    while (Serial2.available()) {
      char c = Serial2.read();
      response += c;
    }
    // Break early if we get a definitive response to save time
    if (response.indexOf("OK") != -1 || response.indexOf("ERROR") != -1 || response.indexOf("DOWNLOAD") != -1) break;
  }
  Serial.println(">> " + cmd + " [RESP]: " + response);
  return response;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(8000); 

  Serial.println("🚀 Initializing...");
  sendAT("AT", 1000);
  sendAT("AT+CPIN?", 1000);

  // Network configuration
  sendAT("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\"", 1000);
  sendAT("AT+CGACT=1,1", 1000);
  
  // Set static parameters once to avoid redundancy
  sendAT("AT+HTTPINIT", 500);
  sendAT("AT+HTTPPARA=\"CID\",1", 500);
  sendAT("AT+HTTPPARA=\"SSLCFG\",0", 500);
  sendAT("AT+HTTPTERM", 500); 

  sendAT("AT+CGNSSPWR=1", 1000);
  Serial.println("✅ Setup Done");
}

void loop() {

    if (millis() - lastStatusCheck > 10000) {

    String simRes = sendAT("AT+CPIN?", 1000);
    simStatus = (simRes.indexOf("READY") != -1) ? "OK" : "FAIL";

    String netRes = sendAT("AT+CGATT?", 1000);
    netStatus = (netRes.indexOf(": 1") != -1) ? "OK" : "FAIL";

    lastStatusCheck = millis();
  }
  // 1. Get GPS Info[cite: 2]
  Serial2.println("AT+CGPSINFO");
  delay(1000); 

  gpsData = "";
  while (Serial2.available()) gpsData += char(Serial2.read());

  if (gpsData.indexOf(",,,,,,,,") != -1 || gpsData.indexOf("ERROR") != -1) {
    Serial.println("⏳ Waiting for GPS fix...");
    gpsStatus = "WAITING"; 
    delay(2000);
    return;
  }

  // Parse GPS coordinates[cite: 2]
  int startIdx = gpsData.indexOf(":");
  if (startIdx == -1) return;
  String data = gpsData.substring(startIdx + 2);
  
  int c1 = data.indexOf(',');
  int c2 = data.indexOf(',', c1 + 1);
  int c3 = data.indexOf(',', c2 + 1);
  int c4 = data.indexOf(',', c3 + 1);

  String latStr = data.substring(0, c1);
  String latDir = data.substring(c1 + 1, c2);
  String lonStr = data.substring(c2 + 1, c3);
  String lonDir = data.substring(c3 + 1, c4);

  float latitude = latStr.substring(0, 2).toFloat() + latStr.substring(2).toFloat() / 60.0;
  float longitude = lonStr.substring(0, 3).toFloat() + lonStr.substring(3).toFloat() / 60.0;
  if (latDir == "S") latitude *= -1;
  if (lonDir == "W") longitude *= -1;
  gpsStatus = "RECEIVED";

  Serial.println("📍 " + String(latitude, 6) + "," + String(longitude, 6));

  String json = "{";
json += "\"lat\":" + String(latitude, 6) + ",";
json += "\"lon\":" + String(longitude, 6) + ",";
json += "\"sim\":\"" + simStatus + "\",";
json += "\"net\":\"" + netStatus + "\",";
json += "\"gps\":\"" + gpsStatus + "\"";
json += "}";
  String url = "https://bus-tracking-eae81-default-rtdb.asia-southeast1.firebasedatabase.app/gps.json";

  // --- RELIABLE HTTP FLOW ---
  
  // 1. Initialize session
  String initRes = sendAT("AT+HTTPINIT", 500);
  if (initRes.indexOf("ERROR") != -1) {
    sendAT("AT+HTTPTERM", 500);
    sendAT("AT+HTTPINIT", 500);
  }

  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 300);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 300);

  // 2. Send Data with proper handshake[cite: 1]
  Serial2.println("AT+HTTPDATA=" + String(json.length()) + ",2000");
  delay(200); // Wait for DOWNLOAD prompt
  Serial2.print(json);
  delay(100);

  // 3. Execute and wait specifically for the Network Result Code[cite: 1, 2]
  Serial2.println("AT+HTTPACTION=1"); 
  
  String actionResult = "";
  unsigned long startWait = millis();
  bool success = false;

  // Wait up to 5 seconds for the +HTTPACTION confirmation[cite: 2]
  while (millis() - startWait < 5000) {
    if (Serial2.available()) {
      char c = Serial2.read();
      actionResult += c;
      if (actionResult.indexOf("+HTTPACTION: 1,200") != -1) {
        success = true;
        break; 
      }
      if (actionResult.indexOf("+HTTPACTION: 1,") != -1 && actionResult.indexOf(",200") == -1) {
        break; // Stop if we get a failure code (e.g., 601, 404)[cite: 1]
      }
    }
  }

  // 4. Terminate session[cite: 1]
  sendAT("AT+HTTPTERM", 300);

  if (success) {
    Serial.println("✅ Firebase UPDATE SUCCESS");
  } else {
    Serial.println("❌ Firebase UPDATE FAILED");
  }

  Serial.println("-------------------------");
  
  // Adjusted delay for reliability; total cycle will be roughly 3-5 seconds[cite: 2]
  delay(3000); 
}