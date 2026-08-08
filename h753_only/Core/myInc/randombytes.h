#ifndef RANDOMBYTES_H
#define RANDOMBYTES_H

#include <stdint.h>
#include <stddef.h>

void Random_Init(void);
int randombytes(uint8_t *buf, size_t len);

#endif
