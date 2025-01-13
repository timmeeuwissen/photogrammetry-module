#include "config.h"
#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>

// WiFi Credentials from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// API Endpoint from config.h
const char* api_url = API_URL;
const char* camera_url = CAMERA_URL;

// LCD Configuration
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLS, LCD_ROWS);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Controller setup started.");

  // Initialize I2C and LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(500);

  Serial.println("Initializing LCD screen.");
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
    ESP.restart();
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
    delay(500); // Let the system stabilize

    // Signal ESP32-CAM to take photo
    if (!signalPhotoCapture(i)) {
      Serial.println("Error signaling camera");
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

bool signalPhotoCapture(int photo_number) {
  HTTPClient http;
  String capture_url = String(camera_url) + "/capture?number=" + String(photo_number);
  http.begin(capture_url);
  
  int httpResponseCode = http.GET();
  http.end();
  
  return httpResponseCode == 200;
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
