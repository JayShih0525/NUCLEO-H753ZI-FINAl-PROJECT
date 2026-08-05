#pragma once

#include "esp_camera.h"

#define MAX_JPEG_SIZE (1024U * 1024U)

bool Camera_Init(void);
bool Camera_IsFrameValid(const camera_fb_t *frame);
