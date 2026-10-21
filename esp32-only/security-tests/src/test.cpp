#include <iostream>
#include <chrono>

using namespace std;

int main(){

    auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
    
    
    return 0;
}