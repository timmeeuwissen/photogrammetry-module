#include "config.h"
#include <WiFi.h>
#include "esp_camera.h"
#include <HTTPClient.h>
#include <WebServer.h>

// WiFi Credentials from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// API Endpoint from config.h
const char* api_url = API_URL;

// Create web server on port 8889
WebServer server(8889);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Camera setup started.");

  // Initialize camera with error handling
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    delay(1000);
    ESP.restart();
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_SVGA);
    s->set_quality(s, 12);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
    delay(100);
  }

  Serial.println("Camera initialized");

  // Initialize WiFi
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    Serial.print(".");
    wifi_retry++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    delay(1000);
    ESP.restart();
    return;
  }

  Serial.println("WiFi connected");
  Serial.print("Camera IP: ");
  Serial.println(WiFi.localIP());

  // Set up web server endpoints
  server.on("/api/capture", HTTP_GET, handleCapture);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect(true);
    delay(1000);
    WiFi.begin(ssid, password);
    delay(5000);
    return;
  }

  server.handleClient();
  delay(10);
}

void handleCapture() {
  String photoNumber = server.arg("number");
  if (photoNumber == "") {
    server.send(400, "text/plain", "Missing photo number");
    return;
  }

  if (!captureAndSendPhoto(photoNumber.toInt())) {
    server.send(500, "text/plain", "Failed to capture/send photo");
    return;
  }

  server.send(200, "text/plain", "Photo captured and sent");
}

bool captureAndSendPhoto(int photo_number) {
  camera_fb_t* fb = NULL;
  bool success = false;
  
  // Capture with retry
  for (int i = 0; i < 3; i++) {
    fb = esp_camera_fb_get();
    if (fb) break;
    delay(100);
  }
  
  if (!fb) {
    Serial.println("Camera capture failed");
    return false;
  }

  HTTPClient http;
  http.begin(api_url);
  http.addHeader("Content-Type", "multipart/form-data");

  String boundary = "ESP32PhotoBoundary";
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"image\"; filename=\"photo_" + String(photo_number) + ".jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  uint32_t imageLen = fb->len;
  uint32_t extraLen = head.length() + tail.length();
  uint32_t totalLen = imageLen + extraLen;

  http.addHeader("Content-Length", String(totalLen));
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  WiFiClient* client = http.getStreamPtr();
  
  success = true;
  if (!client->write(head.c_str(), head.length())) success = false;
  if (success && !client->write(fb->buf, fb->len)) success = false;
  if (success && !client->write(tail.c_str(), tail.length())) success = false;

  esp_camera_fb_return(fb);
  
  int httpResponseCode = http.POST("");
  http.end();

  if (!success) {
    Serial.println("Failed to send photo data");
    return false;
  }

  return httpResponseCode > 0;
}
