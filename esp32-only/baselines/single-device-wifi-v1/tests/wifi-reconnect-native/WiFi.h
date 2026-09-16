#pragma once
#include "Arduino.h"
#include <functional>
#define WIFI_STA 1
#define WL_CONNECTED 3
#define ARDUINO_EVENT_WIFI_STA_DISCONNECTED 5
using WiFiEvent_t=int;
struct WiFiEventInfo_t { struct {unsigned int reason;} wifi_sta_disconnected; };
struct Address { uint32_t value=1; explicit operator uint32_t() const{return value;} Address toString(){return *this;} const char* c_str(){return "1.2.3.4";} };
struct WiFiStub {int state=0, retries=0; bool automatic=true; std::function<void(int,WiFiEventInfo_t)> callback;
 template<class T> void onEvent(T f,int){callback=f;} void mode(int){} void setAutoReconnect(bool b){automatic=b;}
 void begin(const char*,const char*){} int status(){return state;} void reconnect(){++retries;}
 uint32_t ip=1; int RSSI(){return -50;} Address localIP(){return {ip};}
}; inline WiFiStub WiFi;
class WiFiClient:public Stream {public: int fd(){return 7;} bool live=false; void stop(){live=false;} bool connected(){return live;} explicit operator bool(){return live;} void setNoDelay(bool){} void setTimeout(int){} Address remoteIP(){return {};} unsigned int remotePort(){return 1234;} };
class WiFiServer {public: bool running=false; explicit WiFiServer(int){} void begin(){running=true;} void end(){running=false;} WiFiClient available(){WiFiClient c;c.live=running;return c;} };
