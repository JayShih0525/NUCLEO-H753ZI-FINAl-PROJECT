#include "tcp_client.h"
#include "security_tests.h"

#include <windows.h>
#include <bcrypt.h>

#include <iostream>
#include <fstream>

// g++ -std=c++17 -Wall -Wextra -g3 -Iinclude src/main.cpp src/tcp_client.cpp -o src/output/main.exe -lws2_32
// g++ -std=c++17 -Wall -Wextra -g3 -Iinclude src/main.cpp src/tcp_client.cpp src/security_tests.cpp -o src/output/main.exe -lws2_32 -lbcrypt
void clean( SOCKET client, addrinfo* addresses){
    closesocket(client); 
    freeaddrinfo(addresses);
    WSACleanup();
}

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

    int lookup_result = getaddrinfo("172.20.10.2", "9000", &hints, &addresses); // return 0 if find this address

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
    
    // connect
    int connect_result = connect(client, addresses->ai_addr, (int)addresses->ai_addrlen);
    
    // success == 0 ; err = SOCKET_ERROR
    // if connect failed, should close the socket first and make ur ConnectSocket be INVALID_SOCKET
    if(connect_result == SOCKET_ERROR){
        int err = WSAGetLastError();
        cerr << "Connect failed: " << err << endl;
        clean(client, addresses);
        return 1;
    }

    cout << "Connected success" << endl; 

    // if(!test_unauthenticated_camera(client)){
    //     clean(client, addresses);
    //     return 1;
    // }
    
    // // set socket option for if waiting the data too long

    // // https://learn.microsoft.com/zh-tw/windows/win32/api/winsock/nf-winsock-setsockopt
    // // https://learn.microsoft.com/zh-tw/windows/win32/winsock/sol-socket-socket-options

    // DWORD timeout_ms = 10000;
    
    // int option_result = setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), static_cast<int>(sizeof(timeout_ms)));
    // // in setsockopt, if success return 0, else return SOCKET_ERROR

    // if(option_result == SOCKET_ERROR){
    //     int err = WSAGetLastError();
    //     cerr << "Set socket failed: " << err << endl;
        
    //     freeaddrinfo(addresses);
    //     closesocket(client);
    //     WSACleanup();
    //     return 1;

    // } // else if mean option set success, do not need extra control

    if(!test_unknown_host(client)) {
        clean(client, addresses);
        return 1;
    }
    
    
    clean(client, addresses);
    return 0;

}


// 陌生 Host 金鑰：用未加入白名單的身分握手
// 控制資料篡改：修改命令內容，保留原 HMAC	拒絕，不執行命令
// 同 session 重播：重送已被接受的完整紀錄	拒絕重複序號	保存並重送原始紀錄
// 跨 session 重播