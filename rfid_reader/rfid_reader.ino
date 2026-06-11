#include <SPI.h>
#include <MFRC522.h>

// ==========================
// RC522 PINS
// ==========================

#define SS_PIN   41
#define RST_PIN  38

#define SCK_PIN  42
#define MOSI_PIN 40
#define MISO_PIN 39

// ==========================
// RFID OBJECT
// ==========================

MFRC522 rfid(SS_PIN, RST_PIN);

// ==========================
// SETUP
// ==========================

void setup() {

  Serial.begin(115200);

  // ==========================
  // START SPI WITH CUSTOM PINS
  // SCK, MISO, MOSI, SS
  // ==========================

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);

  // Optional stability improvement
  SPI.setFrequency(1000000);

  // ==========================
  // INIT RC522
  // ==========================

  rfid.PCD_Init();

  Serial.println("=================================");
  Serial.println("RFID READER READY");
  Serial.println("Tap RFID Card...");
  Serial.println("=================================");
}

// ==========================
// LOOP
// ==========================

void loop() {

  // ==========================
  // DETECT CARD
  // ==========================

  if (!rfid.PICC_IsNewCardPresent())
    return;

  // ==========================
  // READ CARD SERIAL
  // ==========================

  if (!rfid.PICC_ReadCardSerial())
    return;

  Serial.println("\n=========================");
  Serial.println("RFID CARD DETECTED");
  Serial.println("=========================");

  // ==========================
  // PRINT UID
  // ==========================

  Serial.print("Card UID: ");

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (rfid.uid.uidByte[i] < 0x10)
      Serial.print("0");

    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }

  Serial.println();

  // ==========================
  // AUTHENTICATION
  // ==========================

  MFRC522::MIFARE_Key key;

  // Default key = FF FF FF FF FF FF

  for (byte i = 0; i < 6; i++) {

    key.keyByte[i] = 0xFF;
  }

  byte block = 4;

  MFRC522::StatusCode status;

  status = rfid.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A,
      block,
      &key,
      &(rfid.uid)
  );

  if (status != MFRC522::STATUS_OK) {

    Serial.print("Authentication failed: ");

    Serial.println(
      rfid.GetStatusCodeName(status)
    );

    return;
  }

  // ==========================
  // READ BLOCK
  // ==========================

  byte buffer[18];

  byte size = sizeof(buffer);

  status = rfid.MIFARE_Read(
      block,
      buffer,
      &size
  );

  if (status != MFRC522::STATUS_OK) {

    Serial.print("Read failed: ");

    Serial.println(
      rfid.GetStatusCodeName(status)
    );

    return;
  }

  // ==========================
  // PRINT STORED TEXT
  // ==========================

  Serial.print("Stored Text: ");

String numberOnly = "";

for (uint8_t i = 0; i < 16; i++) {

  char c = (char)buffer[i];

  // Keep ONLY numbers
  if (c >= '0' && c <= '9') {

    numberOnly += c;
  }
}

Serial.print("Stored Number: ");
Serial.println(numberOnly);
  Serial.println();

  // ==========================
  // STOP ENCRYPTION
  // ==========================

  rfid.PICC_HaltA();

  rfid.PCD_StopCrypto1();

  delay(1000);
}
