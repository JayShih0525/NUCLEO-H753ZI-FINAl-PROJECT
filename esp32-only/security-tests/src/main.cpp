#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

string recv_by_line(SOCKET client, string &pending);

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

    int lookup_result = getaddrinfo("192.168.1.104", "9000", &hints, &addresses); // return 0 if find this address

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
        }
        
        if(send_result == 0){
            cout << "stuck in send" << endl;
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
        }

        sent_bytes += send_result;
    }
    cout << "Sent " << sent_bytes << " Bytes " << endl;

    char recv_buff[1024];
    int recv_result;
    string pending, output;
    bool info = false;

    while(!info){

        recv_result = recv(client, recv_buff, static_cast<int>(sizeof(recv_buff)), 0);
        
        // the diff between cout and cout.wirte is the second one can exactly output the num u put in second element in the function.
        // cuz recv wont put \0 in the last element of input
        // OR use a string appended

        if(recv_result > 0) {
            pending.append(recv_buff, recv_result);

            size_t pos = pending.find('\n');
            if(pos != string::npos){ // mean its find the \n in pending, so need to output and ready to recv the next line
                output = pending.substr(0, pos);
                cout << output;
                pending.erase(0, pos+1);
                
                if(output.rfind("INFO ", 0) != string::npos) info = true; 
            }

        }else if(recv_result == 0){
            cout << "end of recv" << endl;  
        }else{
            int err = WSAGetLastError();
            cerr << "Receive failed: " << err << endl;
            freeaddrinfo(addresses);
            closesocket(client);
            WSACleanup();
            return 1;
        }
    }


    freeaddrinfo(addresses);
    closesocket(client);
    WSACleanup();

    return 0;

}


string recv_by_line(SOCKET client, string &pending){

    char buf[1024]; 
    int result = recv(client, buf, static_cast<int>(sizeof(buf)), 0);
    string temp;
    
    while(result > 0){
        
        temp.append(buf, sizeof(buf));
    }
}