#pragma once
#include <cstddef>
#include <cerrno>
#include <cassert>
#define MSG_DONTWAIT 0x40
inline int sendResult=-2, sendError=0, sendCalls=0;
inline int send(int fd,const void*,size_t n,int flags){
 assert(fd==7 && flags==MSG_DONTWAIT); ++sendCalls;
 errno=sendError; return sendResult==-2 ? static_cast<int>(n) : sendResult;
}
