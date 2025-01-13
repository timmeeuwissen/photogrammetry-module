#include <WiFi.h>
#include "esp_camera.h"
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


// WiFi Configuration
#define WIFI_SSID "meeuw-iot"
#define WIFI_PASSWORD "avZD7rafc7hQ"

// Server Configuration
#define API_URL "http://192.168.10.171:8888/api"

// Camera pins for ESP32-CAM HW-297
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


// Flash LED Pin (built into ESP32-CAM)
#define FLASH_LED_PIN 4  // GPIO4 is the built-in flash LED


// WiFi Credentials from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Server Configuration
const char* server_url = API_URL;
String auth_token;
bool is_registered = false;
unsigned long last_heartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 10000;  // 10 seconds

// Create web server
WebServer server(80);

void setup() {
  Serial.begin(9600);
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
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Set up server endpoints
  server.on("/capture", HTTP_POST, handleCapture);
  server.on("/abort", HTTP_POST, handleAbort);
  
  server.begin();
  Serial.println("HTTP server started");

  // Register with main server
  registerWithServer();
}

void loop() {
  server.handleClient();

  // Handle heartbeat
  if (is_registered && millis() - last_heartbeat > HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    last_heartbeat = millis();
  }

  // If WiFi disconnects, try to reconnect
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect(true);
    delay(1000);
    WiFi.begin(ssid, password);
    delay(5000);
    return;
  }
}

void registerWithServer() {
  Serial.println("Registring with main server");
  HTTPClient http;
  http.begin(String(server_url) + "/register");
  http.addHeader("Content-Type", "application/json");

  String ip = WiFi.localIP().toString();
  String payload = "{\"type\":\"camera\",\"ip\":\"" + ip + "\"}";
  
  Serial.println("Sending payload to server");
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    Serial.println("Server was okay with registration request");

    String response = http.getString();
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      auth_token = doc["token"].as<String>();
      is_registered = true;
      Serial.println("Registered with server successfully");
    }
  } else {
    Serial.printf("Registration failed: %d\n", httpCode);
    delay(2000);
    Serial.println("Restarting ESP, we need to start the registration over");
    ESP.restart();
  }
  
  http.end();
}


void sendHeartbeat() {
  if (!is_registered) return;

  HTTPClient http;
  http.begin(String(server_url) + "/heartbeat");
  http.addHeader("Authorization", "Bearer " + auth_token);
  
  int httpCode = http.POST("");
  
  if (httpCode != 200) {
    Serial.printf("Heartbeat failed: %d\n", httpCode);
    is_registered = false;
    registerWithServer();
  }
  
  http.end();
}

void handleCapture() {
  Serial.println("Capture request received");
  
  if (!server.hasHeader("Authorization") || 
      "Bearer " + auth_token != server.header("Authorization")) {
    Serial.println("Unauthorized capture request");
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  if (!server.hasArg("plain")) {
    Serial.println("Missing body in capture request");
    server.send(400, "text/plain", "Missing body");
    return;
  }

  Serial.println("Parsing capture request JSON");
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  int step = doc["step"] | 0;
  Serial.print("Starting capture for step: ");
  Serial.println(step);

  // Capture photo first
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed - null frame buffer");
    server.send(500, "text/plain", "Failed to capture photo");
    return;
  }
  Serial.printf("Photo captured successfully. Size: %u bytes\n", fb->len);

  // Send response headers
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Disposition", "attachment; filename=photo_" + String(step) + ".jpg");
  server.sendHeader("Connection", "close");
  server.send(200, "image/jpeg", "");  // Send headers only
  
  // Send photo data
  server.sendContent((const char*)fb->buf, fb->len);
  
  esp_camera_fb_return(fb);
  Serial.println("Photo sent successfully");

  // Send capture complete notification
  HTTPClient http;
  http.begin(String(server_url) + "/capture_complete");
  http.addHeader("Authorization", "Bearer " + auth_token);
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"step\":" + String(step) + "}";
  int completeCode = http.POST(payload);
  Serial.printf("Capture complete notification response: %d\n", completeCode);
  http.end();
}

void handleAbort() {
  if (!server.hasHeader("Authorization") || 
      "Bearer " + auth_token != server.header("Authorization")) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  server.send(200, "text/plain", "OK");
}

