/*
 * Sapientia Wifi VendoMachine (Fork of JuanFi, https://github.com/ivanalayan15/JuanFi)
 * No Copyright Infringment Intended
 * 
 * Using NodeMCU ESP8266, Servo, Distance Sensor and Mikrotik Router
 * 
 * Admin System
 *   - Initial setup of the system
 *   - Mikrotik connection setup, SSID setup, Hardware settings
 *   - Promo Rates configuration ( Rates, expiration)
 *   - Dashboard, Sales report
 * 
 * Supported ESP8266 
 * 
*/

//increase always when publishing a new version for tracking
#define CURRENT_VERSION "0.2"

/* Debugging Utilities */
//Use for debugging application
#define APPLICATION_DEBUG_MODE 1

#if (APPLICATION_DEBUG_MODE)
#define APP_DEBUG_PRINT(fmt, ...) Serial.printf(fmt "\r\n", ##__VA_ARGS__)
#else
  #define APP_DEBUG_PRINT(fmt, ...)
#endif

// #pragma GCC diagnostic ignored "-Wwrite-strings"
#define DEBUG_ESP_PORT
#define DEBUG_ESP_HTTP_CLIENT
#include <ESP8266TelnetClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <Arduino.h>
#include <flash_hal.h>

#include <LittleFS.h>
#include <EEPROM.h>
#include <FS.h>
#include <base64.h>
#include <Servo.h>

/* 
 * Delimiter for rates.data = #
 * Delimiter for system.data = |
 */

// Start here.
int SENSOR_1_PIN = D6;
int SENSOR_2_PIN = D7;
int SENSOR_3_PIN = D3;
int SERVO_1_PIN = D1;
int SERVO_1_CW_VAL = 120;     //servo1ClockwiseVal
int SERVO_1_ACW_VAL = 120;    //servo1AntiClockwiseVal
int DEBUGLED_1_PIN = D0;      //Green LED 
int DEBUGLED_2_PIN = D5;

/* 
 * When triggering ALL sensors, their assertion in hardware.
 * Add a delay before checking each interrupt assertion value.
 * Can be adjusted.
 * Can also be seen as de-bouncer.
 */
long DELAY_BEFORE_READING_SENSORS_ASSERT_VAL = 20; 

//Put here your RouterAP IP address, and login details
IPAddress mikrotikRouterIp(10, 0, 0, 1);

//RouterAP web/telnet credential
String user = "botefi";
String pwd = "test";
String pwdconf = "test";

//RouterAP ssd
String ssid = "Sapientia Wifi VendoMachine";
//RouterAP Wifi credential
String password = "";
String adminAuth = "";
String vendorName = "";

//Put here ESP8266 IP Address for RouterAP
IPAddress local_IP(10, 0, 0, 100);
IPAddress gateway(192, 168, 88, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 88, 1);  // this is optional

//Put here ESP8266 IP Address for Wifi Station (Setup)
IPAddress apIP(192, 168, 10, 15);
// ESP8266 Wifi Station User and PW
String ADMIN_USER = "botefi";
String ADMIN_PW = "test";
String ADMIN_PW_CONF = "test";

WiFiClient client2;
WiFiClient client;
ESP8266telnetClient tc(client);
ESP8266WebServer server(80);
const byte DNS_PORT = 53;
DNSServer dnsServer;

// Internal Only
int CHECK_INTERNET_CONNECTION = 0;
int IP_ADDRESS_MODE = 0;
int VOUCHER_LOGIN_OPTION = 0;
int VOUCHER_VALIDITY_OPTION = 0;
String VOUCHER_PROFILE = "default";
String VOUCHER_PREFIX = "P";
int SETUP_FINISH = 0;

/* EEPROM Layout (??) */
const int LIFETIME_BOTTLE_COUNT_ADDRESS = 0;
const int CURRENT_BOTTLE_COUNT_ADDRESS = 5;
const int CUSTOMER_COUNT_ADDRESS = 10;
const int RANDOM_MAC_ADDRESS = 15;
const int BACKUP_CONFIG_LENGTH_INDEX = 20;


/* 
 * Sensors ISR
 */
//For single tracking
volatile bool isSensorsTriggered = false;
//For individual tracking
volatile int isSensor1Activated = 0;
volatile int isSensor2Activated = 0;
volatile int isSensor3Activated = 0;

// Hardware-related settings
int SENSOR_1_ASSERT_VAL = 1;
int SENSOR_2_ASSERT_VAL = 1;
int SENSOR_3_ASSERT_VAL = 1;

IRAM_ATTR void sensor1ISR() {
  isSensorsTriggered = true;
  isSensor1Activated = SENSOR_1_ASSERT_VAL;
}
IRAM_ATTR void sensor2ISR() {
  isSensorsTriggered = true;
  isSensor2Activated = SENSOR_2_ASSERT_VAL;
}
IRAM_ATTR void sensor3ISR() {
  isSensorsTriggered = true;
  isSensor3Activated = SENSOR_3_ASSERT_VAL;
}

void sensorsClearISRFlags() {
  //Clear Flags
  isSensorsTriggered = false;
  isSensor1Activated = !SENSOR_1_ASSERT_VAL;
  isSensor2Activated = !SENSOR_2_ASSERT_VAL;
  isSensor3Activated = !SENSOR_3_ASSERT_VAL;
}
void sensorsInterruptAttach()
{
  sensorsClearISRFlags();
  // attachInterrupt(digitalPinToInterrupt(SENSOR_1_PIN), sensor1ISR, CHANGE);  //Comment for now since sensor 1 is not working
  attachInterrupt(digitalPinToInterrupt(SENSOR_2_PIN), sensor2ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SENSOR_3_PIN), sensor3ISR, CHANGE);
}
void sensorsInterruptDetach()
{
  sensorsClearISRFlags();
  // attachInterrupt(digitalPinToInterrupt(SENSOR_1_PIN));  //Comment for now since sensor 1 is not working
  detachInterrupt(digitalPinToInterrupt(SENSOR_2_PIN));
  detachInterrupt(digitalPinToInterrupt(SENSOR_3_PIN));
}
// Old ISR
// void ICACHE_RAM_ATTR BottleInserted()    
// {
//   if(isBottleDetectionActive){
//     bottle = bottle + 1;  
//     bottlesChange = 1;
//   }
// }
/* End of ISR related Functions */
///////////////Unverified Code Below/////////////////////////

