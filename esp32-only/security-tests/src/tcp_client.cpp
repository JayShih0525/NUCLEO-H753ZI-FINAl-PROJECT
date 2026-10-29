
#include <iostream>

#include "tcp_client.h"

constexpr size_t max_line_bytes = 4096; // const expression

bool send_all(SOCKET client, const string& message){

    // the result return the bytes sent or receive if its work, OR SOCKET_ERROR
    int sent_bytes = 0;

    while(sent_bytes < static_cast<int>(message.size())){
        
        int send_result = send(client, message.data() + sent_bytes, static_cast<int>(message.size() - sent_bytes), 0);

        if(send_result == SOCKET_ERROR){
            int err = WSAGetLastError();
            cerr << "send failed: " << err << endl;
            return false;
        }else if(send_result == 0){
            cout << "stuck in send" << endl;
            return false;
        }

        sent_bytes += send_result;
    }
    cout << "Sent " << sent_bytes << " Bytes " << endl;

    return true;

    
}

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

        // set a reamining_time rotate to base on ms and let recv have time limit
        auto remaining_time = deadline - chrono::steady_clock::now();

        // if remaining_time less or equal 0, need return imme
        if(remaining_time <= chrono::steady_clock::duration::zero()){
            state = "timeout";
            return "";
        }
        
        DWORD remaining_ms = chrono::ceil<chrono::milliseconds>(remaining_time).count();

        int sockopt = setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&remaining_ms), static_cast<int>(sizeof(remaining_ms)));
        if(sockopt == SOCKET_ERROR){
    
            int err = WSAGetLastError();
            cerr << "set socket failed: " << err << endl;
            state = "err";
            return "";    
        }

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
