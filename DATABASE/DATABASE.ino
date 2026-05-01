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

  delay(8000);

  Serial.println("🚀 Initializing...");

  sendAT("AT");
  sendAT("AT+CPIN?");

  // Network
  sendAT("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\"");
  sendAT("AT+CGATT=1");
  sendAT("AT+CGACT=1,1");

  // 🔥 IMPORTANT: reset HTTP service
  sendAT("AT+HTTPTERM");   // ignore error if first time
  delay(1000);
  sendAT("AT+HTTPINIT");

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

  float latitude = latStr.substring(0, 2).toFloat() +
                   latStr.substring(2).toFloat() / 60.0;

  float longitude = lonStr.substring(0, 3).toFloat() +
                    lonStr.substring(3).toFloat() / 60.0;

  if (latDir == "S") latitude *= -1;
  if (lonDir == "W") longitude *= -1;

  Serial.println("📍 " + String(latitude,6) + "," + String(longitude,6));

  // JSON
  String json = "{\"lat\":" + String(latitude,6) +
                ",\"lon\":" + String(longitude,6) + "}";

  String url = "http://bus-tracking-eae81-default-rtdb.asia-southeast1.firebasedatabase.app/gps.json";

  // 🔥 HTTP FLOW
  sendAT("AT+HTTPINIT");  // must call every cycle

  sendAT("AT+HTTPPARA=\"CID\",1");
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"");
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

  Serial2.println("AT+HTTPDATA=" + String(json.length()) + ",5000");
  delay(2000);

  Serial2.print(json);
  delay(2000);

  String response = sendAT("AT+HTTPACTION=1", 6000);

  sendAT("AT+HTTPREAD");
  sendAT("AT+HTTPTERM");

  if (response.indexOf("+HTTPACTION: 1,200") != -1) {
    Serial.println("✅ Firebase UPDATE SUCCESS");
  } else {
    Serial.println("❌ Firebase UPDATE FAILED");
  }

  Serial.println("-------------------------");

  delay(10000);
}