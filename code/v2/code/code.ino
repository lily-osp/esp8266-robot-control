// Includes
// Includes: Platform-Specific
#if defined(ESP32)
    #include <WiFi.h>
    #include <WebServer.h>
    #include <ESPmDNS.h>
    #include <DNSServer.h>
    WebServer server(80);
#elif defined(ESP8266)
    #include <ESP8266WiFi.h>
    #include <ESP8266WebServer.h>
    #include <ESP8266mDNS.h>
    #include <DNSServer.h>
    ESP8266WebServer server(80);
#endif

// Includes: General
#include "MotorController.h"
#include "UltrasonicSensor.h"
#include "ObstacleAvoidance.h"
#include "RobotArm.h"
#include "ui_index.h"
#include <EEPROM.h>
#include "setup_ui.h"
#include "main_ui.h"

DNSServer dnsServer;
// Includes end

// Constants
// Constants: EEPROM
constexpr byte DNS_PORT = 53;
constexpr size_t EEPROM_SIZE = 512;
constexpr int WIFI_SSID_ADDR = 0;
constexpr int WIFI_PASS_ADDR = 64;
constexpr int MDNS_NAME_ADDR = 128;
constexpr size_t MAX_SSID_LENGTH = 32;
constexpr size_t MAX_PASS_LENGTH = 64;
constexpr size_t MAX_MDNS_LENGTH = 32;

// Constants: Pin Definitions
#if defined(ESP32)
    constexpr uint8_t MOTOR1_IN1 = 5, MOTOR1_IN2 = 4, MOTOR2_IN1 = 18, MOTOR2_IN2 = 19;
    constexpr uint8_t MOTOR1_ENA = 32, MOTOR2_ENB = 33, TRIG_PIN = 25, ECHO_PIN = 26;
    constexpr uint8_t BASE_PIN = 27, SHOULDER_PIN = 14, ELBOW_PIN = 12, GRIPPER_PIN = 13;
#else
    constexpr uint8_t MOTOR1_IN1 = 5, MOTOR1_IN2 = 4, MOTOR2_IN1 = 0, MOTOR2_IN2 = 2;
    constexpr uint8_t MOTOR1_ENA = 14, MOTOR2_ENB = 12, TRIG_PIN = 13, ECHO_PIN = 15;
    constexpr uint8_t BASE_PIN = 16, SHOULDER_PIN = 3, ELBOW_PIN = 1, GRIPPER_PIN = 9;
#endif
// Constants end

// Variables
String wifiSSID, wifiPassword, mdnsName;

// Objects
MotorController motors(MOTOR1_IN1, MOTOR1_IN2, MOTOR2_IN1, MOTOR2_IN2, MOTOR1_ENA, MOTOR2_ENB);
UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN);
ObstacleAvoidance oa(&motors, &sensor);
RobotArm arm(BASE_PIN, SHOULDER_PIN, ELBOW_PIN, GRIPPER_PIN);

// Function Declarations
void saveSettingsToEEPROM();
void loadSettingsFromEEPROM();
void setupAccessPoint();
bool connectToWiFi();
void setupMDNS();
void handleRoot();
void handleCommand();
void handleSetup();
void setupHTTPRoutes();
void writeStringToEEPROM();
String readStringFromEEPROM();
void executeCommand();
void handleArmCommands();
void startNavigationMode();
void processMovementOrSave();
void processArmMovement();

// Helper Functions: EEPROM
void writeStringToEEPROM(int addr, const String &data) {
    for (int i = 0; i < data.length(); ++i) {
        EEPROM.write(addr + i, data[i]);
    }
    EEPROM.write(addr + data.length(), '\0'); // Null-terminate
}

String readStringFromEEPROM(int addr, int maxLength) {
    String result;
    for (int i = 0; i < maxLength; ++i) {
        char c = EEPROM.read(addr + i);
        if (c == '\0') break;
        result += c;
    }
    return result;
}

// Functions: EEPROM Handling
void saveSettingsToEEPROM() {
    writeStringToEEPROM(WIFI_SSID_ADDR, wifiSSID);
    writeStringToEEPROM(WIFI_PASS_ADDR, wifiPassword);
    writeStringToEEPROM(MDNS_NAME_ADDR, mdnsName);
    EEPROM.commit();
    Serial.println("Settings saved to EEPROM.");
}

void loadSettingsFromEEPROM() {
    wifiSSID = readStringFromEEPROM(WIFI_SSID_ADDR, MAX_SSID_LENGTH);
    wifiPassword = readStringFromEEPROM(WIFI_PASS_ADDR, MAX_PASS_LENGTH);
    mdnsName = readStringFromEEPROM(MDNS_NAME_ADDR, MAX_MDNS_LENGTH);

    Serial.println("Settings loaded:");
    Serial.printf("SSID: %s\nPassword: %s\nmDNS: %s\n", wifiSSID.c_str(), wifiPassword.c_str(), mdnsName.c_str());
}

// Functions: Web Server Handlers
void handleRoot() {
    String uiPage = WiFi.status() == WL_CONNECTED ? REMOTE_UI : SETUP_UI;
    server.send(200, "text/html", uiPage);
}

void handleSetup() {
    if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("mdns")) {
        wifiSSID = server.arg("ssid");
        wifiPassword = server.arg("password");
        mdnsName = server.arg("mdns");

        saveSettingsToEEPROM();
        server.send(200, "text/plain", "Settings saved. Rebooting...");
        delay(2000);
        ESP.restart();
    } else {
        server.send(400, "text/plain", "Invalid input!");
    }
}

void handleCommand() {
    String cmd = server.arg("cmd");
    executeCommand(cmd);
    server.send(200, "text/plain", "Command received: " + cmd);
}

// Functions: Networking
void setupAccessPoint() {
    WiFi.softAP("ConfigAP", "12345678");
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    Serial.printf("AP Mode. Connect to: ConfigAP, IP: %s\n", WiFi.softAPIP().toString().c_str());

    setupHTTPRoutes();
    server.begin();
}

bool connectToWiFi() {
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    Serial.print("Connecting to WiFi");
    for (int attempts = 0; attempts < 10; ++attempts) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connection Failed.");
    return false;
}

void setupMDNS() {
    if (MDNS.begin(mdnsName.c_str())) {
        Serial.printf("mDNS responder started: http://%s.local\n", mdnsName.c_str());
    } else {
        Serial.println("mDNS setup failed!");
    }
}

// Fucntion Movement
// Helper Function: Body
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
        Serial.println("Distance: " + String(distance) + " cm"); 
    }
    else if (command == "stream") { arm.startRecording(); }
    else if (command.length() >= 3) { handleArmCommands(command); }
    else { Serial.println("Invalid Command."); }
}

// Helper Function: Arm
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

// Helper Function: Navigation
void startNavigationMode() {
    oa.enable();
    while (oa.isActive()) {
        oa.navigate();
    }
    motors.stop();
}

// Helper Function: Filter
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

// Helper Function: Arm Predifined
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
// Fucntion Movement End

// Main Setup
void setup() {
    Serial.begin(115200);
    motors.begin();
    sensor.begin();
    oa.begin();
    arm.begin();
    EEPROM.begin(EEPROM_SIZE);

    loadSettingsFromEEPROM();

    if (wifiSSID.isEmpty() || wifiPassword.isEmpty() || mdnsName.isEmpty() || !connectToWiFi()) {
        setupAccessPoint();
    } else {
        setupMDNS();
        setupHTTPRoutes();
        server.begin();
    }
}

void loop() {
    server.handleClient();
    if (oa.isActive()) {
        oa.check();
    }
}
