#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_PWRKEY 10

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("STARTED");   // 🔥 check this first

  Serial2.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);

  delay(8000);

  Serial.println("READY - Type AT");
}

void loop() {
  if (Serial.available()) {
    Serial2.write(Serial.read());
  }

  if (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}