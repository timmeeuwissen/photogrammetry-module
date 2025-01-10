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
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLS, LCD_ROWS); // I2C address 0x27, 16 columns, 2 rows

void setup() {
  Serial.begin(9600);
  delay(2000);
  
  // Initialize LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");


  // Initialize WiFi
  WiFi.begin(ssid, password);

  lcd.setCursor(0, 1);
  lcd.print("WiFi Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("WiFi connected.");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connected ");

  // Initialize stepper motor pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);

  // Setting the steppermotor pins to a known low state
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);

  // Initialize camera
  camera_config_t config;
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

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    lcd.setCursor(0, 1);
    lcd.print("Cam Init Failed");
    return;
  }
  Serial.println("Camera initialized.");
  lcd.setCursor(0, 1);
  lcd.print("Cam Ready       ");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String status_url = String(api_url) + "/status";
    http.begin(status_url);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String response = http.getString();
      if (response.indexOf("scanning") != -1) {
        Serial.println("Scan command received. Starting process...");
        lcd.setCursor(0, 0);
        lcd.print("Process Started ");
        startProcess();
      }
    }
    
    http.end();
    delay(1000);  // Poll every second
  } else {
    // Try to reconnect to WiFi
    WiFi.begin(ssid, password);
    lcd.setCursor(0, 1);
    lcd.print("WiFi Reconnect ");
    delay(5000);
  }
}

void startProcess() {
  const int steps_per_rotation = 60;  // For 6-degree increments in a 360° circle
  for (int i = 0; i < steps_per_rotation; i++) {
    lcd.setCursor(0, 1);
    lcd.print("Step: ");
    lcd.print(i + 1);
    
    // Rotate motor by 6 degrees
    rotateStepper();

    // Capture and send photo
    if (!captureAndSendPhoto(i)) {
      Serial.println("Error capturing or sending photo.");
      lcd.setCursor(0, 1);
      lcd.print("Capture Failed ");
    }
  }

  // Notify API process completion
  lcd.setCursor(0, 1);
  lcd.print("Uploading Done ");
  notifyAPICompletion();
  lcd.setCursor(0, 1);
  lcd.print("Process Done   ");
}

void rotateStepper() {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
  delay(10);

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, HIGH);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
  delay(10);

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, HIGH);
  digitalWrite(IN4_PIN, LOW);
  delay(10);

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, HIGH);
  delay(10);
}

bool captureAndSendPhoto(int photo_number) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed.");
    return false;
  }

  // Prepare HTTP client
  HTTPClient http;
  http.begin(api_url);
  http.addHeader("Content-Type", "multipart/form-data");

  // Send image as a POST request
  String boundary = "----ESP32Boundary";
  String body = "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"image\"; filename=\"photo_" + String(photo_number) + ".jpg\"\r\n";
  body += "Content-Type: image/jpeg\r\n\r\n";

  WiFiClient* stream = http.getStreamPtr();
  stream->print(body);
  stream->write(fb->buf, fb->len);
  stream->print("\r\n--" + boundary + "--\r\n");

  int httpResponseCode = http.POST("");
  esp_camera_fb_return(fb);

  if (httpResponseCode > 0) {
    Serial.printf("Photo %d sent successfully. HTTP Response code: %d\n", photo_number, httpResponseCode);
  } else {
    Serial.printf("Error sending photo %d. HTTP Response code: %d\n", photo_number, httpResponseCode);
  }

  http.end();
  return httpResponseCode > 0;
}

void notifyAPICompletion() {
  HTTPClient http;
  String completion_url = String(api_url) + "/complete";
  http.begin(completion_url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.printf("API notified of completion. HTTP Response code: %d\n", httpResponseCode);
  } else {
    Serial.printf("Error notifying API. HTTP Response code: %d\n", httpResponseCode);
  }

  http.end();
}
