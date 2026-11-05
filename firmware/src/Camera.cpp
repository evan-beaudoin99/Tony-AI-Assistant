#include "Camera.h"

#include "pins.h"

namespace {
bool on = false;
}

namespace cam {

bool begin() {
  if (on) return true;
  camera_config_t c = {};
  c.pin_pwdn = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;
  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_d7 = CAM_PIN_Y9;
  c.pin_d6 = CAM_PIN_Y8;
  c.pin_d5 = CAM_PIN_Y7;
  c.pin_d4 = CAM_PIN_Y6;
  c.pin_d3 = CAM_PIN_Y5;
  c.pin_d2 = CAM_PIN_Y4;
  c.pin_d1 = CAM_PIN_Y3;
  c.pin_d0 = CAM_PIN_Y2;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href = CAM_PIN_HREF;
  c.pin_pclk = CAM_PIN_PCLK;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;  // plenty for Claude to see your desk, ~30 KB a frame
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    // The driver needs internal DMA RAM as well as PSRAM; with WiFi, audio and the screen running, that can run out.
    Serial.printf("camera: init failed 0x%x (%s). Free internal RAM %u, largest DMA block %u\n", err,
                  esp_err_to_name(err), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  on = true;
  Serial.println("camera: on");
  return true;
}

void end() {
  if (!on) return;
  esp_camera_deinit();
  on = false;
  Serial.println("camera: off");
}

bool isOn() { return on; }

camera_fb_t* grab() { return on ? esp_camera_fb_get() : nullptr; }

void release(camera_fb_t* fb) {
  if (fb) esp_camera_fb_return(fb);
}

}  // namespace cam