int bottle = 0;
int processBottle = 0;
int totalBottle = 0;
boolean isNewVoucher = false;
int bottlesChange = 0;
String currentActiveVoucher = "";
String currentMacAttempt = "";
int timeToAdd = 0;
unsigned long targetMilis = 0;

bool isBottleDetectionActive = false;
bool isReadyToDispense = false;
bool isDispensingTimeoutExpired = false;
bool mikrotikConnectionSuccess = false;
String currentMacAddress = "";
String currentIpAddress = "";
String HARDWARE_TYPE = "NodeMCU v1 ESP8266";

int bottleWaiting = 0;
long lastLinkStatusCheck = 0;

typedef struct {
  String rateName;
  int price;
  int minutes;
  int validity;
  int dataLimit;
  String profileName;
} PromoRates;

typedef struct {
  String mac;
  long unlockTime;
  int attemptCount;
} AttemptMacAddress;

int attemptedMaxCount = 20;
AttemptMacAddress attempted[20];
PromoRates rates[100];
int ratesCount = 0;
int currentValidity = 0;
int currentDataLimit = 0;
String currentRateProfile = "";

int MAX_WAIT_BOTTLE_SEC = 30000;
int BOTTLE_INSERT_BAN_COUNT = 0;
int INSERTBOTTLE_BAN_MINUTES = 0;

const int WIFI_CONNECT_TIMEOUT = 180000;
const int WIFI_CONNECT_DELAY = 500;

bool networkConnected = false;

/* Dispenser Control + M995-related Functions */
Servo servo1;  // create servo object to control a servo
enum {
  MG995_CLOCKWISE = 0,
  MG995_ANTICLOCKWISE
} MG995_Direction;
int MG995Rotate (Servo &servo_obj, int distanceTime, int direction)
{
  if(direction == MG995_CLOCKWISE) {
    servo_obj.write(0); //Rotate Clockwise
  } else if (direction == MG995_ANTICLOCKWISE) {
    servo_obj.write(180); //Rotate Anti Clockwise
  }
  delay(distanceTime);
  servo_obj.write(91); //Stop
  return 1;
}
// For now opening dispenser will rotate servo clockwise
bool isDispenserOpen = 0;
bool openDispenser() {
  if (!isDispenserOpen) {
    //Open Dispenser
    MG995Rotate(servo1, SERVO_1_CW_VAL, MG995_CLOCKWISE);
    isDispenserOpen = true;
    return true;
  }
  else {
    APP_DEBUG_PRINT("Dispenser is already open.");
    return false;
  }
}
bool closeDispenser() {
  if (isDispenserOpen) {
    //Close Dispenser
    MG995Rotate(servo1, SERVO_1_ACW_VAL, MG995_CLOCKWISE);
    isDispenserOpen = false;
    return true;
  }
  else {
    APP_DEBUG_PRINT("Dispenser is still closed.");
    return false;
  }
}
/* End of Dispenser Control + M995-related Function */

/* Important Functions for Program Logic */
bool checkIfSystemIsAvailable() {
  if (!mikrotikConnectionSuccess) {
    String keys[] = { "status", "errorCode" };
    String values[] = { "false", "insert.bottle.notavailable" };
    setupCORSPolicy();
    server.send(200, "application/json", toJson(keys, values, 2));
    return false;
  } else {
    return true;
  }
}

void handleSystemAbnormal() {
  Serial.println("AP disconnected!!!!!!!!!!!!!!!");
  mikrotikConnectionSuccess = false;

  //Reconnect after 30 seconds
  delay(30000);
  ESP.restart();
}

