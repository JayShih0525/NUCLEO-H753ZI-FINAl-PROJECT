#include <iostream>
#include <string>

using namespace std;

int main(){

    string test = "OHELLO";

    size_t pos = test.rfind("OHE", 0);

    if(pos != string::npos) cout << "rfind: " << pos << endl;
    else cout << "not found" << endl;    

    pos = test.find('O');
    
    if(pos != string::npos) cout << "find: " << pos << endl;
    else cout << "not found " << endl;

    return 0;
}