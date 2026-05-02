#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10

String gpsData = "";

// Send AT command
String sendAT(String cmd, int waitTime = 2000) {
  String response = "";
  Serial.println(">> " + cmd);
  Serial2.println(cmd);
  delay(waitTime);

  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.write(c);
    response += c;
  }
  return response;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);

  delay(8000); // Wait for module to wake up

  Serial.println("🚀 Initializing...");

  sendAT("AT");
  sendAT("AT+CPIN?");

  // Network configuration
  sendAT("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\"");
  sendAT("AT+CGATT=1");
  sendAT("AT+CGACT=1,1");

  // Move static HTTP/SSL settings here to avoid "Already Set" errors in loop
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"CID\",1");
  sendAT("AT+HTTPPARA=\"SSLCFG\",0"); 
  sendAT("AT+HTTPTERM"); // Close it so the loop can open it cleanly

  // GPS
  sendAT("AT+CGNSSPWR=1");
  delay(5000);

  Serial.println("✅ Setup Done");
}

void loop() {
  Serial2.println("AT+CGPSINFO");
  delay(3000);

  gpsData = "";
  while (Serial2.available()) {
    gpsData += char(Serial2.read());
  }

  Serial.println("Raw: " + gpsData);

  if (gpsData.indexOf(",,,,,,,,") != -1 || gpsData.indexOf("ERROR") != -1) {
    Serial.println("⏳ Waiting for GPS fix...");
    delay(5000);
    return;
  }

  // Parse GPS coordinates
  int start = gpsData.indexOf(":");
  if (start == -1) return;
  String data = gpsData.substring(start + 2);
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

  Serial.println("📍 " + String(latitude, 6) + "," + String(longitude, 6));

  // JSON and URL for Firebase
  String json = "{\"lat\":" + String(latitude, 6) + ",\"lon\":" + String(longitude, 6) + "}";
  String url = "https://bus-tracking-eae81-default-rtdb.asia-southeast1.firebasedatabase.app/gps.json";

  // --- CLEAN HTTP FLOW ---
  
  // 1. Check if already initialized, if so, terminate and restart[cite: 2]
  String initRes = sendAT("AT+HTTPINIT");
  if (initRes.indexOf("ERROR") != -1) {
    sendAT("AT+HTTPTERM");
    delay(200);
    sendAT("AT+HTTPINIT");
  }

  // 2. Set dynamic parameters
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"");
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

  // 3. Handshake for data transmission[cite: 1]
  Serial2.println("AT+HTTPDATA=" + String(json.length()) + ",5000");
  delay(1000); // Wait for "DOWNLOAD" response
  Serial2.print(json);
  delay(500);

  // 4. Execute POST[cite: 1]
  String response = sendAT("AT+HTTPACTION=1", 6000);

  // 5. Success check and buffer reading[cite: 2]
  if (response.indexOf("+HTTPACTION: 1,200") != -1) {
    Serial.println("✅ Firebase UPDATE SUCCESS");
    delay(500); // Small delay to let read buffer populate[cite: 2]
    sendAT("AT+HTTPREAD");
  } else {
    Serial.println("❌ Firebase UPDATE FAILED");
  }

  // 6. Mandatory cleanup[cite: 1]
  sendAT("AT+HTTPTERM");

  Serial.println("-------------------------");
  delay(5000);
}