// Check Internet Connection
String INTERNET_CHECK_URL = "http://ifconfig.me";
bool hasInternetConnect() {
  HTTPClient http;

  http.begin(client2, INTERNET_CHECK_URL);  //HTTP
  http.addHeader("User-Agent", "curl/7.55.1");
  int httpCode = http.GET();

  if (httpCode > 0) {
    const String& payload = http.getString();
    Serial.println("[CheckNet] received payload:\n<<");
    Serial.println(payload);
    Serial.println(">>");
    Serial.println("[CheckNet] Internet connection detected!");
    http.end();

    return true;
  } else {
    Serial.println("[CheckNet] Internet connection not detected!");
    Serial.printf("[CheckNet] [HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    http.end();

    return false;
  }
}

void addAttemptToInsertBottle() {
  if (BOTTLE_INSERT_BAN_COUNT > 0) {
    int currentMacIndex = -1;
    int availableIndex = -1;
    for (int i = 0; i < attemptedMaxCount; i++) {
      if (attempted[i].mac == currentMacAttempt) {
        currentMacIndex = i;
        break;
      } else if (attempted[i].mac == "") {
        availableIndex = i;
      }
    }
    Serial.println(currentMacAttempt);
    Serial.println(currentMacIndex);
    Serial.println(availableIndex);
    if (currentMacIndex > -1) {
      attempted[currentMacIndex].attemptCount++;

      if (attempted[currentMacIndex].attemptCount >= BOTTLE_INSERT_BAN_COUNT) {
        long curMil = millis();
        attempted[currentMacIndex].unlockTime = curMil + (INSERTBOTTLE_BAN_MINUTES * 60000);
        Serial.print("Unlock time: ");
        Serial.println(attempted[currentMacIndex].unlockTime);
      }
    } else {
      if (availableIndex > -1) {
        attempted[availableIndex].mac = currentMacAttempt;
        attempted[availableIndex].attemptCount++;
      }
    }
  }
}

void clearAttemptToInsertBottle() {
  if (BOTTLE_INSERT_BAN_COUNT > 0) {
    for (int i = 0; i < attemptedMaxCount; i++) {
      if (attempted[i].mac == currentMacAttempt) {
        attempted[i].mac = "";
        attempted[i].unlockTime = 0;
        attempted[i].attemptCount = 0;
        break;
      }
    }
  }
}

void checkBottle() {

  if (!checkIfSystemIsAvailable()) {
    return;
  }

  String voucher = server.arg("voucher");
  if (!validateVoucher(voucher)) {
    return;
  }

  if (isDispensingTimeoutExpired) {
    String keys[] = { "status", "errorCode" };
    String values[] = { "false", "bottles.wait.expired" };
    setupCORSPolicy();
    server.send(200, "application/json", toJson(keys, values, 2));
    return;
  }

  if (!isReadyToDispense) {
    totalBottle += processBottle;
    timeToAdd = calculateAddTime();
    String keys[] = { "status", "newBottle", "timeAdded", "totalBottle", "validity", "data" };
    char bottleStr[16];
    itoa(processBottle, bottleStr, 10);
    char timeToAddStr[16];
    itoa(timeToAdd, timeToAddStr, 10);
    char totalBottleStr[16];
    itoa(totalBottle, totalBottleStr, 10);
    char validityStr[16];
    itoa(currentValidity, validityStr, 10);
    char currentDataLimitStr[16];
    itoa(currentDataLimit, currentDataLimitStr, 10);
    String values[] = { "true", bottleStr, timeToAddStr, totalBottleStr, validityStr, currentDataLimitStr };
    enableBottleDetection();
    setupCORSPolicy();
    server.send(200, "application/json", toJson(keys, values, 6));
  } else {
    String keys[] = { "status", "errorCode", "remainTime", "timeAdded", "totalBottle", "waitTime", "validity", "data" };
    char remainTimeStr[20];
    long remain = targetMilis - millis();
    itoa(remain, remainTimeStr, 10);
    char timeToAddStr[16];
    itoa(timeToAdd, timeToAddStr, 10);
    char totalBottleStr[16];
    itoa(totalBottle, totalBottleStr, 10);
    char waitTimeStr[16];
    itoa(MAX_WAIT_BOTTLE_SEC, waitTimeStr, 10);
    char validityStr[16];
    itoa(currentValidity, validityStr, 10);
    char currentDataLimitStr[16];
    itoa(currentDataLimit, currentDataLimitStr, 10);
    String values[] = { "false", "bottle.not.inserted", remainTimeStr, timeToAddStr, totalBottleStr, waitTimeStr, validityStr, currentDataLimitStr };
    setupCORSPolicy();
    server.send(200, "application/json", toJson(keys, values, 8));
  }
}

void useVoucher() {

  if (!checkIfSystemIsAvailable()) {
    Serial.printf("[%s] System Unavailable.\r\n", __FUNCTION__);
    return;
  }

  String voucher = server.arg("voucher");
  if (!validateVoucher(voucher)) {
    Serial.printf("[%s] Invalid Voucher.\r\n", __FUNCTION__);
    return;
  }

  disableBottleDetection();

  if (timeToAdd > 0) {
    clearAttemptToInsertBottle();
    registerNewVoucher(voucher);
    updateStatisticToEE();
    addTimeToVoucher(voucher, timeToAdd);
  } else {
    addAttemptToInsertBottle();
  }
  String keys[] = { "status", "totalBottle", "timeAdded", "validity" };
  char totalBottleStr[16];
  itoa(totalBottle, totalBottleStr, 10);
  char timeToAddStr[16];
  itoa(timeToAdd, timeToAddStr, 10);
  char validityStr[16];
  itoa(currentValidity, validityStr, 10);
  String values[] = { "true", totalBottleStr, timeToAddStr, validityStr };
  resetGlobalVariables();
  setupCORSPolicy();
  isReadyToDispense = false;
  server.send(200, "application/json", toJson(keys, values, 4));
}

void updateStatisticToEE() {
  int lifeTimeBottleCount = eeGetInt(LIFETIME_BOTTLE_COUNT_ADDRESS);
  lifeTimeBottleCount += totalBottle;
  eeWriteInt(LIFETIME_BOTTLE_COUNT_ADDRESS, lifeTimeBottleCount);
  int bottleCount = eeGetInt(CURRENT_BOTTLE_COUNT_ADDRESS);
  bottleCount += totalBottle;
  eeWriteInt(CURRENT_BOTTLE_COUNT_ADDRESS, bottleCount);
  int customerCount = eeGetInt(CUSTOMER_COUNT_ADDRESS);
  customerCount++;
  eeWriteInt(CUSTOMER_COUNT_ADDRESS, customerCount);
}

bool validateVoucher(String voucher) {
  if (voucher != currentActiveVoucher) {
    String keys[] = { "status", "errorCode" };
    String values[] = { "false", "insertbottle.busy" };
    setupCORSPolicy();
    server.send(200, "application/json", toJson(keys, values, 2));
    return false;
  } else {
    return true;
  }
}

void topUp() { 
  //Internet Connection Check
  bool hasInternetConnection = true;
  if (CHECK_INTERNET_CONNECTION == 1) {
    hasInternetConnection = hasInternetConnect();
  }
  if (!hasInternetConnection) {
    APP_DEBUG_PRINT("No Internet Connection");
    String keys[] = { "status", "errorCode" };
    String values[] = { "false", "no.internet.detected" };
    setupCORSPolicy();
    server.send(200, "application/json", toJson(keys, values, 2));
    return;
  }

  //Check if RouterAP is connected
  if (!checkIfSystemIsAvailable()) {
    Serial.printf("[%s] System Unavailable.\r\n", __FUNCTION__);
    return;
  }

  String macAdd = server.arg("mac");
  //Check if MAC Address is BANNED. 
  if (!checkMacAddress(macAdd)) {
    String keys[] = { "status", "errorCode" };
    String values[] = { "false", "insert.bottle.banned" };
    setupCORSPolicy();
    server.send(200, "application/json", toJson(keys, values, 2));
    return;
  }
  currentMacAttempt = macAdd;
  String voucher = server.arg("voucher");
  if (currentActiveVoucher != "" && !validateVoucher(voucher)) {
    return;
  }

  currentValidity = 0;
  if (voucher == "") {
    voucher = generateVoucher();
    isNewVoucher = true;
  } else {
    if (isNewVoucher && voucher == currentActiveVoucher) {
      isNewVoucher = true;  //??? isNewVoucher will stay the same. Maybe remove isNewVoucher from if()?
    } else {
      isNewVoucher = false;
    }
  }
  String keys[] = { "status", "voucher" };
  int voucherLength = voucher.length() + 1;
  char voucherChar[voucherLength];
  voucher.toCharArray(voucherChar, voucherLength);
  String values[] = { "true", voucherChar };
  if (voucher != currentActiveVoucher) {
    resetGlobalVariables();
    enableBottleDetection();
    currentActiveVoucher = voucher;
  }
  setupCORSPolicy();
  server.send(200, "application/json", toJson(keys, values, 2));
}

boolean checkMacAddress(String mac) {
  bool isValid = true;
  if (BOTTLE_INSERT_BAN_COUNT > 0) {
    Serial.print("Checking mac if valid ");
    Serial.println(mac);
    for (int i = 0; i < attemptedMaxCount; i++) {
      if (attempted[i].mac != "") {
        long curMil = millis();
        if (attempted[i].unlockTime > 0 && attempted[i].unlockTime <= curMil) {
          Serial.print(attempted[i].mac);
          Serial.println(" unlocking mac address...");
          attempted[i].mac = "";
          attempted[i].attemptCount = 0;
          attempted[i].unlockTime = 0;
        } else if (attempted[i].mac == mac) {
          Serial.print("Mac address has previous attempt");
          Serial.println(attempted[i].attemptCount);
          Serial.println(BOTTLE_INSERT_BAN_COUNT);
          if (attempted[i].attemptCount >= BOTTLE_INSERT_BAN_COUNT) {
            isValid = false;
            Serial.print(mac);
            Serial.println(" mac address currently banned");
          }
        }
      }
    }
  }
  return isValid;
}

String generateVoucher() {
  int randomNumber = random(1000, 9999);
  String voucher = VOUCHER_PREFIX + String(randomNumber);
  return voucher;
}

void registerNewVoucher(String voucher) {
  /* This adds a hotspot user */
  String addHotspotUserScript = "/ip hotspot user add name=";
  addHotspotUserScript += voucher;
  addHotspotUserScript += " limit-uptime=0 comment=0";
  if (VOUCHER_LOGIN_OPTION == 1) {
    addHotspotUserScript += " password=";
    addHotspotUserScript += voucher;
  }
  if (VOUCHER_PROFILE != "" && VOUCHER_PROFILE != "default") {
    addHotspotUserScript += " profile=";
    addHotspotUserScript += VOUCHER_PROFILE;
  }
  sendCommand(addHotspotUserScript);
}

void addTimeToVoucher(String voucher, int secondsToAdd) {

  String script = ":global lpt; :global nlu; :set lpt [/ip hotspot user get ";
  script += voucher;
  script += " limit-uptime]; ";
  sendCommand(script);
  script = ":set nlu [($lpt+";
  script += (secondsToAdd / 60);
  script += "m)]; ";
  script += "/ip hotspot user set limit-uptime=$nlu comment=\"";
  script += currentValidity;
  script += "m,";
  script += String(totalBottle);
  if (isNewVoucher) {
    script += ",0,";
  } else {
    script += ",1,";
  }
  script += vendorName;
  script += "\" ";

  if (currentRateProfile != "") {
    script += "profile=";
    script += currentRateProfile;
    script += " ";
  }
  script += voucher;
  script += "; ";
  sendCommand(script);

  if (currentDataLimit != 0) {
    String script = ":global tdtl; :global dtl [/ip hotspot user get VOUCHER_HERE  limit-bytes-total];";
    script.replace("VOUCHER_HERE", voucher);
    sendCommand(script);
    script = ":if ($dtl>0) do={ :set tdtl [(dtl+DATA_LIMIT_HERE*1048576)] } else { :set tdtl [(DATA_LIMIT_HERE*1048576)] }; /ip hotspot user set limit-bytes-total=$tdtl VOUCHER_HERE";
    script.replace("VOUCHER_HERE", voucher);
    script.replace("DATA_LIMIT_HERE", String(currentDataLimit));
    sendCommand(script);
  }
}

/* RouterAP (Mikrotik) Send Command Function */
void sendCommand(String script) {
  Serial.println(script);
  int scriptLength = script.length() + 1;
  char command[scriptLength];
  script.toCharArray(command, scriptLength);
  tc.sendCommand(command);
}
void setupCORSPolicy() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Max-Age", "10000");
  server.sendHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.sendHeader("Access-Control-Allow-Credentials", "false");
}
void resetGlobalVariables() {
  currentActiveVoucher = "";
  timeToAdd = 0;
  totalBottle = 0;
  currentDataLimit = 0;
  currentRateProfile = "";
}

