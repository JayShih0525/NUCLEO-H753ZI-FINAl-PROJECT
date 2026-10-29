#include "tcp_client.h"

#include <iostream>

// g++ -std=c++17 -Wall -Wextra -g3 -Iinclude src/main.cpp src/tcp_client.cpp -o src/output/main.exe -lws2_32

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
        closesocket(client); 
        freeaddrinfo(addresses);
        WSACleanup();
        return 1;
    }

    cout << "Connected success" << endl; 

    // send
    string message = "CAMERA_CAPTURE_ENCRYPTED\n";

    if(!send_all(client, message)){
        freeaddrinfo(addresses);
        closesocket(client);
        WSACleanup();
        return 1;
    }

    string pending = "";
    bool info = false;
    string state = "";

    auto deadline = chrono::steady_clock::now() + chrono::seconds(10);

    while(!info){

        if(chrono::steady_clock::now() >= deadline){
            cerr << "deadline" << endl;
            return 1;
        }
        string temp = recv_by_line(client, pending, state, deadline);

        if(state == "End of recv"){ // mean that when not print INFO but stop the recv by server
            cout << "end recv before get INFO" << endl;
            return 1;
        }else if(state == "err"){
            return 1;
        }else if(state == "timeout"){
            cerr << "timeout for waiting" << endl;
            return 1;
            
        }else if(state == "too long"){
            
            cout << "The message out of the limit per line. " << endl;
            return 1;

        }else if(temp ==  "ERR HOST_AUTH_REQUIRED"){
            cout << "[PASS] Unauthenticated camera request rejected" << endl;
            info = true;
        }else cout << temp << endl;// normal output but not start with INFO

    }

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


    freeaddrinfo(addresses);
    closesocket(client);
    WSACleanup();
    
    return 0;

}
