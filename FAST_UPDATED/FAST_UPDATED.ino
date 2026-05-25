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
    if (response.indexOf("OK") != -1 || response.indexOf("ERROR") != -1 || response.indexOf("DOWNLOAD") != -1) break;
  }
  Serial.println(">> " + cmd + " [RESP]: " + response);
  return response;
}

// Automatically checks and heals network links mid-route for ANY SIM card
bool checkAndRecoverNetwork() {
  String cpinCheck = sendAT("AT+CPIN?", 1000);
  if (cpinCheck.indexOf("READY") == -1) {
    simStatus = "FAIL";
    Serial.println("⚠️ SIM Card missing or locked! Retrying...");
    return false;
  }
  simStatus = "OK";

  String cregCheck = sendAT("AT+CREG?", 1000);
  if (cregCheck.indexOf(",1") == -1 && cregCheck.indexOf(",5") == -1) {
    netStatus = "FAIL";
    Serial.println("📡 Lost Network Registration. Executing Deep Recovery...");
    
    sendAT("AT+COPS=0", 2000);   // Force modem to scan networks automatically
    sendAT("AT+CGATT=1", 2000);   // Re-attach core network packet services
    sendAT("AT+CGACT=1,1", 2000); // Re-activate standard data context
    
    delay(2000);
    
    cregCheck = sendAT("AT+CREG?", 1000);
    if (cregCheck.indexOf(",1") == -1 && cregCheck.indexOf(",5") == -1) {
      return false; 
    }
  }
  netStatus = "OK";
  return true;
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

  // Universal data attachment (Works across carriers seamlessly)
  sendAT("AT+CGATT=1", 2000);
  sendAT("AT+CGACT=1,1", 2000);
  
  sendAT("AT+HTTPINIT", 500);
  sendAT("AT+HTTPPARA=\"CID\",1", 500);
  sendAT("AT+HTTPPARA=\"SSLCFG\",0", 500);
  sendAT("AT+HTTPTERM", 500); 

  sendAT("AT+CGNSSPWR=1", 1000);
  Serial.println("✅ Setup Done");
}

void loop() {
  // Always check network integrity before moving forward
  if (!checkAndRecoverNetwork()) {
    Serial.println("🛑 Network unready. Retrying link layer...");
    delay(4000); 
    return;      
  }

  // 1. Request raw GPS data
  Serial2.println("AT+CGPSINFO");
  delay(1000); 

  gpsData = "";
  while (Serial2.available()) gpsData += char(Serial2.read());
  Serial.println("Raw GPS Response: " + gpsData);
  // 🔥 STRICT CHECK: If no satellite lock or data is corrupted, drop this cycle completely!
  if (gpsData.indexOf(",,,,,,,,") != -1 || gpsData.indexOf("ERROR") != -1) {
    Serial.println("⏳ Waiting for a true GPS satellite lock. Will NOT send 0 data to cloud.");
    delay(2000);
    return;  // Exits loop here; prevents any Firebase transmission
  }

  // Parse valid GPS fields
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

  // Translate NMEA layout to geometric decimal formats
  float latitude = latStr.substring(0, 2).toFloat() + latStr.substring(2).toFloat() / 60.0;
  float longitude = lonStr.substring(0, 3).toFloat() + lonStr.substring(3).toFloat() / 60.0;
  if (latDir == "S") latitude *= -1;
  if (lonDir == "W") longitude *= -1;
  gpsStatus = "RECEIVED";

  Serial.println("📍 Valid Lock Verified: " + String(latitude, 6) + "," + String(longitude, 6));

  // Package the JSON string (Fixed structural colon layout)
  String json = "{";
  json += "\"lat\":" + String(latitude, 6) + ",";
  json += "\"lon\":" + String(longitude, 6) + ",";
  json += "\"sim\":\"" + simStatus + "\",";
  json += "\"net\":\"" + netStatus + "\",";
  json += "\"gps\":\"" + gpsStatus + "\"";
  json += "}";
  
  String url = "https://bus-tracking-eae81-default-rtdb.asia-southeast1.firebasedatabase.app/gps.json";

  // --- RELIABLE HTTP DATA UPLOAD ---
  String initRes = sendAT("AT+HTTPINIT", 500);
  if (initRes.indexOf("ERROR") != -1) {
    sendAT("AT+HTTPTERM", 500);
    sendAT("AT+HTTPINIT", 500);
  }

  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 300);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 300);

  // Handshake sequence
  Serial2.println("AT+HTTPDATA=" + String(json.length()) + ",2000");
  delay(200); 
  Serial2.print(json);
  delay(100);

  // Execute transmission request
  Serial2.println("AT+HTTPACTION=1"); 
  
  String actionResult = "";
  unsigned long startWait = millis();
  bool success = false;

  // Track the transaction endpoint
  while (millis() - startWait < 5000) {
    if (Serial2.available()) {
      char c = Serial2.read();
      actionResult += c;
      if (actionResult.indexOf("+HTTPACTION: 1,200") != -1) {
        success = true;
        break;
      }
      if (actionResult.indexOf("+HTTPACTION: 1,") != -1 && actionResult.indexOf(",200") == -1) {
        break;
      }
    }
  }

  sendAT("AT+HTTPTERM", 300);

  if (success) {
    Serial.println("✅ Firebase UPDATE SUCCESS");
  } else {
    Serial.println("❌ Firebase UPDATE FAILED");
  }

  Serial.println("-------------------------");
  delay(3000); 
}