void enableBottleDetection() {
  delay(200);
  processBottle = 0;

  //Ready to Dispense Bottle
  isReadyToDispense = true;
  isBottleDetectionActive = true;

  //Enable Interrupt 
  sensorsInterruptAttach();
  if(!isDispenserOpen) openDispenser();
  targetMilis = millis() + MAX_WAIT_BOTTLE_SEC;
}
void disableBottleDetection() {
  isBottleDetectionActive = false;

  //Remove Interrupt 
  sensorsInterruptDetach();
  closeDispenser();
}

int calculateAddTime() {
  int totalTime = 0;
  currentValidity = 0;
  currentDataLimit = 0;
  int remainingBottle = totalBottle;
  int highestPrice = 0;
  while (remainingBottle > 0) {
    int candidatePrice = 0;
    int candidateIndex = -1;
    for (int i = 0; i < ratesCount; i++) {
      if (rates[i].price <= remainingBottle) {
        if (candidatePrice < rates[i].price) {
          candidatePrice = rates[i].price;
          candidateIndex = i;
        }
      }
    }
    if (candidateIndex != -1) {
      //when extend time and voucher validity option is  First Validity + extend time, add the extend time instead of validity
      if ((!isNewVoucher) && VOUCHER_VALIDITY_OPTION == 1) {
        currentValidity += rates[candidateIndex].minutes;
      } else {
        currentValidity += rates[candidateIndex].validity;
      }

      //get the highest user rate profile from the rates
      if (highestPrice < rates[candidateIndex].price) {
        highestPrice = rates[candidateIndex].price;
        if (rates[candidateIndex].profileName != "") {
          currentRateProfile = rates[candidateIndex].profileName;
        }
      }

      currentDataLimit += rates[candidateIndex].dataLimit;

      totalTime += rates[candidateIndex].minutes;
      remainingBottle -= rates[candidateIndex].price;
    } else {
      break;
    }
  }
  return totalTime * 60;
}

void handleGenerateVouchers() {

  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }
  int amount = server.arg("amt").toInt();
  int qty = server.arg("qty").toInt();
  int addToSales = server.arg("sales").toInt();
  String prefix = server.arg("pfx");
  String voucherGenerated = "";

  for (int i = 0; i < qty; i++) {
    int randomNumber = random(1000, 9999);
    String voucher = prefix + String(randomNumber);
    totalBottle = amount;
    timeToAdd = calculateAddTime();
    registerNewVoucher(voucher);
    if (addToSales == 1) {
      updateStatisticToEE();
    }
    addTimeToVoucher(voucher, timeToAdd);
    if (i > 0) {
      voucherGenerated += "#";
    }
    voucherGenerated += voucher;
  }
  String returnData = vendorName + "|" + amount + "|" + String(timeToAdd) + "|" + voucherGenerated;
  server.send(200, "text/pain", returnData);
}

