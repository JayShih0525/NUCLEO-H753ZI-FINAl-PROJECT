#include <cassert>
#define PQC_USE_WIFI true
#define PQC_WIFI_SSID "test"
#define PQC_WIFI_PASSWORD "test"
#define PQC_TCP_PORT 9000
#include "Arduino.h"
uint32_t clockMs=0;
#include "../../firmware/esp32_pqc_demo/transport.cpp"
int main(){
 using namespace demo_transport;
 assert(begin()); assert(!serverRunning); assert(!WiFi.automatic);
 delay(1000); serviceNetwork(); assert(WiFi.retries==1);
 for(int i=0;i<100;i++) serviceNetwork(); assert(WiFi.retries==1);
 delay(11000); serviceNetwork(); assert(WiFi.retries==2);
 assert(retryDelay==4000);
 WiFi.state=WL_CONNECTED; serviceNetwork(); assert(serverRunning);
 assert(acceptConnection()); assert(connected());
 WiFi.state=0; WiFi.callback(5,{{201}}); assert(!connected());
 serviceNetwork(); assert(!serverRunning); assert(!client.live);
 WiFi.state=WL_CONNECTED; serviceNetwork(); assert(serverRunning); assert(acceptConnection());
 assert(retryDelay==1000); assert(connected());
 // Even a fast disconnect/reconnect must invalidate the old TCP session.
 WiFi.callback(5,{{202}}); assert(!connected()); serviceNetwork(); assert(!client.live);
 assert(acceptConnection()); assert(connected());
 // Association alone is not sufficient: DHCP may still have no address.
 WiFi.ip=0; WiFi.callback(5,{{2}}); serviceNetwork();
 assert(!serverRunning); assert(!connected()); assert(!acceptConnection());
 const int before=WiFi.retries;
 delay(41000); serviceNetwork(); assert(WiFi.retries>before);
 WiFi.ip=1; serviceNetwork(); assert(serverRunning);
 assert(acceptConnection()); assert(connected());
}
