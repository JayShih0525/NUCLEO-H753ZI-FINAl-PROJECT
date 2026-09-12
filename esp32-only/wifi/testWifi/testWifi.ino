#include <WiFi.h>

const char* ssid = "12345678";
const char* password = "12345678";

void setup() { 
  Serial.begin(921600);
  delay(10);

  Serial.println();
  Serial.print("connect to: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password); // try to connects

  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("success connected");
  Serial.println("ip address: ");
  Serial.println(WiFi.localIP()); // get the local IP where esp32 

}

void loop() {
  // put your main code here, to run repeatedly:
  delay(500);
}
