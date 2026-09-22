#include "randombytes.h"

#include "esp_random.h"

int randombytes(uint8_t *output, size_t n) {
    if (output == NULL) {
        return -1;
    }

    esp_fill_random(output, n);
    return 0;
}