void loginMirotik() {

  //WHICH CHARACTER SHOULD BE INTERPRETED AS "PROMPT"?
  tc.setPromptChar('>');

  //this is to trigger manually the login
  //since it could be a problem to attach the serial monitor while negotiating with the server (it cause the board reset)
  //remove it or replace it with a delay/wait of a digital input in case you're not using the serial monitors
  Serial.print("Logging in to mikrotik ");
  Serial.print(mikrotikRouterIp);
  Serial.print(" using ");
  Serial.print(user);
  Serial.print(" / ");
  Serial.println(pwd);
  delay(3000);

  //PUT HERE YOUR USERNAME/PASSWORD
  mikrotikConnectionSuccess = tc.login(mikrotikRouterIp, user.c_str(), pwd.c_str());
  if (mikrotikConnectionSuccess) {
      Serial.println("Login to mikrotek router success");
    } else {
      //Temporary fix for those cannot connect to mikrotik
      mikrotikConnectionSuccess = true;
      Serial.println("Warning, Failed to login in mikrotek router, please check mikrotik log");
      Serial.println("Note: Just ignore due to race condition with checking of prompt.");
    }
}

void populateRates() {

  Serial.println("Loading promo rates");
  String data = readFile("/admin/config/rates.data");
  Serial.print("Data: ");
  Serial.println(data);
  int dataLength = data.length() + 1;
  char dataChar[dataLength];
  String rows[100];
  ratesCount = split(rows, data, '|');

  for (int i = 0; i < ratesCount; i++) {
    Serial.print("Data: ");
    Serial.println(rows[i]);
    String column[6];
    split(column, rows[i], '#');
    rates[i].rateName = column[0];
    rates[i].price = column[1].toInt();
    rates[i].minutes = (column[2]).toInt();
    rates[i].validity = (column[3]).toInt();
    rates[i].dataLimit = (column[4]).toInt();
    rates[i].profileName = column[5];
  }
}

void populateSystemConfiguration() {
  //Read atleast 4 bytes on system.data offset in EEPROM
  int backupLength = eeGetInt(BACKUP_CONFIG_LENGTH_INDEX);

  if (backupLength > 0) {
    Serial.print("Backup data found ");
    Serial.println(backupLength);
    String backupData = eeReadString(BACKUP_CONFIG_LENGTH_INDEX + 5, backupLength);
    Serial.println(backupData);
    handleFileWrite("/admin/config/system.data", backupData);
    eeWriteInt(BACKUP_CONFIG_LENGTH_INDEX, 0);
    Serial.print("Backup data restored!, restarting....");
    ESP.restart();
    return;
  }

  Serial.println("Loading system configuration");
  String data = readFile("/admin/config/system.data"); 

  if (data.isEmpty()) {
    Serial.println("system.data is empty or does not exists.");
  }
  else {
    Serial.print("Data: ");
    Serial.println(data);
  }

  /* Parse system.data 
   * Make sure this is consistent with postData = createParam([])
   */
  int rowSize = 31;
  String rows[rowSize];

  split(rows, data, '|');
  
  //Parse based on arrangement
  vendorName = rows[0];
  SETUP_FINISH = rows[1].toInt();
  ssid = rows[2];
  password = rows[3];
  IP_ADDRESS_MODE = rows[4].toInt();

  if (IP_ADDRESS_MODE == 1) {
    
    //localIpAddress
    String localIpAddress[4];
    split(localIpAddress, rows[5], '.');
    local_IP[0] = localIpAddress[0].toInt();
    local_IP[1] = localIpAddress[1].toInt();
    local_IP[2] = localIpAddress[2].toInt();
    local_IP[3] = localIpAddress[3].toInt();

    //gatewayIp
    String gatewayIpAddress[4];
    split(gatewayIpAddress, rows[6], '.');
    gateway[0] = gatewayIpAddress[0].toInt();
    gateway[1] = gatewayIpAddress[1].toInt();
    gateway[2] = gatewayIpAddress[2].toInt();
    gateway[3] = gatewayIpAddress[3].toInt();

    //subnetMask
    String subnetAddress[4];
    split(gatewayIpAddress, rows[7], '.');
    subnet[0] = subnetAddress[0].toInt();
    subnet[1] = subnetAddress[1].toInt();
    subnet[2] = subnetAddress[2].toInt();
    subnet[3] = subnetAddress[3].toInt();

    //dnsServer
    String primaryDNSAddress[4];
    split(primaryDNSAddress, rows[8], '.');
    primaryDNS[0] = primaryDNSAddress[0].toInt();
    primaryDNS[1] = primaryDNSAddress[1].toInt();
    primaryDNS[2] = primaryDNSAddress[2].toInt();
    primaryDNS[3] = primaryDNSAddress[3].toInt();
  }

  // mikrotikIp
  {
    String ip[10];
    split(ip, rows[9], '.');

    mikrotikRouterIp[0] = ip[0].toInt();
    mikrotikRouterIp[1] = ip[1].toInt();
    mikrotikRouterIp[2] = ip[2].toInt();
    mikrotikRouterIp[3] = ip[3].toInt();
  }
  
  // Mikrotik/Router
  user = rows[10];
  pwd = rows[11];
  pwdconf = rows[12];

  // WebGUI Admin Username and Password
  ADMIN_USER = rows[13];
  ADMIN_PW = rows[14];
  ADMIN_PW_CONF = rows[15];
  adminAuth = base64::encode(ADMIN_USER + ":" + ADMIN_PW);

  CHECK_INTERNET_CONNECTION = rows[16].toInt();
  VOUCHER_PREFIX = rows[17];
  VOUCHER_LOGIN_OPTION = rows[18].toInt();
  VOUCHER_PROFILE = rows[19];
  VOUCHER_VALIDITY_OPTION = rows[20].toInt();

  // Hardware Settings
  SENSOR_1_ASSERT_VAL = rows[21].toInt();
  SENSOR_2_ASSERT_VAL = rows[22].toInt();
  SENSOR_3_ASSERT_VAL = rows[23].toInt();
  SENSOR_1_PIN = rows[24].toInt();
  SENSOR_2_PIN = rows[25].toInt();
  SENSOR_3_PIN = rows[26].toInt();
  SERVO_1_PIN = rows[27].toInt();
  SERVO_1_CW_VAL = rows[28].toInt();     //servo1ClockwiseVal
  SERVO_1_ACW_VAL = rows[29].toInt();    //servo1AntiClockwiseVal
  DEBUGLED_1_PIN = rows[30].toInt();

  /* End parse system.data */
}
/*
 * Wifi-related Functions
 */