bool captureAndSendPhoto(int photo_number) {
  Serial.println("Initializing photo capture and upload");

  // Verify WiFi connection first
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, attempting reconnection");
    WiFi.disconnect(true);
    delay(1000);
    WiFi.begin(ssid, password);
    
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 10) {
      delay(500);
      Serial.print(".");
      retry++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Failed to reconnect WiFi");
      return false;
    }
    Serial.println("WiFi reconnected");
  }

  // First capture the photo before initializing HTTP connection
  Serial.println("Capturing photo from camera");
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed - null frame buffer");
    return false;
  }
  Serial.printf("Photo captured successfully. Size: %u bytes\n", fb->len);

  // Now initialize HTTP connection
  WiFiClient client;
  HTTPClient http;
  String upload_url = String(server_url) + "/upload";
  Serial.print("Connecting to upload URL: ");
  Serial.println(upload_url);
  
  // Parse server address from API_URL (format: "http://192.168.10.171:8888/api")
  String serverAddress = String(server_url).substring(7); // Remove "http://"
  int portIndex = serverAddress.indexOf(':');
  int pathIndex = serverAddress.indexOf('/', portIndex);
  String host = serverAddress.substring(0, portIndex);
  int port = serverAddress.substring(portIndex + 1, pathIndex).toInt();
  
  Serial.print("Connecting to server: ");
  Serial.print(host);
  Serial.print(":");
  Serial.println(port);

  if (!client.connect(host.c_str(), port)) {
    Serial.println("Failed to connect to server");
    esp_camera_fb_return(fb);
    return false;
  }

  String boundary = "---------------------------" + String(millis());
  String head = 
    "POST /api/upload HTTP/1.1\r\n"
    "Host: " + host + ":" + String(port) + "\r\n"
    "Authorization: Bearer " + auth_token + "\r\n"
    "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
    "Accept: */*\r\n"
    "Accept-Encoding: gzip, deflate, br\r\n"
    "Connection: close\r\n";

  String body_head = 
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"image\"; filename=\"photo_" + String(photo_number) + ".jpg\"\r\n"
    "Content-Type: image/jpeg\r\n"
    "Content-Transfer-Encoding: binary\r\n\r\n";

  String body_tail = "\r\n--" + boundary + "--\r\n";

  uint32_t imageLen = fb->len;
  uint32_t totalLen = body_head.length() + imageLen + body_tail.length();
  head += "Content-Length: " + String(totalLen) + "\r\n\r\n";

  Serial.println("Writing headers");
  if (!client.print(head)) {
    Serial.println("Failed to write headers");
    client.stop();
    esp_camera_fb_return(fb);
    return false;
  }

  Serial.println("Writing body headers");
  if (!client.print(body_head)) {
    Serial.println("Failed to write body headers");
    client.stop();
    esp_camera_fb_return(fb);
    return false;
  }

  // Write the photo data in chunks
  uint8_t *fbBuf = fb->buf;
  size_t fbLen = fb->len;
  size_t remainingBytes = fbLen;
  const size_t bufferSize = 1024; // Smaller chunks for more reliable transmission
  size_t totalBytesWritten = 0;

  Serial.println("Starting to stream photo data");
  while (remainingBytes > 0) {
    size_t bytesToWrite = (remainingBytes > bufferSize) ? bufferSize : remainingBytes;
    
    size_t bytesWritten = client.write(fbBuf, bytesToWrite);
    if (bytesWritten == 0) {
      Serial.printf("Failed to write chunk at offset %u\n", totalBytesWritten);
      client.stop();
      esp_camera_fb_return(fb);
      return false;
    }
    
    fbBuf += bytesWritten;
    remainingBytes -= bytesWritten;
    totalBytesWritten += bytesWritten;
    
    Serial.printf("Progress: %u/%u bytes sent\n", totalBytesWritten, fbLen);
    delay(1); // Small delay to prevent watchdog reset
  }

  Serial.println("Writing body tail");
  if (!client.print(body_tail)) {
    Serial.println("Failed to write body tail");
    client.stop();
    esp_camera_fb_return(fb);
    return false;
  }

  Serial.println("Waiting for response");
  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 10000) {
      Serial.println("Response timeout");
      client.stop();
      esp_camera_fb_return(fb);
      return false;
    }
    delay(10);
  }

  // Read response
  String response = client.readString();
  client.stop();
  esp_camera_fb_return(fb);

  Serial.println("Response received:");
  Serial.println(response);

  bool success = false;
  if (response.indexOf("200 OK") > -1) {
    success = true;
    
    // Notify capture complete
    Serial.println("Sending capture complete notification");
    HTTPClient http;
    http.begin(String(server_url) + "/capture_complete");
    http.addHeader("Authorization", "Bearer " + auth_token);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"step\":" + String(photo_number) + "}";
    int completeCode = http.POST(payload);
    Serial.printf("Capture complete notification response: %d\n", completeCode);
    http.end();
  } else {
    Serial.println("Upload failed - server did not return 200 OK");
  }

  Serial.println("Photo capture and upload process finished");
  return success;
}
