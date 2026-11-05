#include "security_tests.h"
#include "tcp_client.h"

#include <iostream>
#include <fstream>
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

bool test_unknown_host(SOCKET client){

    ifstream file(".test-data/host-b/fingerprint.bin", ios::binary);

    if(!file){
        cerr << "open file failed" << endl;
        return false;
    } 

    string fingerprint(32, '\0'); // a string of size 32, fill 0 at first
    if(!file.read(&fingerprint[0], 32)){
        cerr << "read file failed" << endl;
        return false;
    }
    
    string payload = fingerprint; // wait for combine

    // challenge num for everytime is new
    string nonce(32, '\0');


    // ref for generate the challenge
    // https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom
    NTSTATUS rng_result = BCryptGenRandom(nullptr, reinterpret_cast<unsigned char*>(nonce.data()), static_cast<ULONG>(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG); // (hAlgorithm, pbBuffer, cbBuffer, dwFlags)
    
    if(rng_result < 0){
        cerr << "BCryptGenRandom failed: " << rng_result << endl;
        return false;
    }

    // or its work
    
    payload.append(nonce);

    // 32 + 32 + 4 -> fingetprint + challenge + rekey
    
    payload.append("\x00\x00\x00\x0A", 4); // assume we use --rekey 10 always
    
    if(payload.size() != 68){
        cerr << "test size worng" << endl;
        return false;
    }

    if(!send_all(client, "MUTUAL_BEGIN\n")){
        return false;
    }
    
    if(!send_frame(client, payload)){
        return false;
    }

    
    // ready to send

    string pending = "";
    string state = "";

    auto deadline = chrono::steady_clock::now() + chrono::seconds(10);

    while(1){

        if(chrono::steady_clock::now() >= deadline){
            cerr << "deadline" << endl;
            return false;
        }
        
        string temp = recv_by_line(client, pending, state, deadline);

        if(state != "ok"){

            if(state == "End of recv"){ // mean that when not print INFO but stop the recv by server
                cout << "end recv early" << endl;
                return false;
            }else if(state == "err"){
                return false;
            }else if(state == "timeout"){
                cerr << "timeout for waiting" << endl;
                return false;
            }else if(state == "too long"){
                cout << "The message out of the limit per line. " << endl;
                return false;
            }

        }else if(temp == "PQC-DEMO READY"){
            continue;
        }else if(temp == "ERR HOST_NOT_TRUSTED"){
            cout << "[PASS] Unknown host identity rejected" << endl;
            return true;
        }else{
            cout << temp << endl;
            return false;
        }

    }



}