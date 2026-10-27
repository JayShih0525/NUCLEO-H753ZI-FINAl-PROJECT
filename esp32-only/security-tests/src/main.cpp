#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

using namespace std;

constexpr size_t max_line_bytes = 4096; // const expression

string recv_by_line(SOCKET client, string &pending, string &state, chrono::steady_clock::time_point deadline){

    char buf[1024]; 
    int result;
    state = "ok";

    size_t pos = pending.find('\n');
    
    while(pos == string::npos){

        // check the whold array not out of the limit max at first then keep recv to '\n'
        if(pending.size() > max_line_bytes){
            state = "too long";
            return "";
        }

        auto remaining_time = chrono::steady_clock::now() - deadline;
        chrono::duration_cast<chrono::milliseconds>(remaining_time);
        remaining_time.count();

        result = recv(client, buf, static_cast<int>(sizeof(buf)), 0);

        if(result > 0) pending.append(buf, result);
        else if(result == 0){
            state = "End of recv";
            return "";
        } else{

            int err = WSAGetLastError();
            cerr << "recv failed: " << err << endl;
            
            // if timeout make error, the err will be WSAT
            if(err == WSAETIMEDOUT){
                state = "timeout";
            }else{
                state = "err";
            }

            return "";
        }

        pos = pending.find('\n');
    }

    // check this line is not out of the max limit
    if(pos > max_line_bytes){
        state = "too long";
        return "";
    }

    string temp = pending.substr(0, pos);
    pending.erase(0, pos + 1);
    return temp;
        
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
        closesocket(client); 
        freeaddrinfo(addresses);
        WSACleanup();
        return 1;
    }

    cout << "Connected success" << endl; 


    // set socket option for if waiting the data too long

    // https://learn.microsoft.com/zh-tw/windows/win32/api/winsock/nf-winsock-setsockopt
    // https://learn.microsoft.com/zh-tw/windows/win32/winsock/sol-socket-socket-options

    DWORD timeout_ms = 10000;
    
    int option_result = setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), static_cast<int>(sizeof(timeout_ms)));
    // in setsockopt, if success return 0, else return SOCKET_ERROR

    if(option_result == SOCKET_ERROR){
        int err = WSAGetLastError();
        cerr << "Set socket failed: " << err << endl;
        
        freeaddrinfo(addresses);
        closesocket(client);
        WSACleanup();
        return 1;

    } // else if mean option set success, do not need extra control

    // send and receive 

    string message = "INFO\n";

    // the result return the bytes sent or receive if its work, OR SOCKET_ERROR
    int sent_bytes = 0;

    while(sent_bytes < static_cast<int>(message.size())){
        
        int send_result = send(client, message.data() + sent_bytes, static_cast<int>(message.size() - sent_bytes), 0);

        if(send_result == SOCKET_ERROR){
            int err = WSAGetLastError();
            cerr << "send failed: " << err << endl;
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
        }else if(send_result == 0){
            cout << "stuck in send" << endl;
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
        }

        sent_bytes += send_result;
    }
    cout << "Sent " << sent_bytes << " Bytes " << endl;

    string pending = "";
    bool info = false;
    string state = "";

    auto deadline = chrono::steady_clock::now() + chrono::seconds(10);

    while(!info){

        if(chrono::steady_clock::now() >= deadline){
            cerr << "timeout" << endl;
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
        }
        string temp = recv_by_line(client, pending, state, deadline);

        if(state == "End of recv"){ // mean that when not print INFO but stop the recv by server
            cout << "end recv before get INFO" << endl;
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
        }else if(state == "err"){
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
        }else if(state == "timeout"){
            
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
            
        }else if(state == "too long"){
            
            cout << "The message out of the limit per line. " << endl;
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;

        }else if(temp.rfind("INFO ", 0) == 0){
            cout << temp << endl;
            info = true;
        }else cout << temp << endl;// normal output but not start with INFO

    }

    freeaddrinfo(addresses);
    closesocket(client);
    WSACleanup();

    return 0;

}
