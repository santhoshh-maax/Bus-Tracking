#define MODEM_RX 18
#define MODEM_TX 17
#define MODEM_POWKEY 10

String gpsData = "";

String sendAT(String cmd, int waitTime = 2000){
  String responce = "";
  Serial2.println(cmd);


unsigned long start = millis();
while(millis() - start < waitTime){
  while(Serial2.available()){
    char c = Serial2.read();
    responce += c; 
  }
  if(responce.indexof("OK") != -1 || responce.indexof("ERROR") != -1 || responce.indexof("DOWNLOAD") != -1) break;
} 
Serial.println(">>" + cmd + " [RESP]: " + responce);
return responce;
}

void setup(){
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL8N1, MODEM_RX, MODEM_TX);


  pinMode(MODEM_POWKEY, OUTPUT);
  digitalWrite(MODEM_POWKEY, LOW;
  delay(8000);

  Serial.println("starting");
  sendAT("AT", 1000);
  sendAT("AT+CPIN?", 1000);

  sendAT("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\"",1000);
  sendAT("AT+CGACT=1,1",1000);

  sendAT("AT+HTTPINT", 500);
  sendAT("")
  
}