void checkSTAConnection() {
  wl_status_t status = WiFi.status();

  switch (status) {
    case WL_CONNECTED:
      Serial.println("STA: Connected");
      Serial.print("STA RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      break;
    case WL_DISCONNECTED:
    case WL_CONNECT_FAILED:
    case WL_NO_SSID_AVAIL:
      Serial.println("STA: Disconnected or Connection Failed");
      // You might want to attempt reconnection here:
      // WiFi.begin(ssid_sta, password_sta);
      break;
    default:
      Serial.print("STA: Other Status: ");
      Serial.println(status);
      break;
  }
} //End of Wifi-related functions
/*
 *  EEPROM Handling Functions
 */
void eeWriteInt(int pos, int val) {
  byte* p = (byte*)&val;
  EEPROM.write(pos, *p);
  EEPROM.write(pos + 1, *(p + 1));
  EEPROM.write(pos + 2, *(p + 2));
  EEPROM.write(pos + 3, *(p + 3));
  EEPROM.commit();
}

void eeWriteString(int addr, String val) {
  int str_len = val.length() + 1;
  for (int i = addr; i < str_len + addr; ++i) {
    EEPROM.write(i, val.charAt(i - addr));
  }
  EEPROM.write(str_len + addr, '\0');
  EEPROM.commit();
}

String eeReadString(int addr, int str_len) {
  String val = "";
  for (int i = addr; i < str_len + addr; ++i) {
    val += String(char(EEPROM.read(i)));
  }
  return val;
}

/* Read atleast 4 bytes */
int eeGetInt(int pos) {
  int val;
  byte* p = (byte*)&val;
  *p = EEPROM.read(pos);
  *(p + 1) = EEPROM.read(pos + 1);
  *(p + 2) = EEPROM.read(pos + 2);
  *(p + 3) = EEPROM.read(pos + 3);
  if (val < 0) {
    return 0;
  } else {
    return val;
  }
}
/* End of EEPROM Handling Functions */

/*
 * Filesystem-related Functions
 */
bool handleFileRead(String path) {  // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";  // If a folder is requested, send the index file
  String contentType = getContentType(path);     // Get the MIME type
  String pathWithGz = path + ".gz";
  if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) {  // If the file exists, either as a compressed archive, or normal
    if (LittleFS.exists(pathWithGz))                           // If there's a compressed version available
      path += ".gz";                                           // Use the compressed version
    File file = LittleFS.open(path, "r");                      // Open the file
    size_t sent = server.streamFile(file, contentType);        // Send it to the client
    file.close();                                              // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;  // If the file doesn't exist, return false
}

bool handleFileWrite(String path, String content) {  // send the right file to the client (if it exists)
  Serial.println("handleFileWrite: " + path);
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "w");
    int bytesWritten = file.print(content);
    if (bytesWritten <= 0) {
      return false;
    }
    file.close();
    Serial.println(String("Write file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;  // If the file doesn't exist, return false
}

String readFile(String path) {
  String result;
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    String content = file.readStringUntil('\n');
    file.close();
    return content;
  }
  return result;
}
/* End of Filesystem-related functions */

/* 
 * Webserver Handling Functions
 */
void handleAdminPage() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }

  handleFileRead("/admin/system-config.html");
}

void handleAdminGeneratedVoucherPage() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }

  handleFileRead("/admin/voucher-generate.html");
}

void handleHealth() {
  setupCORSPolicy();
  server.send(200, "text/plain", "ok");
}

void handleLogout() {
  server.sendHeader("WWW-Authenticate", "Basic realm=\"Secure\"");
  server.send(401, "text/html", "<html>Authentication failed</html>");
}

void handleJquerySript() {
  handleFileRead("/admin/js/jquery.min.js");
}

void handleUserGetRates() {
  setupCORSPolicy();
  handleFileRead("/admin/config/rates.data");
}

void handleAdminGetRates() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }
  handleFileRead("/admin/config/rates.data");
}

void handleAdminSaveRates() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }

  String data = server.arg("data");
  handleFileWrite("/admin/config/rates.data", data);
  populateRates();
  server.send(200, "text/plain", "ok");
}

void handleAdminSaveSystemConfig() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }

  String data = server.arg("data");
  handleFileWrite("/admin/config/system.data", data);
  server.send(200, "text/plain", "ok");
  delay(2000);
  ESP.restart();
}

void handleAdminGetSystemConfig() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }

  handleFileRead("/admin/config/system.data");
}

void handleAdminResetStats() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }

  String type = server.arg("type");
  if (type == "lifeTimeCount") {
    eeWriteInt(LIFETIME_BOTTLE_COUNT_ADDRESS, 0);
  } else if (type == "bottleCount") {
    eeWriteInt(CURRENT_BOTTLE_COUNT_ADDRESS, 0);
  } else if (type == "customerCount") {
    eeWriteInt(CUSTOMER_COUNT_ADDRESS, 0);
  }
  server.send(200, "text/plain", "ok");
}

void handleAdminDashboard() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }

  long upTime = millis();
  int lifeTimeBottleCount = eeGetInt(LIFETIME_BOTTLE_COUNT_ADDRESS);
  int bottleCount = eeGetInt(CURRENT_BOTTLE_COUNT_ADDRESS);
  int customerCount = eeGetInt(CUSTOMER_COUNT_ADDRESS);
  bool hasInternetConnection = true;
  if (CHECK_INTERNET_CONNECTION == 1) {
    hasInternetConnection = hasInternetConnect();
  }
  String data = "";
  data += String(upTime);
  data += String("|");
  data += String(lifeTimeBottleCount);
  data += String("|");
  data += String(bottleCount);
  data += String("|");
  data += String(customerCount);
  data += String("|");
  if (hasInternetConnection) {
    data += String("1");
  } else {
    data += String("0");
  }
  data += String("|");
  if (mikrotikConnectionSuccess) {
    data += String("1");
  } else {
    data += String("0");
  }
  data += String("|");
  data += currentMacAddress;
  data += String("|");
  data += currentIpAddress;
  data += String("|");
  data += HARDWARE_TYPE;
  data += String("|");
  data += CURRENT_VERSION;

  server.send(200, "text/plain", data);
}

bool isAuthorized() {
  String auth = server.header("Authorization");
  String expectedAuth = "Basic " + adminAuth;
  if (auth != expectedAuth) {
    Serial.print("Admin incorrect: ");
    Serial.print(auth);
    Serial.print(" vs ");
    Serial.println(expectedAuth);
  }
  return auth == expectedAuth;
}

