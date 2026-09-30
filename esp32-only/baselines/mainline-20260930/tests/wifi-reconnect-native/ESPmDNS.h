#pragma once
struct MDNSStub {
  bool running=false, succeed=true;
  int starts=0, stops=0;
  bool begin(const char*) { ++starts; running=succeed; return succeed; }
  void end() { ++stops; running=false; }
  void addService(const char*,const char*,int) {}
  bool addServiceTxt(const char*,const char*,const char*,const char*) { return running; }
};
inline MDNSStub MDNS;
