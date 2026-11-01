#include "security_tests.h"
#include "tcp_client.h"

#include <iostream>

bool test_unauthenticated_camera(SOCKET client){

    // send
    string message = "CAMERA_CAPTURE_ENCRYPTED\n";

    if(!send_all(client, message)){
        return false;
    }

    string pending = "";
    string state = "";

    auto deadline = chrono::steady_clock::now() + chrono::seconds(10);

    while(1){

        if(chrono::steady_clock::now() >= deadline){
            cerr << "deadline" << endl;
            return false;
        }
        
        string temp = recv_by_line(client, pending, state, deadline);

        if(state == "End of recv"){ // mean that when not print INFO but stop the recv by server
            cout << "end recv before get INFO" << endl;
            return false;
        }else if(state == "err"){
            return false;
        }else if(state == "timeout"){
            cerr << "timeout for waiting" << endl;
            return false;
        }else if(state == "too long"){
            cout << "The message out of the limit per line. " << endl;
            return false;
        }else if(temp == "ERR HOST_AUTH_REQUIRED"){
            cout << "[PASS] Unauthenticated camera request rejected" << endl;
            return true;
        }else cout << temp << endl;// normal output but not start with INFO

    }

}