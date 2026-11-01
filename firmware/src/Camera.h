#pragma once

#include <Arduino.h>
#include <esp_camera.h>

// The OV3660. When it's off the driver is shut down completely: no frames exist to send.
namespace cam {

bool begin();
void end();
bool isOn();

// The latest JPEG frame (640x480). Give it back with release() as soon as it's sent.
camera_fb_t* grab();
void release(camera_fb_t* fb);

}  // namespace cam
