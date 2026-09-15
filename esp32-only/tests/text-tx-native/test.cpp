#include <cassert>
#include <string>
#include <limits>
#include "../../firmware/esp32_pqc_demo/protocol.cpp"
namespace demo_transport {
std::string output;
bool live=true;
size_t limit=std::numeric_limits<size_t>::max(), calls=0;
uint32_t sendDelay=0;
Stream stream;
bool wifiMode(){return true;}
bool connected(){return live;}
void closeConnection(){live=false;}
Stream& io(){return stream;}
void flush(){}
size_t write(const uint8_t* p,size_t n){++calls;delay(sendDelay); n=std::min(n,limit);output.append(reinterpret_cast<const char*>(p),n);return n;}
}
int main(){
 using namespace demo_transport;
 assert(demo_protocol::writeFormatted("INFO proto=%d\n",4));
 assert(output=="INFO proto=4\n" && calls==1);
 output.clear();calls=0;limit=2;
 assert(demo_protocol::writeFormatted("OK %s\n","test"));
 assert(output=="OK test\n" && calls==4 && live);
 output.clear();limit=0;
 assert(!demo_protocol::writeFormatted("INFO\n"));
 assert(!live && output.empty() && nowMs>=2000);
 live=true;limit=512;calls=0;
 std::string oversized(512,'x');
 assert(!demo_protocol::writeFormatted("%s",oversized.c_str()));
 assert(!live && calls==0);
 live=true;output.clear();
 demo_protocol::initializeBootDiagnostics();
 demo_protocol::writeBootInfo();
 assert(output=="BOOT boot_id=00000000000000000000000000000000 reset_reason=1 reset_name=POWERON uptime_ms=1234\n");
 output.clear();calls=0;live=false;
 demo_protocol::writeBootInfo(true);
 assert(calls==0 && output.empty());
 live=true;limit=1;sendDelay=1000;nowMs=0;
 assert(!demo_protocol::writeFormatted("0123456789\n"));
 assert(!live && nowMs==5000 && output=="01234");
}
