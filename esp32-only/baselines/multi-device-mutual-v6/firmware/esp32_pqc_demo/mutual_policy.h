#pragma once
#include <string.h>
namespace mutual_policy {
inline bool publicCommand(const char *command) {
  return strcmp(command, "INFO") == 0 || strcmp(command, "GET_DSA_PUBLIC_KEY") == 0;
}
inline bool legacyCommand(const char *command) {
  return strncmp(command, "AUTH_KEM", 8) == 0 || strncmp(command, "KEM_DECAPSULATE", 15) == 0 ||
         strncmp(command, "CONFIRM_SESSION", 15) == 0 || strcmp(command, "GET_KEM_PUBLIC_KEY") == 0 ||
         strncmp(command, "SET_REKEY_INTERVAL ", 19) == 0 || strcmp(command, "SELFTEST") == 0;
}
}
