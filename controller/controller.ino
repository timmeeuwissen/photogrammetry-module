#include "config.h"
#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

// WiFi Credentials from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Server Configuration
const char* server_url = API_URL;
String auth_token;
bool is_registered = false;
unsigned long last_heartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 10000;  // 10 seconds

// LCD Configuration
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLS, LCD_ROWS);

// Create web server
WebServer server(80);

// Stepper state
bool is_scanning = false;
int current_step = 0;
const int STEPS_PER_ROTATION = 60;

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

  // Initialize stepper motor pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);

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
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Set up server endpoints
  server.on("/lcd", HTTP_POST, handleLcdUpdate);
  server.on("/start_rotation", HTTP_POST, handleStartRotation);
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
    lcd.setCursor(0, 1);
    lcd.print("WiFi Reconnect ");
    delay(5000);
    return;
  }

  // Handle scanning process
  if (is_scanning && current_step < STEPS_PER_ROTATION) {
    rotateStepper();
    delay(500); // Let the system stabilize

    // Notify server of rotation completion
    notifyRotationComplete();
    
    current_step++;
    delay(500);
  } else if (is_scanning && current_step >= STEPS_PER_ROTATION) {
    is_scanning = false;
    current_step = 0;
    notifyScanComplete();
  }
}

void registerWithServer() {
  HTTPClient http;
  http.begin(String(server_url) + "/register");
  http.addHeader("Content-Type", "application/json");

  String ip = WiFi.localIP().toString();
  String payload = "{\"type\":\"controller\",\"ip\":\"" + ip + "\"}";
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    String response = http.getString();
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      auth_token = doc["token"].as<String>();
      is_registered = true;
      Serial.println("Registered with server successfully");
      lcd.clear();
      lcd.print("Ready");
    }
  } else {
    Serial.printf("Registration failed: %d\n", httpCode);
    lcd.clear();
    lcd.print("Reg Failed");
    delay(2000);
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

void handleLcdUpdate() {
  if (!server.hasHeader("Authorization") || 
      "Bearer " + auth_token != server.header("Authorization")) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  JsonArray lines = doc["lines"].as<JsonArray>();
  if (lines.size() != 2) {
    server.send(400, "text/plain", "Expected two lines");
    return;
  }

  // Update first line
  String line1 = lines[0].as<String>();
  lcd.setCursor(0, 0);
  lcd.print("                "); // Clear line
  lcd.setCursor(0, 0);
  lcd.print(line1);

  // Update second line
  String line2 = lines[1].as<String>();
  lcd.setCursor(0, 1);
  lcd.print("                "); // Clear line
  lcd.setCursor(0, 1);
  lcd.print(line2);

  server.send(200, "text/plain", "OK");
}

void handleStartRotation() {
  if (!server.hasHeader("Authorization") || 
      "Bearer " + auth_token != server.header("Authorization")) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  if (is_scanning) {
    server.send(409, "text/plain", "Scan already in progress");
    return;
  }

  is_scanning = true;
  current_step = 0;
  server.send(200, "text/plain", "OK");
}

void handleAbort() {
  if (!server.hasHeader("Authorization") || 
      "Bearer " + auth_token != server.header("Authorization")) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  is_scanning = false;
  current_step = 0;
  server.send(200, "text/plain", "OK");
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

void notifyRotationComplete() {
  HTTPClient http;
  http.begin(String(server_url) + "/rotation_complete");
  http.addHeader("Authorization", "Bearer " + auth_token);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"step\":" + String(current_step + 1) + "}";
  http.POST(payload);
  http.end();
}

void notifyScanComplete() {
  HTTPClient http;
  http.begin(String(server_url) + "/scan_complete");
  http.addHeader("Authorization", "Bearer " + auth_token);
  http.POST("");
  http.end();
}