void handleNotAuthorize() {
  server.sendHeader("WWW-Authenticate", "Basic realm=\"Secure\"");
  server.send(401, "text/html", "<html>Authentication failed</html>");
}

boolean hasUploadError = false;
boolean isFileSystem = true;

void handleFileUploadRequest() {
  if (Update.hasError()) {
    server.send(200, F("text/html"), "Upload has error");
  } else {
    server.client().setNoDelay(true);
    server.send_P(200, PSTR("text/html"), "Upload done");
    delay(100);
    server.client().stop();
    ESP.restart();
  }
}

 //Taken from https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266HTTPUpdateServer/src/ESP8266HTTPUpdateServer-impl.h
void handleFileUploadStream() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!isAuthorized()) {
      handleNotAuthorize();
      return;
    }
    if (upload.name == "filesystem") {
      isFileSystem = true;
      backupSystemConfig();
      size_t fsSize = ((size_t)&_FS_end - (size_t)&_FS_start);
      close_all_fs();
      if (!Update.begin(fsSize, U_FS)) {  //start with max available size
        Serial.println("Upload filesystem start failed");
        hasUploadError = true;
      }
    } else {
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace, U_FLASH)) {  //start with max available size
        Serial.println("Upload sketch start failed");
        hasUploadError = true;
      }
    }
  } else if (upload.status == UPLOAD_FILE_WRITE && !hasUploadError) {
    Serial.printf(".");
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Serial.println("Upload write failed");
      hasUploadError = true;
    }
  } else if (upload.status == UPLOAD_FILE_END && !hasUploadError) {
    if (Update.end(true)) {  //true to set the size to the current progress
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    } else {
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    hasUploadError = true;
    Serial.println("Upload aborted");
  }
  delay(0);
}

void backupSystemConfig() {
  Serial.println("Starting to backup system.data");
  String data = readFile("/admin/config/system.data");
  int len = data.length();
  eeWriteInt(BACKUP_CONFIG_LENGTH_INDEX, len);
  eeWriteString(BACKUP_CONFIG_LENGTH_INDEX + 5, data);
}

void handleNotFound() {
  Serial.println("preflight....");
  Serial.print("Request: ");
  Serial.print(server.hostHeader());
  Serial.println(server.uri());
  if (server.method() == HTTP_OPTIONS) {
    Serial.println("Preflight request....");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Max-Age", "10000");
    server.sendHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.sendHeader("Access-Control-Allow-Credentials", "false");
    server.send(204);
  } else {
    server.send(404, "text/plain", "");
  }
}


void testInsertBottle() {
  if (!isAuthorized()) {
    handleNotAuthorize();
    return;
  }
  String data = server.arg("bottle");
  if (isBottleDetectionActive) {
    bottle += data.toInt();
    bottlesChange = 1;
  }
  server.send(200, "text/plain", "ok");
}

void handleCancelTopUp() {

  if (!checkIfSystemIsAvailable()) {
    Serial.printf("[%s] System Unavailable.\r\n", __FUNCTION__);
    return;
  }
  String voucher = server.arg("voucher");
  if (!validateVoucher(voucher)) {
    return;
  }
  targetMilis = millis();
  String keys[] = { "status" };
  String values[] = { "true" };
  setupCORSPolicy();
  server.send(200, "application/json", toJson(keys, values, 1));
}
/* End of Webserver-handling Functions */

/*
 * Miscellanous Functions
 * These do not affect primary logic of the code.
 */
 String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

 String toJson(String keys[], String values[], int nField) {
  String json = "{";

  for (int i = 0; i < nField; i++) {
    if (i > 0) {
      json += ",";
    }
    json += " \"";
    json += String(keys[i]);
    json += "\": \"";
    json += String(values[i]);
    json += "\" ";
  }
  json += "}";
  return json;
}
// split() for tokenization of data
int split(String rows[], String data, char delimeter) {
  int count = 0;
  String elementData = "";
  for (int i = 0; i < data.length(); i++) {
    if (data.charAt(i) != delimeter) {
      elementData.concat(data.charAt(i));
    } else {
      rows[count] = elementData;
      elementData = "";
      count++;
    }
  }
  if (elementData != "") {
    rows[count] = elementData;
    count++;
  }
  return count;
} // split() end

/* End of Miscellanous Functions */

