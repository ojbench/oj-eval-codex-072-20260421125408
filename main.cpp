#include <bits/stdc++.h>
using namespace std;
int main(){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    // Placeholder: read all bytes and compute a simple checksum
    unsigned long long sum=0; unsigned char c; int ch;
    while((ch=getchar())!=EOF){ c=(unsigned char)ch; sum=(sum*1315423911u + c) & 0xFFFFFFFFu; }
    cout<<sum<<"\n";
    return 0;
}
