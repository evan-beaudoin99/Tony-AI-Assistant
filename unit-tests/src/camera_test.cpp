// Test 1: the camera.
//
// Checks the OV3660 (PSRAM, driver, sensor ID, a real JPEG, frame rate), then serves a web page with the live
// view. Open the address it prints. The page has the stream, a snapshot button and the camera's settings.
// Same settings as Tony's firmware (firmware/src/Camera.cpp), so what you see here is what Claude sees.

#include <Arduino.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#include <atomic>

#include "TestKit.h"
#include "pins.h"

namespace {

constexpr const char* HOST = "tony-camera";
constexpr const char* BOUNDARY = "tonyframe";

httpd_handle_t webServer = nullptr;     // port 80: the page, snapshots, settings
httpd_handle_t streamServer = nullptr;  // port 81: the video, so it never blocks the rest
std::atomic<float> streamFps{0};
std::atomic<int> viewers{0};

const char PAGE[] = R"html(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tony camera test</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; font: 15px system-ui, sans-serif; background: #111; color: #eee; }
  main { max-width: 960px; margin: 0 auto; padding: 16px; }
  h1 { font-size: 20px; margin: 0 0 12px; }
  #view { width: 100%; background: #000; border-radius: 8px; min-height: 200px; display: block; }
  #stat { margin: 8px 0 16px; color: #9a9; font-family: ui-monospace, monospace; font-size: 13px; }
  .row { display: flex; flex-wrap: wrap; gap: 12px 24px; align-items: center; margin-bottom: 12px; }
  label { display: flex; gap: 8px; align-items: center; }
  button, select { font: inherit; padding: 6px 12px; border-radius: 6px; border: 1px solid #444; background: #222; color: #eee; }
  button:hover { background: #333; }
</style></head><body><main>
<h1>Tony camera test</h1>
<img id="view" alt="camera stream">
<div id="stat">connecting...</div>
<div class="row">
  <label>Resolution <select id="framesize">
    <option value="5">QVGA 320x240</option><option value="8">VGA 640x480 (Tony)</option>
    <option value="9">SVGA 800x600</option><option value="10">XGA 1024x768</option>
    <option value="11">HD 1280x720</option><option value="13">UXGA 1600x1200</option>
  </select></label>
  <label>Quality <input id="quality" type="range" min="6" max="40"> <span id="qv"></span></label>
  <label>Brightness <input id="brightness" type="range" min="-2" max="2"></label>
  <label>Saturation <input id="saturation" type="range" min="-2" max="2"></label>
</div>
<div class="row">
  <label><input id="vflip" type="checkbox"> Flip vertical</label>
  <label><input id="hmirror" type="checkbox"> Mirror</label>
  <button id="snap">Snapshot</button>
  <button id="restart">Restart stream</button>
</div>
<script>
const $ = id => document.getElementById(id);
const streamUrl = () => `${location.protocol}//${location.hostname}:81/stream?t=${Date.now()}`;
$('view').src = streamUrl();
$('restart').onclick = () => { $('view').src = ''; setTimeout(() => $('view').src = streamUrl(), 200); };
$('snap').onclick = () => window.open('/capture?t=' + Date.now(), '_blank');
const set = (v, val) => fetch(`/control?var=${v}&val=${val}`);
for (const id of ['framesize', 'quality', 'brightness', 'saturation'])
  $(id).onchange = e => set(id, e.target.value);
for (const id of ['vflip', 'hmirror'])
  $(id).onchange = e => set(id, e.target.checked ? 1 : 0);
$('quality').oninput = e => $('qv').textContent = e.target.value;
let first = true;
async function poll() {
  try {
    const s = await (await fetch('/status')).json();
    $('stat').textContent = `${s.width}x${s.height}  ${s.fps.toFixed(1)} fps  ` +
      `last frame ${(s.frame_bytes / 1024).toFixed(1)} KB  wifi ${s.rssi} dBm  ` +
      `free RAM ${(s.heap / 1024) | 0} KB  PSRAM ${(s.psram / 1024) | 0} KB  up ${s.uptime}s`;
    if (first) {
      for (const k of ['framesize', 'quality', 'brightness', 'saturation']) $(k).value = s[k];
      $('vflip').checked = !!s.vflip; $('hmirror').checked = !!s.hmirror; $('qv').textContent = s.quality;
      first = false;
    }
  } catch { $('stat').textContent = 'lost the ESP32...'; }
  setTimeout(poll, 1000);
}
poll();
</script></main></body></html>)html";

std::atomic<uint32_t> lastFrameBytes{0};

esp_err_t handlePage(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleCapture(httpd_req_t* req) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=tony.jpg");
  esp_err_t r = httpd_resp_send(req, reinterpret_cast<const char*>(fb->buf), fb->len);
  esp_camera_fb_return(fb);
  return r;
}

esp_err_t handleControl(httpd_req_t* req) {
  char query[64], var[24], val[12];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
      httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need var and val");
    return ESP_FAIL;
  }
  sensor_t* s = esp_camera_sensor_get();
  int v = atoi(val);
  int r = -1;
  if (!strcmp(var, "framesize")) r = s->set_framesize(s, static_cast<framesize_t>(v));
  else if (!strcmp(var, "quality")) r = s->set_quality(s, v);
  else if (!strcmp(var, "brightness")) r = s->set_brightness(s, v);
  else if (!strcmp(var, "saturation")) r = s->set_saturation(s, v);
  else if (!strcmp(var, "vflip")) r = s->set_vflip(s, v);
  else if (!strcmp(var, "hmirror")) r = s->set_hmirror(s, v);
  Serial.printf("camera: %s = %d %s\n", var, v, r == 0 ? "ok" : "FAILED");
  if (r != 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "sensor said no");
    return ESP_FAIL;
  }
  return httpd_resp_send(req, "ok", 2);
}

