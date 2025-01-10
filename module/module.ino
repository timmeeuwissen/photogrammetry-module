#include "config.h"
#include <WiFi.h>
#include "esp_camera.h"
#include <Wire.h>
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>

// WiFi Credentials from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// API Endpoint from config.h
const char* api_url = API_URL;

// LCD Configuration
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLS, LCD_ROWS);

void setup() {
  // Initialize Serial first for debugging
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  Serial.println("Setup started.");

  // Initialize I2C and LCD with more delay
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(500);

  Serial.println("Initialising the LCD screen.");
  lcd.init();
  delay(100);
  lcd.backlight();
  delay(100);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  delay(1000);

  // Initialize stepper motor pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
  delay(100);

  // Camera configuration with more conservative settings
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

  // Initialize camera with error handling
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    lcd.setCursor(0, 1);
    lcd.print("Cam Init Failed");
    delay(1000);
    ESP.restart(); // Restart on camera init failure
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
  lcd.setCursor(0, 1);
  lcd.print("Cam Ready      ");
  delay(1000);

  // Initialize WiFi
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  lcd.setCursor(0, 1);
  lcd.print("WiFi Connecting");

  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    Serial.print(".");
    wifi_retry++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    lcd.setCursor(0, 1);
    lcd.print("WiFi Failed    ");
    delay(1000);
    ESP.restart(); // Restart on WiFi failure
    return;
  }

  Serial.println("WiFi connected");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connected ");
  delay(1000);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect(true);
    delay(1000);
    WiFi.begin(ssid, password);
    lcd.setCursor(0, 1);
    lcd.print("WiFi Reconnect ");
    delay(5000);
    return;
  }

  HTTPClient http;
  String status_url = String(api_url) + "/status";
  http.begin(status_url);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    if (response.indexOf("scanning") != -1) {
      Serial.println("Scan command received");
      lcd.setCursor(0, 0);
      lcd.print("Process Started ");
      startProcess();
    }
  }
  
  http.end();
  delay(1000);
}

bool checkAborted() {
  HTTPClient http;
  String status_url = String(api_url) + "/status";
  http.begin(status_url);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    bool aborted = response.indexOf("idle") != -1;
    http.end();
    return aborted;
  }
  http.end();
  return false;
}

void startProcess() {
  const int steps_per_rotation = 60;
  for (int i = 0; i < steps_per_rotation; i++) {
    // Check if scan was aborted
    if (checkAborted()) {
      Serial.println("Scan aborted");
      lcd.setCursor(0, 1);
      lcd.print("Scan Aborted   ");
      return;
    }

    lcd.setCursor(0, 1);
    lcd.print("Step: ");
    lcd.print(i + 1);
    lcd.print("        ");
    
    rotateStepper();
    delay(500); // Let the camera stabilize

    if (!captureAndSendPhoto(i)) {
      Serial.println("Error in capture/send");
      lcd.setCursor(0, 1);
      lcd.print("Error: Step ");
      lcd.print(i + 1);
      delay(1000);
    }
    delay(500); // Additional delay between steps
  }

  lcd.setCursor(0, 1);
  lcd.print("Finishing...   ");
  notifyAPICompletion();
  lcd.setCursor(0, 1);
  lcd.print("Process Done   ");
}

void rotateStepper() {
  // Stepper sequence for smoother rotation
  const int numSteps = 8;
  const int stepSequence[8][4] = {
    {1,0,0,0},
    {1,1,0,0},
    {0,1,0,0},
    {0,1,1,0},
    {0,0,1,0},
    {0,0,1,1},
    {0,0,0,1},
    {1,0,0,1}
  };

  for (int step = 0; step < numSteps; step++) {
    digitalWrite(IN1_PIN, stepSequence[step][0]);
    digitalWrite(IN2_PIN, stepSequence[step][1]);
    digitalWrite(IN3_PIN, stepSequence[step][2]);
    digitalWrite(IN4_PIN, stepSequence[step][3]);
    delay(10);
  }
  
  // Set all pins low to reduce power consumption and heat
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
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

void notifyAPICompletion() {
  HTTPClient http;
  String completion_url = String(api_url) + "/complete";
  http.begin(completion_url);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode <= 0) {
    Serial.printf("Error notifying API: %d\n", httpResponseCode);
  }
  
  http.end();
}
