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
 assert(!MDNS.running); // Never advertise before persistent identity is ready.
 advertiseIdentity("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
 serviceNetwork(); assert(MDNS.running); assert(MDNS.starts==1);
 serviceNetwork(); assert(MDNS.starts==1);
 assert(testPowerSave==WIFI_PS_NONE); assert(powerSaveQueries==1);
 serviceNetwork(); assert(powerSaveQueries==1); // No per-frame polling.
 assert(acceptConnection()); assert(connected());
 WiFi.state=0; WiFi.callback(5,{{201}}); assert(!connected());
 serviceNetwork(); assert(!serverRunning); assert(!client.live);
 assert(!MDNS.running);
 WiFi.state=WL_CONNECTED; serviceNetwork(); assert(serverRunning); assert(acceptConnection());
 assert(MDNS.running); assert(MDNS.starts==2);
 assert(retryDelay==1000); assert(connected());
 // Even a fast disconnect/reconnect must invalidate the old TCP session.
 WiFi.callback(5,{{202}}); assert(!connected()); serviceNetwork(); assert(!client.live);
 assert(acceptConnection()); assert(connected());
 // Host disappears without FIN: stale client expires, a fresh one is accepted.
 delay(29999); assert(connected());
 delay(1); assert(!connected()); assert(!client.live);
 assert(acceptConnection()); assert(connected());
 delay(20000); commandBegin("INFO"); commandEnd();
 delay(20000); assert(connected()); // Active commands refresh the lease.
 uint8_t payload[20]={};
 assert(write(payload,20)==20);
 sendResult=-1;sendError=EAGAIN;
 const auto beforeSend=millis();
 assert(write(payload,20)==0 && connected() && millis()==beforeSend);
 sendResult=3;assert(write(payload,20)==3);
 sendResult=-1;sendError=ECONNRESET;assert(write(payload,20)==0 && !client.live);
 sendResult=-2;sendError=0;assert(acceptConnection());
 // Association alone is not sufficient: DHCP may still have no address.
 WiFi.ip=0; WiFi.callback(5,{{2}}); serviceNetwork();
 assert(!serverRunning); assert(!connected()); assert(!acceptConnection());
 const int before=WiFi.retries;
 delay(41000); serviceNetwork(); assert(WiFi.retries>before);
 WiFi.ip=1; serviceNetwork(); assert(serverRunning);
 assert(acceptConnection()); assert(connected());
 stopDiscovery(); MDNS.succeed=false; serviceNetwork(); assert(!MDNS.running);
 const int attempts=MDNS.starts; serviceNetwork(); assert(MDNS.starts==attempts);
 MDNS.succeed=true; delay(10000); serviceNetwork(); assert(MDNS.running);
}
