#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>

using namespace std;
int main(){

    // init
    // https://learn.microsoft.com/en-us/windows/win32/winsock/initializing-winsock
    WSADATA data{};
    int result = WSAStartup(MAKEWORD(2,2), &data);
    
    if(result != 0){
        cerr << "start fail: " << result << endl;
        return 1;
    }

    cout << "start success " << endl;

    WSACleanup();

    return 0;

}