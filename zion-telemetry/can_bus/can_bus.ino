#include <SPI.h>
#include <mcp_can.h>

#define CAN_SCK   12
#define CAN_MISO  13
#define CAN_MOSI  14
#define CAN_CS     8
#define CAN_INT   16

MCP_CAN CAN0(CAN_CS);

void setup()
{
  Serial.begin(115200);
  delay(1000);

  SPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);

  Serial.println("Initializing MCP2515...");

  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK)
  {
    Serial.println("MCP2515 SUCCESS");
  }
  else
  {
    Serial.println("MCP2515 FAILED");
    while (1);
  }

  CAN0.setMode(MCP_NORMAL);

  pinMode(CAN_INT, INPUT);

  Serial.println("CAN listening...");
}

void loop()
{
  if (digitalRead(CAN_INT) == LOW)
  {
    unsigned long rxId;
    byte len;
    byte buf[8];

    if (CAN0.readMsgBuf(&rxId, &len, buf) == CAN_OK)
    {
      Serial.print("ID: 0x");
      Serial.print(rxId, HEX);

      Serial.print("  DLC: ");
      Serial.print(len);

      Serial.print("  DATA: ");

      for (byte i = 0; i < len; i++)
      {
        if (buf[i] < 0x10)
          Serial.print("0");

        Serial.print(buf[i], HEX);
        Serial.print(" ");
      }

      Serial.println();
    }
  }
}