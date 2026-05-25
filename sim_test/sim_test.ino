#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10

// Helper function to send commands and print responses immediately
void sendCommand(String cmd, int waitTime = 2000) {
  Serial.println("\n--- Sending: " + cmd + " ---");
  Serial2.println(cmd);
  
  unsigned long start = millis();
  while (millis() - start < waitTime) {
    while (Serial2.available()) {
      char c = Serial2.read();
      Serial.write(c);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  // Turn on the A7672S module
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  
  Serial.println("🔄 Waking up the 4G Module... Please wait 10 seconds...");
  delay(10000); 

  Serial.println("\n=================================");
  Serial.println("      SIM & NETWORK TESTER       ");
  Serial.println("=================================");

  // 1. Basic communication test
  sendCommand("AT", 1000);

  // 2. Check if the SIM card is physically detected
  sendCommand("AT+CPIN?", 2000);

  // 3. Check Signal Strength (Excellent = 20-31, Poor = under 10)
  sendCommand("AT+CSQ", 1000);

  // 4. Check Network Registration Status
  sendCommand("AT+CREG?", 2000);

  // 5. Check GPRS Network Attachment Status
  sendCommand("AT+CGATT?", 1000);
}

void loop() {
  // Keeps the test running sequentially; nothing needed in the loop
}