esp_err_t handleStatus(httpd_req_t* req) {
  sensor_t* s = esp_camera_sensor_get();
  const camera_status_t& st = s->status;
  char json[320];
  snprintf(json, sizeof(json),
           "{\"width\":%u,\"height\":%u,\"framesize\":%d,\"quality\":%d,\"brightness\":%d,\"saturation\":%d,"
           "\"vflip\":%d,\"hmirror\":%d,\"fps\":%.2f,\"frame_bytes\":%lu,\"viewers\":%d,\"rssi\":%d,"
           "\"heap\":%lu,\"psram\":%lu,\"uptime\":%lu}",
           resolution[st.framesize].width, resolution[st.framesize].height, st.framesize, st.quality, st.brightness,
           st.saturation, st.vflip, st.hmirror, streamFps.load(), (unsigned long)lastFrameBytes.load(), viewers.load(),
           WiFi.RSSI(), (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram(),
           (unsigned long)(millis() / 1000));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// MJPEG: one long response, a JPEG per part. Browsers show it as a live <img>.
esp_err_t handleStream(httpd_req_t* req) {
  char ctype[64], part[96];
  snprintf(ctype, sizeof(ctype), "multipart/x-mixed-replace;boundary=%s", BOUNDARY);
  httpd_resp_set_type(req, ctype);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  ++viewers;
  Serial.printf("stream: viewer connected (%d watching)\n", viewers.load());

  uint32_t lastMs = millis();
  float fps = 0;
  esp_err_t r = ESP_OK;
  while (r == ESP_OK) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("stream: frame grab failed");
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    int n = snprintf(part, sizeof(part), "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                     BOUNDARY, fb->len);
    r = httpd_resp_send_chunk(req, part, n);
    if (r == ESP_OK) r = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(fb->buf), fb->len);
    lastFrameBytes = fb->len;
    esp_camera_fb_return(fb);

    uint32_t now = millis();
    float inst = 1000.0f / max<uint32_t>(now - lastMs, 1);
    fps = fps == 0 ? inst : fps * 0.9f + inst * 0.1f;
    streamFps = fps;
    lastMs = now;
  }
  --viewers;
  if (viewers == 0) streamFps = 0;
  Serial.printf("stream: viewer left (%d watching)\n", viewers.load());
  return ESP_OK;
}

bool startServers() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.ctrl_port = 32768;
  if (httpd_start(&webServer, &cfg) != ESP_OK) return false;
  const httpd_uri_t pages[] = {
      {"/", HTTP_GET, handlePage, nullptr},
      {"/capture", HTTP_GET, handleCapture, nullptr},
      {"/control", HTTP_GET, handleControl, nullptr},
      {"/status", HTTP_GET, handleStatus, nullptr},
  };
  for (const auto& p : pages) httpd_register_uri_handler(webServer, &p);

  cfg.server_port = 81;
  cfg.ctrl_port = 32769;
  if (httpd_start(&streamServer, &cfg) != ESP_OK) return false;
  const httpd_uri_t stream = {"/stream", HTTP_GET, handleStream, nullptr};
  httpd_register_uri_handler(streamServer, &stream);
  return true;
}

