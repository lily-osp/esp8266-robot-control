#include <WiFi.h>
#ifdef ESP32
#include <WebServer.h>
#include <ESPmDNS.h>
#else
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#endif
#include "MotorController.h"
#include "UltrasonicSensor.h"
#include "ObstacleAvoidance.h"
#include "RobotArm.h"
#include "ui_index.h"

// WiFi credentials
const char* ssid = "SSID";
const char* password = "password";

// Pin definitions and board configuration
#if defined(ESP32)
const uint8_t MOTOR1_IN1 = 5, MOTOR1_IN2 = 4, MOTOR2_IN1 = 18, MOTOR2_IN2 = 19;
const uint8_t MOTOR1_ENA = 32, MOTOR2_ENB = 33, TRIG_PIN = 25, ECHO_PIN = 26;
const uint8_t BASE_PIN = 27, SHOULDER_PIN = 14, ELBOW_PIN = 12, GRIPPER_PIN = 13;
WebServer server(80);
#else
const uint8_t MOTOR1_IN1 = 5, MOTOR1_IN2 = 4, MOTOR2_IN1 = 0, MOTOR2_IN2 = 2;
const uint8_t MOTOR1_ENA = 14, MOTOR2_ENB = 12, TRIG_PIN = 13, ECHO_PIN = 15;
const uint8_t BASE_PIN = 16, SHOULDER_PIN = 3, ELBOW_PIN = 1, GRIPPER_PIN = 9;
ESP8266WebServer server(80);
#endif

// Initialize objects
MotorController motors(MOTOR1_IN1, MOTOR1_IN2, MOTOR2_IN1, MOTOR2_IN2, MOTOR1_ENA, MOTOR2_ENB);
UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN);
ObstacleAvoidance oa(&motors, &sensor);
RobotArm arm(BASE_PIN, SHOULDER_PIN, ELBOW_PIN, GRIPPER_PIN);

// Function Prototypes
void handleRoot();
void handleCommand();
void executeCommand(const String& command);
void handleArmCommands(const String& command);
void startNavigationMode();
void processMovementOrSave(const String& command, char action);
void processArmMovement(char movement);

// Web server handlers
void handleRoot() {
    server.send(200, "text/html", htmlContent);
}

void handleCommand() {
    String cmd = server.arg("cmd");
    executeCommand(cmd);
    server.send(200, "text/plain", "Command received: " + cmd);
}

// Command execution logic
void executeCommand(const String& command) {
    if (command == "mv") motors.moveForward();
    else if (command == "bk") motors.moveBackward();
    else if (command == "lt") motors.turnLeft();
    else if (command == "rt") motors.turnRight();
    else if (command == "rl") motors.rotateLeft();
    else if (command == "rr") motors.rotateRight();
    else if (command == "st") motors.stop();
    else if (command.startsWith("spd ")) {
        int speed = command.substring(4).toInt();
        motors.setSpeed(speed);
    } else if (command == "oa on") oa.enable();
    else if (command == "oa off") oa.disable();
    else if (command == "oa nav") startNavigationMode();
    else if (command == "dist") {
        float distance = sensor.getFilteredDistance(5);
        Serial.println("Distance: " + String(distance) + " cm");
    } else if (command == "stream") arm.startRecording();
    else if (command.length() >= 3) handleArmCommands(command);
    else Serial.println("Invalid Command.");
}

void handleArmCommands(const String& command) {
    char type = command.charAt(0);
    char action = command.charAt(2);

    switch (type) {
        case 'b': case 's': case 'e': arm.moveJoint(type, action); break;
        case 'g': arm.moveGripper(action); break;
        case 'm': processMovementOrSave(command, action); break;
        case 'p': if (action == 's') arm.printSavedPositions(); break;
        default: Serial.println("Invalid Arm Command.");
    }
}

void startNavigationMode() {
    oa.enable();
    while (oa.isActive()) {
        oa.navigate();
    }
    motors.stop();
}

void processMovementOrSave(const String& command, char action) {
    if (command.startsWith("m pos")) {
        int posNum = command.substring(6).toInt();
        arm.saveCurrentPosition(posNum);
    } else if (command.startsWith("m save")) {
        int posNum = command.substring(6).toInt();
        arm.executeSavedPosition(posNum);
    } else {
        processArmMovement(action);
    }
}

void processArmMovement(char movement) {
    switch (movement) {
        case 'h': arm.moveToHome(); break;
        case 's': arm.performScan(); break;
        case 'p': arm.performPick(); break;
        case 'd': arm.performDrop(); break;
        case 'w': arm.performWave(); break;
        case 'b': arm.performBow(); break;
        case 'r': arm.performReach(); break;
        default: Serial.println("Invalid Arm Movement.");
    }
}

// Setup and main loop
void setup() {
    Serial.begin(115200);

    // Initialize components
    motors.begin();
    sensor.begin();
    oa.begin();
    arm.begin();

    // Setup Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }
    Serial.println("Connected to WiFi. IP: " + WiFi.localIP().toString());

    // Setup mDNS
    if (MDNS.begin("serbot")) {
        Serial.println("mDNS responder started");
    } else {
        Serial.println("Error setting up mDNS responder!");
    }

    // Setup web server
    server.on("/", handleRoot);
    server.on("/command", handleCommand);
    server.begin();
}

void loop() {
    server.handleClient();
    if (oa.isActive()) {
        oa.check();
    }
}
