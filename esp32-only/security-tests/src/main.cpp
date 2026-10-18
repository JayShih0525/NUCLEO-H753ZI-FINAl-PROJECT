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
    
    addrinfo hints{};
    hints.ai_family = AF_INET; // ipv4
    hints.ai_socktype = SOCK_STREAM; // 位元組串流, DGRAM FOR UDP  
    hints.ai_protocol = IPPROTO_TCP; // ip protocol tcp

    addrinfo* addresses = nullptr; 

    int lookup_result = getaddrinfo("192.168.1.104", "9000", &hints, &addresses); // return 0 if find this

    if(lookup_result != 0){
        cerr << "getaddrinfo error" << lookup_result << endl;
        WSACleanup(); //still need cleanm the WSA if get exception out
        return 1;
    }
    
    // create the socket
    SOCKET client = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    
    if(client == INVALID_SOCKET){
        int err = WSAGetLastError();
        cerr << "Socket failed: " << err << endl;
        freeaddrinfo(addresses);
        WSACleanup();
        return 1;
    }

    cout << "start success " << endl;


    freeaddrinfo(addresses);
    WSACleanup();

    return 0;

}