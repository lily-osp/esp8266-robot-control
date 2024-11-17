#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "MotorController.h"
#include "UltrasonicSensor.h"
#include "ObstacleAvoidance.h"
#include "RobotArm.h"
#include "ui_index.h"

// WiFi credentials
const char* ssid = "SSID";
const char* password = "password";

// pin definitions for ESP8266
const uint8_t MOTOR1_IN1 = 5;  // GPIO5
const uint8_t MOTOR1_IN2 = 4;  // GPIO4
const uint8_t MOTOR2_IN1 = 0;  // GPIO0
const uint8_t MOTOR2_IN2 = 2;  // GPIO2
const uint8_t MOTOR1_ENA = 14; // GPIO14 (PWM capable)
const uint8_t MOTOR2_ENB = 12; // GPIO12 (PWM capable)
const uint8_t TRIG_PIN = 13;   // GPIO13
const uint8_t ECHO_PIN = 15;   // GPIO15
const uint8_t BASE_PIN = 16;   // GPIO16
const uint8_t SHOULDER_PIN = 3; // GPIO3 (RX)
const uint8_t ELBOW_PIN = 1;   // GPIO1 (TX)
const uint8_t GRIPPER_PIN = 9; // GPIO9 (flash)

// Initialize objects
MotorController motors(MOTOR1_IN1, MOTOR1_IN2, MOTOR2_IN1, MOTOR2_IN2, MOTOR1_ENA, MOTOR2_ENB);
UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN);
ObstacleAvoidance oa(&motors, &sensor);
RobotArm arm(BASE_PIN, SHOULDER_PIN, ELBOW_PIN, GRIPPER_PIN);

// Web server setup
ESP8266WebServer server(80);

void handleRoot() {
    server.send(200, "text/html", htmlContent);  // HTML interface
}

void handleCommand() {
    String cmd = server.arg("cmd");  // Command from web interface
    executeCommand(cmd);             // Execute the received command
    server.send(200, "text/plain", "Command received: " + cmd);
}

// Execute command based on the received cmd string
void executeCommand(String command) {
    if (command == "mv") { motors.moveForward(); }
    else if (command == "bk") { motors.moveBackward(); }
    else if (command == "lt") { motors.turnLeft(); }
    else if (command == "rt") { motors.turnRight(); }
    else if (command == "rl") { motors.rotateLeft(); }
    else if (command == "rr") { motors.rotateRight(); }
    else if (command == "st") { motors.stop(); }
    else if (command.startsWith("spd ")) {
        int speed = command.substring(4).toInt();
        motors.setSpeed(speed);
    }
    else if (command == "oa on") { oa.enable(); }
    else if (command == "oa off") { oa.disable(); }
    else if (command == "oa nav") { startNavigationMode(); }
    else if (command == "dist") {
        float distance = sensor.getFilteredDistance(5);
        Serial.println("Distance: " + String(distance) + " cm");  // Send feedback to serial for testing
    }
    else if (command == "stream") { arm.startRecording(); }
    else if (command.length() >= 3) { handleArmCommands(command); }
    else { Serial.println("Invalid Command."); }
}

void handleArmCommands(String command) {
    char type = command.charAt(0);
    char action = command.charAt(2);

    switch (type) {
        case 'b': case 's': case 'e': arm.moveJoint(type, action); break;
        case 'g': arm.moveGripper(action); break;
        case 'm': processMovementOrSave(command, action); break;
        case 'p': if (action == 's') arm.printSavedPositions(); break;
    }
}

void startNavigationMode() {
    oa.enable();
    while (oa.isActive()) {
        oa.navigate();
        // Navigation stops if the stop command is received
    }
    motors.stop();
}

void processMovementOrSave(String command, char action) {
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
        default: Serial.println("Invalid Arm Command.");
    }
}

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
    MDNS.begin("serbot");

    // Setup web server endpoints
    server.on("/", handleRoot);
    server.on("/command", handleCommand);
    server.begin();
}

void loop() {
    server.handleClient();
    if (oa.isActive()) {
        oa.check();  // Continuously check for obstacles if OA is active
    }
}