esp_err_t initCamera() {
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
  // The frame buffers are sized for this, so start big and drop to VGA below: the page can then go up to UXGA.
  c.frame_size = FRAMESIZE_UXGA;
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c);
}

bool runChecks() {
  char msg[128];

  check::expect(psramFound(), "PSRAM found (the frame buffers live there)",
                "The board settings in platformio.ini must match an N16R8: qio_opi memory, opi PSRAM.");

  esp_err_t err = initCamera();
  snprintf(msg, sizeof(msg), "Camera driver started (%s)", esp_err_to_name(err));
  if (!check::expect(err == ESP_OK, msg,
                     "Reseat the ribbon: lift the connector latch, push the cable all the way in, close the latch. "
                     "0x105 / ESP_ERR_NOT_FOUND means the sensor didn't answer on SCCB (pins 4 and 5).")) {
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    check::expect(true, "Sensor is an OV3660");
  } else {
    snprintf(msg, sizeof(msg), "Sensor ID is 0x%04x, not the OV3660 (0x3660).", s->id.PID);
    check::warn(msg, "A different sensor (OV2640 = 0x26, OV5640 = 0x5640) still works; colors may need tuning.");
  }
  // Tony's settings, from firmware/src/Camera.cpp.
  s->set_framesize(s, FRAMESIZE_VGA);
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  rgbLedWrite(PIN_RGB_LED, 40, 0, 0);  // the "camera is on" light, same as Tony

  // The first frames after a size change can be stale; throw a few away.
  for (int i = 0; i < 3; ++i) {
    if (camera_fb_t* fb = esp_camera_fb_get()) esp_camera_fb_return(fb);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!check::expect(fb != nullptr, "Grabbed a frame",
                     "The sensor answered but sent no picture: check the ribbon for a bent or dirty contact.")) {
    return false;
  }
  bool jpeg = fb->len > 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 && fb->buf[fb->len - 2] == 0xFF &&
              fb->buf[fb->len - 1] == 0xD9;
  snprintf(msg, sizeof(msg), "Frame is a complete JPEG (%ux%u, %u bytes)", fb->width, fb->height, fb->len);
  check::expect(jpeg, msg, "Broken JPEG markers: usually a loose data line (Y2-Y9) on the ribbon.");
  if (fb->len < 3000) check::warn("That frame is tiny. Is the lens cap on, or the camera face down?");
  esp_camera_fb_return(fb);

  const int N = 20;
  uint32_t start = millis();
  int got = 0;
  for (int i = 0; i < N; ++i) {
    if (camera_fb_t* f = esp_camera_fb_get()) {
      ++got;
      esp_camera_fb_return(f);
    }
  }
  float fps = got * 1000.0f / max<uint32_t>(millis() - start, 1);
  snprintf(msg, sizeof(msg), "Steady frames: %d/%d at %.1f fps (VGA)", got, N, fps);
  check::expect(got == N && fps >= 5, msg, "Dropped or slow frames: try a shorter or better-seated ribbon.");
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // time for the serial monitor to attach
  check::title("CAMERA");
  check::lastReset();

  bool camOk = runChecks();
  bool netOk = camOk && testwifi::connect(HOST);
  if (netOk) check::expect(startServers(), "Web server started");
  check::summary();

  if (netOk) {
    testwifi::printAddress(HOST);
  } else if (camOk) {
    Serial.println("  The camera works, but without WiFi there's no live view.");
  }
}

void loop() {
  static uint32_t lastMs = 0;
  if (millis() - lastMs >= 10000 && viewers > 0) {
    lastMs = millis();
    Serial.printf("stream: %.1f fps, %d watching\n", streamFps.load(), viewers.load());
  }
  delay(100);
}
