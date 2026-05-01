#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10

String gpsData = "";

void setup() {
  Serial.begin(115200);

  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  // Power ON modem
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);

  delay(8000);

  Serial.println("🚀 Starting GPS...");

  // ✅ Correct GPS power command for your firmware
  Serial2.println("AT+CGNSSPWR=1");
  delay(2000);
}

void loop() {
  // Request GPS data
  Serial2.println("AT+CGPSINFO");
  delay(2000);

  gpsData = "";

  // Read response
  while (Serial2.available()) {
    char c = Serial2.read();
    gpsData += c;
  }

  Serial.println("Raw: " + gpsData);

  // ❌ Ignore invalid or error responses
  if (gpsData.indexOf("ERROR") != -1 ||
      gpsData.indexOf(",,,,,,,,") != -1) {
    Serial.println("⚠️ Waiting for GPS fix...");
    Serial.println("-------------------------");
    delay(5000);
    return;
  }

  // Extract data
  int start = gpsData.indexOf(":");
  if (start != -1) {
    String data = gpsData.substring(start + 2);

    int comma1 = data.indexOf(',');
    int comma2 = data.indexOf(',', comma1 + 1);
    int comma3 = data.indexOf(',', comma2 + 1);
    int comma4 = data.indexOf(',', comma3 + 1);

    String lat = data.substring(0, comma1);
    String latDir = data.substring(comma1 + 1, comma2);
    String lon = data.substring(comma2 + 1, comma3);
    String lonDir = data.substring(comma3 + 1, comma4);

    // Ensure valid values
    if (lat.length() > 0 && lon.length() > 0) {

      // Convert Latitude
      float lat_deg = lat.substring(0, 2).toFloat();
      float lat_min = lat.substring(2).toFloat();
      float latitude = lat_deg + (lat_min / 60.0);

      // Convert Longitude
      float lon_deg = lon.substring(0, 3).toFloat();
      float lon_min = lon.substring(3).toFloat();
      float longitude = lon_deg + (lon_min / 60.0);

      // Apply direction
      if (latDir == "S") latitude *= -1;
      if (lonDir == "W") longitude *= -1;

      // ✅ Print clean output
      Serial.println("📍 Latitude : " + String(latitude, 6));
      Serial.println("📍 Longitude: " + String(longitude, 6));

      // 🌍 Google Maps link
      Serial.println("🌍 Map: https://maps.google.com/?q=" +
                     String(latitude, 6) + "," +
                     String(longitude, 6));

      Serial.println("-------------------------");
    }
  }

  delay(5000); // update every 5 sec
}