#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

// WiFi Configuration
#define WIFI_SSID "meeuw-iot"
#define WIFI_PASSWORD "avZD7rafc7hQ"

// Server Configuration
#define API_URL "http://192.168.10.171:8888/api"

// Stepper Motor Pins for ESP32-C3
#define IN1_PIN 2   // GPIO2
#define IN2_PIN 3   // GPIO3
#define IN3_PIN 4   // GPIO4
#define IN4_PIN 10  // GPIO10

// LCD Configuration (I2C pins for ESP32-C3)
#define SDA_PIN 5   // GPIO5
#define SCL_PIN 6   // GPIO6
#define I2C_ADDR 0x27  // Common address for PCF8574 I2C LCD adapter
#define LCD_COLS 16
#define LCD_ROWS 2


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
const int STEPS_PER_ROTATION = 512;  // 512 steps for 360 degrees
int current_angle = 0;  // Track current angle in degrees

// actually rotates the stepper motor
void rotateStepper(bool clockwise = true) {
  // Half-step sequence for more precise control
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

  if (clockwise) {
    for (int step = 0; step < numSteps; step++) {
      digitalWrite(IN1_PIN, stepSequence[step][0]);
      digitalWrite(IN2_PIN, stepSequence[step][1]);
      digitalWrite(IN3_PIN, stepSequence[step][2]);
      digitalWrite(IN4_PIN, stepSequence[step][3]);
      delay(1);  // Minimal delay for maximum speed while maintaining reliability
    }
  } else {
    for (int step = numSteps - 1; step >= 0; step--) {
      digitalWrite(IN1_PIN, stepSequence[step][0]);
      digitalWrite(IN2_PIN, stepSequence[step][1]);
      digitalWrite(IN3_PIN, stepSequence[step][2]);
      digitalWrite(IN4_PIN, stepSequence[step][3]);
      delay(1);  // Minimal delay for maximum speed while maintaining reliability
    }
  }
  
  // Set all pins low to reduce power consumption and heat
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
}

// Convert angle to steps with improved precision
int angleToSteps(int angle) {
  // Use floating point for more accurate calculation
  // 4096 steps = 360 degrees
  // Therefore, steps = (angle * 4096) / 360
  float stepsPerDegree = STEPS_PER_ROTATION / 360.0;
  return (int)(angle * stepsPerDegree);
}

// Move stepper to absolute or relative position
void moveToPosition(int target_angle, bool is_relative) {
  int target;
  if (is_relative) {
    target = (current_angle + target_angle + 360) % 360;  // Handle negative angles
  } else {
    target = target_angle % 360;  // Normalize to 0-359
  }
  
  // Calculate shortest path
  int diff = target - current_angle;
  if (diff > 180) diff -= 360;
  if (diff < -180) diff += 360;
  
  // Convert angle difference to steps
  int steps = angleToSteps(abs(diff));
  bool clockwise = diff > 0;
  
  // Move the motor with acceleration
  int delayTime;
  for (int i = 0; i < steps; i++) {
    // Start slow, speed up in the middle, slow down at the end
    if (i < steps/4 || i >= (steps*3)/4) {
      delayTime = 2;  // Slower at start/end
    } else {
      delayTime = 1;  // Faster in the middle
    }
    rotateStepper(clockwise);
    delay(delayTime);
  }
  
  current_angle = target;
}

void setup() {
  delay(2000);  // Give USB CDC time to initialize
  
  // Start serial with default baud rate
  Serial.begin(9600);
  Serial.println();  // Print empty line in case of garbage
  Serial.println("Starting up...");
  delay(100);
  
  Serial.println("Setting up pins...");
  // Initialize stepper motor pins
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);

  Serial.println("Setting up the I2C pins");
  Wire.begin();

  Serial.println("Setting up the LCD module");
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing");
  lcd.setCursor(1, 0);
  lcd.print("Controller..");

  Serial.println("Basic setup complete");
  delay(1000);
  
  // Initialize WiFi
  Serial.println("Initializing WiFi");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("WiFi Connecting");

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
  Serial.println("Setting up Server Endpoints");
  server.on("/lcd", HTTP_POST, handleLcdUpdate);
  server.on("/start_rotation", HTTP_POST, handleStartRotation);
  server.on("/abort", HTTP_POST, handleAbort);
  server.on("/motor", HTTP_POST, handleMotorControl);
  
  server.begin();
  Serial.println("HTTP server started");

  // Register with main server
  registerWithServer();

  Serial.println("Testing loop will start soon...");
  delay(1000);
}

void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 5000) {  // Print every 5 seconds
    Serial.print("Beat: IP address: ");
    Serial.println(WiFi.localIP());
    lastPrint = millis();
  }

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
    return;
  }

  // Handle scanning process
  if (is_scanning) {
    // Calculate angle for this step (6 degrees per step)
    int target_angle = (current_step * 6) % 360;
    
    // Move to position
    moveToPosition(target_angle, false);
    delay(500); // Let the system stabilize
    
    // Notify server of rotation completion
    notifyRotationComplete();
    
    current_step++;
    delay(500);
    
    // Check if scan is complete (60 steps * 6 degrees = 360 degrees)
    if (current_step >= 60) {
      is_scanning = false;
      current_step = 0;
      notifyScanComplete();
    }
  }
}

void handleMotorControl() {
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

  if (!doc.containsKey("angle")) {
    server.send(400, "text/plain", "Missing angle parameter");
    return;
  }

  int angle = doc["angle"].as<int>();
  bool is_relative = doc["relative"] | false;

  if ((!is_relative && (angle < 0 || angle >= 360)) || 
      (is_relative && (angle < -360 || angle > 360))) {
    server.send(400, "text/plain", "Invalid angle");
    return;
  }

  moveToPosition(angle, is_relative);
  server.send(200, "application/json", "{\"angle\":" + String(current_angle) + "}");
}

void registerWithServer() {
  Serial.println("Registring with main server");
  HTTPClient http;
  http.begin(String(server_url) + "/register");
  http.addHeader("Content-Type", "application/json");

  String ip = WiFi.localIP().toString();
  String payload = "{\"type\":\"controller\",\"ip\":\"" + ip + "\"}";
  
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
      lcd.clear();
      lcd.print("Ready");
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
  else {
    Serial.println("The heartbeat was sent");
  }
  
  http.end();
}

void handleLcdUpdate() {
  Serial.println("Been asked to send an LCD update");
  if (!server.hasHeader("Authorization") || 
      "Bearer " + auth_token != server.header("Authorization")) {
    server.send(401, "text/plain", "Unauthorized");
    Serial.println("Unauthorized request of sending an LCD update");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    Serial.println("Missing body");
    return;
  }

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    Serial.println("Invalid JSON");
    return;
  }

  JsonArray lines = doc["lines"].as<JsonArray>();
  if (lines.size() != 2) {
    server.send(400, "text/plain", "Expected two lines");
    Serial.println("Expected two lines");
    return;
  }

  // Update first line
  Serial.println("Updating line 1");
  String line1 = lines[0].as<String>();
  lcd.setCursor(0, 0);
  lcd.print("                "); // Clear line
  lcd.setCursor(0, 0);
  lcd.print(line1);

  // Update second line
  Serial.println("Updating line 2");
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
