#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10

String gpsData = "";
String simStatus = "UNKNOWN";
String netStatus = "UNKNOWN";
String gpsStatus = "WAITING";
String signalStrengthStr = "UNKNOWN"; // 📶 Global variable to hold your human-readable signal text
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

// Deep cellular recovery engine for high-mobility dead zones with Signal Strength Tracking
bool checkAndRecoverNetwork() {
  String cpinCheck = sendAT("AT+CPIN?", 1000);
  if (cpinCheck.indexOf("READY") == -1) {
    simStatus = "FAIL";
    signalStrengthStr = "NO SIGNAL";
    Serial.println("⚠️ SIM Card missing or disconnected! Retrying power layers...");
    return false;
  }
  simStatus = "OK";

  // 📶 FETCH SIGNAL STRENGTH (CSQ)
  String csqCheck = sendAT("AT+CSQ", 500);
  int csqIdx = csqCheck.indexOf("+CSQ:");
  if (csqIdx != -1) {
    String rssiRaw = csqCheck.substring(csqIdx + 5, csqCheck.indexOf(",", csqIdx));
    rssiRaw.trim();
    int rssiVal = rssiRaw.toInt();
    
    // Evaluate and assign the text to our global variable for Firebase
    if (rssiVal == 99) {
      signalStrengthStr = "NO SIGNAL";
    } else if (rssiVal < 10) {
      signalStrengthStr = "POOR";
    } else if (rssiVal < 15) {
      signalStrengthStr = "FAIR";
    } else {
      signalStrengthStr = "GOOD"; // Captures both Good and Excellent ranges cleanly
    }
    
    Serial.println("📶 Cellular Signal Strength Evaluated: " + signalStrengthStr + " (" + rssiRaw + ")");
  } else {
    signalStrengthStr = "NO SIGNAL";
  }

  String cregCheck = sendAT("AT+CREG?", 1000);
  if (cregCheck.indexOf(",1") == -1 && cregCheck.indexOf(",5") == -1) {
    netStatus = "FAIL";
    signalStrengthStr = "NO SIGNAL";
    Serial.println("📡 Bus in Network Dead Zone. Executing Multi-Stage Link Recovery...");
    
    sendAT("AT+HTTPTERM", 500); 
    sendAT("AT+CGATT=0", 1000);  
    delay(1000);
    
    sendAT("AT+COPS=0", 2000);   
    sendAT("AT+CGATT=1", 2000);  
    sendAT("AT+CGACT=1,1", 2000); 
    
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

  Serial.println("🚀 Bus Tracker Initializing...");
  sendAT("AT", 1000);
  sendAT("AT+CPIN?", 1000);

  sendAT("AT+CGATT=1", 2000);
  sendAT("AT+CGACT=1,1", 2000);
  
  sendAT("AT+HTTPTERM", 500); 
  sendAT("AT+HTTPINIT", 500);
  sendAT("AT+HTTPPARA=\"CID\",1", 500);
  sendAT("AT+HTTPPARA=\"SSLCFG\",0", 500);
  sendAT("AT+HTTPTERM", 500); 

  sendAT("AT+CGNSSPWR=1", 1000);
  Serial.println("✅ Setup Sequences Completed");
}

void loop() {
  if (!checkAndRecoverNetwork()) {
    Serial.println("🛑 Cell Tower unreachable. Backing off for 4 seconds...");
    delay(4000); 
    return;      
  }

  // Request fresh GPS data
  Serial2.println("AT+CGPSINFO");
  delay(600); 

  gpsData = "";
  while (Serial2.available()) gpsData += char(Serial2.read());

  if (gpsData.indexOf(",,,,,,,,") != -1 || gpsData.indexOf("ERROR") != -1) {
    Serial.println("⏳ Waiting for true GPS Satellite fix. Skipping upload batch...");
    delay(2000);
    return;  
  }

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

  Serial.println("📍 Active Location Core Calculated: " + String(latitude, 6) + "," + String(longitude, 6));

  // 🛠️ UPDATED JSON payload to dynamically include the signal text field
  String json = "{";
  json += "\"lat\":" + String(latitude, 6) + ",";
  json += "\"lon\":" + String(longitude, 6) + ",";
  json += "\"sim\":\"" + simStatus + "\",";
  json += "\"net\":\"" + netStatus + "\",";
  json += "\"gps\":\"" + gpsStatus + "\",";
  json += "\"signal\":\"" + signalStrengthStr + "\""; // 📶 Appears as "POOR", "FAIR", or "GOOD" in Firebase
  json += "}";
  
  String url = "https://bus-tracking-eae81-default-rtdb.asia-southeast1.firebasedatabase.app/gps.json";

  // --- FAULT-RESISTANT HTTP TRANSACTION ENGINE ---
  Serial2.println("AT+HTTPINIT"); 
  delay(150); 
  while(Serial2.available()) Serial.write(Serial2.read()); 

  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 200);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 200);

  // Data payload handshake allocation
  Serial2.println("AT+HTTPDATA=" + String(json.length()) + ",2000"); 
  delay(200); 
  Serial2.print(json);
  delay(150);
  while(Serial2.available()) Serial.write(Serial2.read()); 

  // Execute actual post transmission
  Serial.println(">> AT+HTTPACTION=1");
  Serial2.println("AT+HTTPACTION=1"); 
  
  String actionResult = "";
  unsigned long startWait = millis();
  bool success = false;

  while (millis() - startWait < 3500) { 
    if (Serial2.available()) {
      char c = Serial2.read();
      actionResult += c;
      
      if (actionResult.indexOf("+HTTPACTION:") != -1 && actionResult.indexOf("\n", actionResult.indexOf("+HTTPACTION:")) == -1) {
        delay(50); 
        while (Serial2.available()) {
          actionResult += char(Serial2.read());
        }
      }

      if (actionResult.indexOf(",200,") != -1 || actionResult.indexOf(",201,") != -1) {
        success = true;
        break;
      }
      
      if (actionResult.indexOf("+HTTPACTION:") != -1 && actionResult.indexOf(",200,") == -1 && actionResult.indexOf(",201,") == -1 && actionResult.endsWith("\n")) {
        break; 
      }
    }
  }
  Serial.println("[ASYNC ACTION RESP]: " + actionResult);

  sendAT("AT+HTTPTERM", 300);

  if (success) {
    Serial.println("✅ Firebase Real-Time Update: SUCCESS");
  } else {
    Serial.println("❌ Firebase Real-Time Update: RESPONSE ERROR OR TIMEOUT");
  }

  Serial.println("----------------------------------------");
  delay(2000); 
}