/* Start of setup() */
void setup() {

  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println();
  delay(300);           // Wait for 'stabilize' serial
  
  EEPROM.begin(512);
  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  //Setup Hardware
  pinMode(SENSOR_1_PIN, INPUT_PULLUP);
  pinMode(SENSOR_2_PIN, INPUT_PULLUP);
  // pinMode(SENSOR_3_PIN, INPUT_PULLUP);
  pinMode(DEBUGLED_1_PIN, OUTPUT);
  pinMode(DEBUGLED_2_PIN, OUTPUT);
  pinMode(SERVO_1_PIN, OUTPUT);
  digitalWrite(DEBUGLED_1_PIN, HIGH);

  //Servo Control
  servo1.attach(SERVO_1_PIN, 500, 2500, 0);

  //Debugging Only
  MG995Rotate(servo1, 500, MG995_CLOCKWISE);
  MG995Rotate(servo1, 500, MG995_ANTICLOCKWISE);

  //Putting this here is for debugging only. Should be added above "Setup Hardware"
  populateSystemConfiguration();

  // We start by connecting to a WiFi network
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  //for static ip configuration
  if (IP_ADDRESS_MODE == 1) { //Static instead of DHCP
    Serial.print("[Wifi Client] Using static ip address ");
    Serial.println(local_IP);
    WiFi.config(local_IP, primaryDNS, gateway, subnet);
  }

  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.println();
  Serial.println();
  Serial.print("[Wifi Client] Wait for WiFi, connecting to RouterAP: ");
  Serial.print(ssid);
  Serial.println();

  int second = 0;
  
  // Wifi Client - Establish Connection
  if (SETUP_FINISH == 1) {
    while (second <= WIFI_CONNECT_TIMEOUT) {
      networkConnected = (WiFi.status() == WL_CONNECTED);
      Serial.print(".");
      if (networkConnected) {
        break;
      }
      delay(WIFI_CONNECT_DELAY);
      second += WIFI_CONNECT_DELAY;
    }
    currentIpAddress = WiFi.localIP().toString().c_str();
    currentMacAddress = WiFi.macAddress();
  } 
  // End of Wifi Client - Establish Connection
  else {
    Serial.println("Initial setup detected, no need to connect to RouterAP");
    networkConnected = false;
  }

  // Wifi Client - Connected
  if (networkConnected) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(currentIpAddress);
    Serial.print("Mac address: ");
    Serial.println(currentMacAddress);
    Serial.println("Connecting.... ");
    Serial.println("Attaching interrupt ");

    //Attach Interrupt Here
    sensorsInterruptAttach();

    //Access RouterAP via Telnet
    loginMirotik();

    if (MDNS.begin("esp8266")) {
      Serial.println("MDNS responder started");
    }

    // Webserver Setup but only when connected 
    server.on("/topUp", topUp);
    server.on("/checkBottle", checkBottle);
    server.on("/useVoucher", useVoucher);
    server.on("/health", handleHealth);
    server.on("/getRates", handleUserGetRates);
    server.on("/cancelTopUp", handleCancelTopUp);
    server.on("/testInsertBottle", testInsertBottle);
    server.onNotFound(handleNotFound);
  } //End of Wifi Client - Connected
  //Soft AP setup 
  else {
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Sapientia Wifi Initial Setup");

    Serial.printf("Connect to Initial Setup, use browser and type %s\r\n",apIP);
    //if DNSServer is started with "*" for domain name, it will reply with
    //provided IP to all DNS request
    dnsServer.start(DNS_PORT, "*", apIP);

    server.onNotFound([]() {
      server.sendHeader("Location", String("/admin"), true);
      server.send(302, "text/plain", "");
    });
    Serial.println("Started Initial Setup Wifi");
  } //End of Soft AP setup

  // Webserver Setup for both Wifi Mode. (Means always available to user)
  server.on("/admin/api/dashboard", handleAdminDashboard);
  server.on("/admin/js/jquery.min.js", handleJquerySript);
  server.on("/admin/api/resetStatistic", handleAdminResetStats);
  server.on("/admin/api/saveSystemConfig", handleAdminSaveSystemConfig);
  server.on("/admin/api/getSystemConfig", handleAdminGetSystemConfig);
  server.on("/admin/api/getRates", handleAdminGetRates);
  server.on("/admin/api/saveRates", handleAdminSaveRates);
  server.on("/admin/api/logout", handleLogout);
  server.on("/admin/api/generateVouchers", handleGenerateVouchers);
  server.on("/admin", handleAdminPage);
  server.on("/admin/viewGeneratedVouchers", handleAdminGeneratedVoucherPage);
  server.on("/admin/updateMainBin", HTTP_POST, handleFileUploadRequest, handleFileUploadStream);

  // Populate Rates based on rates.data
  populateRates();

  server.begin();

  if (mikrotikConnectionSuccess) {
    Serial.println("RouterAP Connected Successfully!");
    digitalWrite(DEBUGLED_1_PIN, HIGH);
  } 
}
/* End of setup() */

/*
 * loop()
 */
void loop() {

  if (networkConnected) {
    unsigned long currentMilis = millis();

    //handling for disconnection of AP
    bool linkStatusOff = false;
    // checkSTAConnection();    //Debugging Only
    if (!client.connected() || linkStatusOff) {
      handleSystemAbnormal();
      server.handleClient();
      return;
    }

    /* 
     * isReadyToDispense is set to True at enableBottleDetection();
     * This function is called in the following scenarios:
     * 1. checkBottle (from /checkBottle URL)
     * 2. topUp (from /topUp URL)
     */
    if (isReadyToDispense) {

      /* 
        * Initially the dispenser is closed. Make sure that the dispenser is closed actually at startup. 
        * Check if Dispenser is close; This is a software tracker.
        */ 
      if(!isDispenserOpen) openDispenser();

      // targetMilis will have value at enableBottleDetection() and handleCanceltopUp()
      if ((targetMilis > currentMilis)) {
        isDispensingTimeoutExpired = false;
        
        if (isSensorsTriggered == 1) {
          // APP_DEBUG_PRINT("SensorsTriggered");
          delay(DELAY_BEFORE_READING_SENSORS_ASSERT_VAL);

          // APP_DEBUG_PRINT("Sensor values 1:%d 2:%d 3: %d\r\n",isSensor1Activated, isSensor2Activated, isSensor3Activated);
          isSensorsTriggered = 0;

          if(isSensor2Activated && isSensor3Activated) { // && isSensor1Activated) but sensor 1 is not working
            APP_DEBUG_PRINT("All Sensors Activated");
            
            //Bottle was detected
            bottle += 1;
            bottlesChange = 1;
            //'Clear' Flags (Clearing depends on the assertion val) 
            sensorsClearISRFlags();

            // Bottle was inserted and here.
            if (bottlesChange > 0) {
              //Not necessary or maybe need to implement differently.
              processBottle = bottle;  
              bottle -= processBottle;

              Serial.print("Bottle inserted: ");
              Serial.println(processBottle);
              bottlesChange = 0;
              isReadyToDispense = false;
            }
          }
        }
      }
      /* 
       * Will surely go here on the next iteration after doing above
       */ 
      else {
        APP_DEBUG_PRINT("Dispense Timeout Expired");

        isDispensingTimeoutExpired = true;
        disableBottleDetection();
        isReadyToDispense = false;

        //Calculate bottles to time and return time.
        timeToAdd = calculateAddTime();

        //Auto add time no need to use voucher
        if (timeToAdd > 0) {
          clearAttemptToInsertBottle(); 
          Serial.print("Bottle insert waiting expired, Auto using the voucher ");
          Serial.print(currentActiveVoucher);
          if (isNewVoucher) {
            registerNewVoucher(currentActiveVoucher);
          }
          updateStatisticToEE();
          addTimeToVoucher(currentActiveVoucher, timeToAdd);
        } else {
          addAttemptToInsertBottle();
        }
        resetGlobalVariables();
      }
    }
    //Clear flags (If not ready to dispense)
    isSensorsTriggered = 0;
    isSensor1Activated = !SENSOR_1_ASSERT_VAL;
    isSensor2Activated = !SENSOR_2_ASSERT_VAL;
    isSensor3Activated = !SENSOR_3_ASSERT_VAL; 
  } else {
    unsigned long currentMilis = millis();

    if (SETUP_FINISH == 1) {
      //when setup is already finish and cannnot connect, wait for 10 mins to setup and will auto restart after that
      //this is to cater slow boot AP
      Serial.println("Network Not Connected Restarting ESP!");
      if (currentMilis >= 600000) {
        ESP.restart();
      }
    }
    dnsServer.processNextRequest();
  }
  
  server.handleClient();
  MDNS.update();
}
/* End of loop() */