#ifndef ESP32_ONLY_CAMERA_DEMO_H
#define ESP32_ONLY_CAMERA_DEMO_H

#include "esp_camera.h"

bool initializeCamera();
bool cameraIsReady();
bool setCameraPhotoMode();
bool setCameraStreamMode();
const char *cameraModeName();
camera_fb_t *captureCameraFrame();
void releaseCameraFrame(camera_fb_t *frame);

#endif
