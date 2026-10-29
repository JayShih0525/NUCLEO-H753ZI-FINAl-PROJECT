#ifndef TCP_CLIENT
#define TCP_CLIENT

#include <winsock2.h>
#include <ws2tcpip.h>


#include <string>
#include <chrono>

using namespace std;

bool send_all(SOCKET client, const string& message);
string recv_by_line(SOCKET client, string &pending, string &state, chrono::steady_clock::time_point deadline);

#endif