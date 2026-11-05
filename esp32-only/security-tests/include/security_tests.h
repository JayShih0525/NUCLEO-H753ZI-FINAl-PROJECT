#ifndef SECURITY_TESTS
#define SECURITY_TESTS

#include <winsock2.h>
#include <string>

bool test_unauthenticated_camera(SOCKET client);
bool test_unknown_host(SOCKET client);

#endif