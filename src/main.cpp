#include <Arduino.h>
#include <esp_log.h>
#include <sstream>
#include <queue>
#include <string>
#include "eQ3.h"
#include "WiFi.h"
#include "PubSubClient.h"
#include <esp_wifi.h>
#include <WiFiClient.h>
#include <BLEDevice.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include "AutoConnect.h"
#include "BLEUtils.h"
#include "BLEScan.h"
#include "BLEAdvertisedDevice.h"
#include "sdkconfig.h"

#define PARAM_FILE      "/keyble.json"
#define AUX_SETTING_URI "/keyble_setting"
#define AUX_SAVE_URI    "/keyble_save"
//#define AUX_CLEAR_URI   "/mqtt_clear"

//AP Mode Settings
#define AP_IP     192,168,4,1
#define AP_ID     "KeyBLEBridge"
#define AP_PSK    "eqivalock"
#define AP_TITLE  "KeyBLEBridge"

#define MQTT_SUB_COMMAND "/KeyBLE/set"
#define MQTT_SUB_STATE "/KeyBLE/get"
#define MQTT_PUB_STATE "/KeyBLE/"
#define MQTT_PUB_LOCK_STATE "/KeyBLE/lock_state"
#define MQTT_PUB_TASK "/KeyBLE/task"
#define MQTT_PUB_BATT "/KeyBLE/battery"
#define MQTT_PUB_RSSI "/KeyBLE/linkquality"

#define CARD_KEY "M001AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

// ---[Variables]---------------------------------------------------------------
#pragma region
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig config;
fs::SPIFFSFS& FlashFS = SPIFFS;

eQ3* keyble = NULL;

bool do_open = false;
bool do_lock = false;
bool do_unlock = false;
bool do_status = false;
bool do_toggle = false;
bool do_pair = false;
bool wifiActive = false;
bool cmdTriggered=false;
unsigned long timeout=0;
bool statusUpdated=false;
bool waitForAnswer=false;
bool KeyBLEConfigured = false;
unsigned long starttime=0;
int status = 0;
bool batteryLow = false;
int rssi = 0;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;

const int PushButton = 0;

String  MqttServerName;
String  MqttPort;
String  MqttUserName;
String  MqttUserPass;
String  MqttTopic;

String KeyBleMac;
String KeyBleUserKey;
String KeyBleUserId;
String KeyBleRefreshInterval;

String mqtt_sub_command_value  = "";
String mqtt_sub_state_value = "";
String mqtt_pub_state_value  = "";
String mqtt_pub_lock_state_value = "";
String mqtt_pub_task_value  = "";
String mqtt_pub_battery_value = "";
String mqtt_pub_rssi_value = "";

bool bleDirtyConfig = false;
bool mqttDirtyConfig = false;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
#pragma endregion
// ---[Add Menue Items]---------------------------------------------------------
#pragma region
static const char AUX_keyble_setting[] PROGMEM = R"raw(
[{
		"title": "KeyBLE Settings",
		"uri": "/keyble_setting",
		"menu": true,
		"element": [{
				"name": "style",
				"type": "ACStyle",
				"value": "label+input,label+select{position:right;left:120px;width:250px!important;box-sizing:border-box;}"
			},
			{
				"name": "MqttServerName",
				"type": "ACInput",
				"value": "",
				"label": "MQTT Broker IP",
				"pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$",
				"placeholder": "MQTT broker IP"
			},
			{
				"name": "MqttPort",
				"type": "ACInput",
				"label": "MQTT Broker Port",
				"placeholder": "1883",
				"pattern": "^([0-9]+)$"
			},
			{
				"name": "MqttUserName",
				"type": "ACInput",
				"label": "MQTT User Name",
				"pattern": "^(.*\\S.*)$"
			},
			{
				"name": "MqttUserPass",
				"type": "ACInput",
				"label": "MQTT User Password",
				"apply": "password",
				"pattern": "^(.*\\S.*)$"
			},
			{
				"name": "MqttTopic",
				"type": "ACInput",
				"value": "",
				"label": "MQTT Topic",
				"pattern": "^(.*\\S.*)$"
			},
			{
				"name": "KeyBleMac",
				"type": "ACInput",
				"label": "KeyBLE MAC",
				"pattern": "^([0-9A-F]{2}[:]){5}([0-9A-F]{2})$"
			},
			{
				"name": "KeyBleUserKey",
				"type": "ACInput",
				"label": "KeyBLE User Key",
				"pattern": "^(.*\\S.*)$"
			},
			{
				"name": "KeyBleUserId",
				"type": "ACInput",
				"label": "KeyBLE User ID",
				"pattern": "^(.*\\S.*)$"
			},
			{
				"name": "KeyBleRefreshInterval",
				"type": "ACInput",
				"label": "KeyBLE refresh interval, minimum is 20000 milliseconds",
				"pattern": "^([2-9])([0-9]){4}([0-9]*)$",
				"placeholder": "20000"
			},
			{
				"name": "save",
				"type": "ACSubmit",
				"value": "Save&amp;Start",
				"uri": "/keyble_save"
			},
			{
				"name": "discard",
				"type": "ACSubmit",
				"value": "Discard",
				"uri": "/_ac"
			}
		]
	},
	{
		"title": "MQTT Settings",
		"uri": "/keyble_save",
		"menu": false,
		"element": [{
				"name": "caption",
				"type": "ACText",
				"value": "<h4>Parameters saved as:</h4>",
				"style": "text-align:center;color:#2f4f4f;padding:10px;"
			},
			{
				"name": "parameters",
				"type": "ACText"
			},
			{
				"name": "clear",
				"type": "ACSubmit",
				"value": "OK",
				"uri": "/keyble_setting"
			}
		]
	}
]
)raw";

#pragma endregion

// ---[SetWifi]-----------------------------------------------------------------
void SetWifi(bool active) {
  wifiActive = active;
  if (active) {
    WiFi.mode(WIFI_STA);
    Serial.println("# WiFi enabled");
  }
  else {
    WiFi.mode(WIFI_OFF);
    Serial.println("# WiFi disabled");
  }
}
// ---[blescan]-----------------------------------------------------------------
bool blescan(String macAddress) {
  bool found = false;
	Serial.println("# Scanning starting");
	BLEScan* pBLEScan = BLEDevice::getScan();
	pBLEScan->setActiveScan(true);
	BLEScanResults scanResults = pBLEScan->start(5);
	Serial.printf("# Found %d devices\n", scanResults.getCount());
  String strNames[scanResults.getCount()];
  for ( int i=0; i<scanResults.getCount(); i++ )
  {
    strNames[i] = scanResults.getDevice(i).getAddress().toString().c_str();
    if (strNames[i] == macAddress)
    {
      found = true;
      Serial.println("# MAC address found!");
    }
  }
	scanResults.dump();
	Serial.println("# Scanning ended");
  return found;
}
// ---[getParams]---------------------------------------------------------------
void getParams(AutoConnectAux& aux) {

  MqttServerName = aux["MqttServerName"].value;
  MqttServerName.trim();
  MqttPort = aux["MqttPort"].value;
  MqttPort.trim();
  MqttUserName = aux["MqttUserName"].value;
  MqttUserName.trim();
  MqttUserPass = aux["MqttUserPass"].value;
  MqttUserPass.trim();
  MqttTopic = aux["MqttTopic"].value;
  MqttTopic.trim();
  KeyBleMac = aux["KeyBleMac"].value;
  KeyBleMac.trim();
  KeyBleUserKey = aux["KeyBleUserKey"].value;
  KeyBleUserKey.trim();
  KeyBleUserId = aux["KeyBleUserId"].value;
  KeyBleUserId.trim();
  KeyBleRefreshInterval = aux["KeyBleRefreshInterval"].value;
  KeyBleRefreshInterval.trim();

 }
// ---[loadParams]--------------------------------------------------------------
String loadParams(AutoConnectAux& aux, PageArgument& args) {
  (void)(args);
  File param = FlashFS.open(PARAM_FILE, "r");
  if (param) {
    if (aux.loadElement(param)) {
      getParams(aux);
      Serial.println("# " PARAM_FILE " loaded");
      KeyBLEConfigured = true;
    }
    else
    {
      Serial.println("# " PARAM_FILE " failed to load");
      KeyBLEConfigured = false;
    }
    param.close();
  }
  else
    Serial.println("# " PARAM_FILE " open failed");
  return String("");
}
// ---[saveParams]--------------------------------------------------------------
String saveParams(AutoConnectAux& aux, PageArgument& args) {
  AutoConnectAux&   keyble_setting = *Portal.aux(Portal.where());

  if (keyble_setting.isValid())
  {
    File param = FlashFS.open(PARAM_FILE, "w");
    keyble_setting.saveElement(param, { "MqttServerName", "MqttPort", "MqttUserName", "MqttUserPass", "MqttTopic", "KeyBleMac", "KeyBleUserKey", "KeyBleUserId", "KeyBleRefreshInterval" });
    param.close();
    getParams(keyble_setting);
    KeyBLEConfigured = true;
  }
  
  AutoConnectInput& mqttserver_input = keyble_setting["MqttServerName"].as<AutoConnectInput>();
  AutoConnectInput& MqttPort_input = keyble_setting["MqttPort"].as<AutoConnectInput>();
  AutoConnectInput& MqttUserName_input = keyble_setting["MqttUserName"].as<AutoConnectInput>();
  AutoConnectInput& MqttUserPass_input = keyble_setting["MqttUserPass"].as<AutoConnectInput>();
  AutoConnectInput& MqttTopic_input = keyble_setting["MqttTopic"].as<AutoConnectInput>();
  AutoConnectInput& KeyBleMac_input = keyble_setting["KeyBleMac"].as<AutoConnectInput>();
  AutoConnectInput& KeyBleUserKey_input = keyble_setting["KeyBleUserKey"].as<AutoConnectInput>();
  AutoConnectInput& KeyBleUserId_input = keyble_setting["KeyBleUserId"].as<AutoConnectInput>();
  AutoConnectInput& refresh_input = keyble_setting["KeyBleRefreshInterval"].as<AutoConnectInput>();

  AutoConnectText&  echo = aux["parameters"].as<AutoConnectText>();
  echo.value = keyble_setting.isValid() ? "Saved!" : "Please correct form errors!";
  echo.value += "<br><br>Server: " + mqttserver_input.value;
  echo.value += mqttserver_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>MQTT Port: " + MqttPort_input.value;
  echo.value += MqttPort_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>MQTT User Name: " + MqttUserName_input.value;
  echo.value += MqttUserName_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>MQTT User Pass: <hidden>";
  echo.value += MqttUserPass_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>MQTT Topic: " + MqttTopic_input.value;
  echo.value += MqttTopic_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>KeyBLE MAC: " + KeyBleMac_input.value;
  echo.value += KeyBleMac_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>KeyBLE User Key: " + KeyBleUserKey_input.value;
  echo.value += KeyBleUserKey_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>KeyBLE User ID: " + KeyBleUserId_input.value;
  echo.value += KeyBleUserId_input.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>KeyBLE Refresh Interval: " + refresh_input.value;
  echo.value += refresh_input.isValid() ? String(" (OK)") : String(" (ERR)");

  if (!mqttserver_input.isValid() ||
      !MqttPort_input.isValid() ||
      !MqttUserName_input.isValid() ||
      !MqttUserPass_input.isValid() ||
      !MqttTopic_input.isValid()
      ) mqttDirtyConfig = true; else mqttDirtyConfig = false;

  if (!KeyBleMac_input.isValid() || 
      !KeyBleUserKey_input.isValid() ||
      !KeyBleUserId_input.isValid() ||
      !refresh_input.isValid()
      ) bleDirtyConfig = true; else bleDirtyConfig = false;

  return String("");
}
// ---[MqttCallback]------------------------------------------------------------
void MqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicString = String(topic);
  String payloadString = String((char*)payload).substring(0, length);
  // Serial.println("# topic: " + topicString + ", payload: " + payloadString);

  if(topicString.endsWith(MQTT_SUB_COMMAND) == 1)
  {
    Serial.println("# Command received");
    /*
    //pair
    if (payloadString == "PAIR")
    {
      do_pair = true;
      mqtt_sub_command_value = "*** pair ***";
      Serial.println(mqtt_sub_command_value");
      
    }
    */
    //toggle
    if (payloadString == "TOGGLE")
    {
      do_toggle = true;
      mqtt_sub_command_value = "*** toggle ***";
      Serial.println(mqtt_sub_command_value);
    }
    //open
    if (payloadString == "OPEN")
    {
      do_open = true;
      mqtt_sub_command_value = "*** open ***";
      Serial.println(mqtt_sub_command_value);
    }
    //lock  
    if (payloadString == "LOCK")
    {
      do_lock = true;
      mqtt_sub_command_value = "*** lock ***";
      Serial.println(mqtt_sub_command_value);
    }
    //unlock
    if (payloadString == "UNLOCK")
    { 
      do_unlock = true;
      mqtt_sub_command_value = "*** unlock ***";
      Serial.println(mqtt_sub_command_value);
    }
  }
  else if(topicString.endsWith(MQTT_SUB_STATE) == 1)
  {
    Serial.println("# Status request received");
    if (payloadString == "")
    {
      do_status = true;
      mqtt_sub_state_value = "*** status ***";
      Serial.println(mqtt_sub_state_value);
    }
  }
}
// ---[MQTTpublish]-------------------------------------------------------------
void MqttPublish()
{
  if (keyble == NULL) return;
  statusUpdated = false;
  
  //MQTT_PUB_STATE status
  batteryLow = (keyble->raw_data[1] == 0x81);
  status = keyble->_LockStatus;
  String str_status="";
  char charBufferStatus[9];

  if(status == 2 || status == 4)
  str_status = "UNLOCKED";
  else if(status == 3)
  str_status = "LOCKED";
  // else if(status == 9)
  // str_status = "offline";
  else
  str_status = "";
  
  String strBuffer =  String(str_status);
  strBuffer.toCharArray(charBufferStatus, 9);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_STATE).c_str()), charBufferStatus);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_STATE).c_str()));
  Serial.print("/");
  Serial.println(charBufferStatus);
  mqtt_pub_state_value = charBufferStatus;

  delay(100);

  //MQTT_PUB_LOCK_STATE lock status
  str_status="";
  char charBufferLockStatus[11];

  if(status == 1)
  str_status = "moving";
  else if(status == 2)
  str_status = "unlocked";
  else if(status == 3)
  str_status = "locked";
  else if(status == 4)
  str_status = "opened";
  else if(status == 9)
  str_status = "unavailable";
  else
  str_status = "unknown";

  String strBufferLockStatus = String(str_status);
  strBufferLockStatus.toCharArray(charBufferLockStatus, 11);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_LOCK_STATE).c_str()), charBufferLockStatus);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_LOCK_STATE).c_str()));
  Serial.print("/");
  Serial.println(charBufferLockStatus);
  mqtt_pub_lock_state_value = charBufferLockStatus;
  
  delay(100);

  //MQTT_PUB_TASK task
  String str_task="waiting";
  char charBufferTask[8];
  str_task.toCharArray(charBufferTask, 8);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_TASK).c_str()), charBufferTask);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_TASK).c_str()));
  Serial.print("/");
  Serial.println(charBufferTask);
  mqtt_pub_task_value = charBufferTask;

  //MQTT_PUB_BATT battery
  String str_batt = "ok";
  char charBufferBatt[4];

  if(batteryLow)
  {
    str_batt = "low";
  }
  
  str_batt.toCharArray(charBufferBatt, 4);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_BATT).c_str()), charBufferBatt);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_BATT).c_str()));
  Serial.print("/");
  Serial.println("0");
  mqtt_pub_battery_value = charBufferBatt;

  //MQTT_PUB_RSSI rssi
  rssi = keyble->_RSSI;
  char charBufferRssi[4];
  String strRSSI =  String(rssi);
  
  strRSSI.toCharArray(charBufferRssi, 4);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_RSSI).c_str()), charBufferRssi);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_RSSI).c_str()));
  Serial.print("/");
  Serial.println(charBufferRssi);
  mqtt_pub_rssi_value = charBufferRssi;
         
  Serial.println("# waiting for command...");
}
// ---[MQTT-Setup]--------------------------------------------------------------
void SetupMqtt() {
  if (!mqttDirtyConfig)
  {
    Serial.println("# Setting up MQTT");
    while (!mqttClient.connected()) 
    { // Loop until we're reconnected to the MQTT server
      mqttClient.setServer(MqttServerName.c_str(), MqttPort.toInt());
      mqttClient.setCallback(&MqttCallback);
      Serial.println("# Connect to MQTT-Broker... ");
      if (mqttClient.connect(MqttTopic.c_str(), MqttUserName.c_str(), MqttUserPass.c_str()))
      {
        Serial.println("# Connected!");
        mqttClient.subscribe((String(MqttTopic + MQTT_SUB_COMMAND).c_str()));
        Serial.print("# Subscribed to topic: ");
        Serial.println((String(MqttTopic + MQTT_SUB_COMMAND).c_str()));
        mqttClient.subscribe((String(MqttTopic + MQTT_SUB_STATE).c_str()));
        Serial.print("# Subscribed to topic: ");
        Serial.println((String(MqttTopic + MQTT_SUB_STATE).c_str()));
        Serial.println("# MQTT Setup done");
      }
      else
      {
        Serial.print("# MQTT error. Please check configuration, rc=");
        Serial.println(mqttClient.state());
        mqttDirtyConfig = true;
        break;
      }
    }
  }
}
// ---[Bluetooth-Setup]--------------------------------------------------------------
void SetupBluetooth() {
  if (!bleDirtyConfig)
  {
    SetWifi(false);
    Serial.println("# Setting up bluetooth");
    BLEDevice::init("");
    Serial.println("# Checking if KeyBle lock is in range");
    bool keyBleFound = blescan(KeyBleMac);
    if (keyBleFound)
    {
      keyble = new eQ3(KeyBleMac.c_str(), KeyBleUserKey.c_str(), KeyBleUserId.toInt());
      Serial.println("# Bluetooth Setup done");
    }
    else
    {
      Serial.println("# KeyBle lock device not found! Please check settings");
      bleDirtyConfig = true;
    }
    SetWifi(true);
  }
}
// ---[RootPage]----------------------------------------------------------------
void rootPage()
{
  String  content =
   "<html>"
    "<head>"
     "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  if (mqtt_sub_command_value != "\0")
  {
    content +=
    "<meta http-equiv=\"refresh\" content=\"30\"/>";
  }
  content +=
   "</head>"
   "<body>"
   "<h2 align=\"center\" style=\"color:blue;margin:20px;\">KeyBLEBridge Config</h2>"
   "<p></p><p style=\"padding-top:15px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>";

  if (!WiFi.localIP())
  {
    content +=
    "<h2 align=\"center\" style=\"color:blue;margin:20px;\">Please click on gear to setup WiFi.</h2>";
  }
  else
  {
    content +=
    "<h2 align=\"center\" style=\"color:green;margin:20px;\">Connected to: " + WiFi.SSID() + "</h2>"
    "<h2 align=\"center\" style=\"color:green;margin:20px;\">Bride local IP: " + WiFi.localIP().toString() + "</h2>"
    "<h2 align=\"center\" style=\"color:green;margin:20px;\">Gateway IP: " + WiFi.gatewayIP().toString() + "</h2>"
    "<h2 align=\"center\" style=\"color:green;margin:20px;\">Netmask: " + WiFi.subnetMask().toString()  + "</h2>";
  }
  if (!KeyBLEConfigured && WiFi.localIP())
  {
    content +=
    "<h2 align=\"center\" style=\"color:red;margin:20px;\">Enter MQTT and KeyBLE credentials.</h2>"
    "<div style=\"text-align:center;\"><a href=\"/keyble_setting\">Click here to configure MQTT and KeyBLE</a></div>";
  }
  if (KeyBLEConfigured && WiFi.localIP())
  {
    content +=
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT Server: " + MqttServerName + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT Port: " + MqttPort + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT User Name: " + MqttUserName + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT Topic: " + MqttTopic + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE MAC Address: " + KeyBleMac + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE User Key: " + KeyBleUserKey + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE User ID: " + KeyBleUserId + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE refresh interval: " + KeyBleRefreshInterval + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last battery state: " + mqtt_pub_battery_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last command received: " + mqtt_sub_command_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last rssi: " + mqtt_pub_rssi_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last state: " + mqtt_pub_state_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last lock state: " + mqtt_pub_lock_state_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last task: " + mqtt_pub_task_value + "</h2>"
     "<br>"
     "<h2 align=\"center\" style=\"color:blue;margin:20px;\">page refresh every 30 seconds</h2>";
  }

  content +=
    "</body>"
    "</html>";
  Server.send(200, "text/html", content);
}
// ---[Wifi Signalquality]-----------------------------------------------------
int GetWifiSignalQuality() {
float signal = 2 * (WiFi.RSSI() + 100);
if (signal > 100)
  return 100;
else
  return signal;
}
// ---[SetupWiFi]---------------------------------------------------------------
void SetupWifi()
{
  if (Portal.begin())
  {
   if (WiFi.status() == WL_CONNECTED)
   {
     Serial.println("# WIFI: connected to SSiD: " + WiFi.SSID());
   } 
   int maxWait=100;
   while (WiFi.status() != WL_CONNECTED) {
    Serial.println("# WIFI: checking SSiD: " + WiFi.SSID());
    delay(500);
    
    if (maxWait <= 0)
        ESP.restart();
      maxWait--;
  }
  Serial.println("# WIFI: connected!");
  Serial.println("# WIFI: signalquality: " + String(GetWifiSignalQuality()) + "%");
  Serial.println("# WiFi connected to IP: " + WiFi.localIP().toString());
  }
}
// ---[Setup]-------------------------------------------------------------------
void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println("---Starting up...---");
  Serial.setDebugOutput(true);
  FlashFS.begin(true);
  Serial.println("---AP Settings---");
  config.apip = IPAddress(AP_IP);
  Serial.print("---AP IP: ");
  Serial.print(config.apip);
  Serial.println("---");
  config.apid = AP_ID;
  Serial.print("---AP SSID: ");
  Serial.print(config.apid);
  Serial.println("---");
  config.psk = AP_PSK;
  config.title = AP_TITLE;
  config.gateway = IPAddress(AP_IP);
  config.ota = AC_OTA_BUILTIN;
  Server.on("/", rootPage);
  Portal.config(config);
  
  if (Portal.load(FPSTR(AUX_keyble_setting))) {
     AutoConnectAux& keyble_setting = *Portal.aux(AUX_SETTING_URI);
     PageArgument  args;
     loadParams(keyble_setting, args);
     config.homeUri = "/_ac";

     Portal.on(AUX_SETTING_URI, loadParams);
     Portal.on(AUX_SAVE_URI, saveParams);
  }
  else
  {
    Serial.println("load error");
  }

  SetupWifi();

  if(KeyBLEConfigured)
  {
    //MQTT
    SetupMqtt();
    //Bluetooth
    SetupBluetooth();
  }
}
// ---[loop]--------------------------------------------------------------------
void loop() {

Portal.handleClient();  

// This statement will declare pin 0 as digital input 
pinMode(PushButton, INPUT);
// digitalRead function stores the Push button state 
// in variable push_button_state
int Push_button_state = digitalRead(PushButton);
// if condition checks if push button is pressed
// if pressed Lock will toggle state
if (Push_button_state == LOW && WiFi.status() == WL_CONNECTED)
{ 
  Serial.println("# Button pushed... toggling lock!");
  do_toggle = true;
}

// Wifi reconnect
if (wifiActive)
{
  if (WiFi.status() != WL_CONNECTED)
  {
   Serial.println("# WiFi disconnected, reconnect...");
   SetupWifi();
  }
  else
  {
   // MQTT connected?
   if(!mqttClient.connected())
   {
     if (WiFi.status() == WL_CONNECTED) 
     {
      if(KeyBLEConfigured)
      {
        SetupMqtt();
        if (statusUpdated)
        {
          MqttPublish();
        }
      }
    }
   }
   else if(mqttClient.connected())
   {
     mqttClient.loop();
   }
  }
}

// Bluetooth reconnect
if (keyble == NULL)
{
  SetupBluetooth();
}

if ((do_open || do_lock || do_unlock || do_status || do_toggle || do_pair) 
    && keyble != NULL) 
{
  String str_task="working";
  char charBufferTask[8];
  str_task.toCharArray(charBufferTask, 8);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_TASK).c_str()), charBufferTask);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_TASK).c_str()));
  Serial.print("/");
  Serial.println(charBufferTask);
  mqtt_pub_task_value = charBufferTask;
  delay(200);
  SetWifi(false);
  yield();
  waitForAnswer=true;
  keyble->_LockStatus = -1;
  starttime = millis();

  if (do_open)
  {
    Serial.println("*** open ***");
    keyble->open();
    do_open = false;
  }

  if (do_lock)
  {
    Serial.println("*** lock ***");
    keyble->lock();
    do_lock = false;
  }

  if (do_unlock)
  {
    Serial.println("*** unlock ***");
    keyble->unlock();
    do_unlock = false;
  }
  
  if (do_status)
  {
    Serial.println("*** get state ***");
    keyble->updateInfo();
    do_status = false;
  }
  
  if (do_toggle)
  {
    Serial.println("*** toggle ***");
    if ((status == 2) || (status == 4))
    {
      keyble->lock();
      do_lock = false;
    }
    if (status == 3)
    {
      keyble->unlock();
      do_unlock = false;
    }
    do_toggle = false;
   }
   
   if (do_pair)
   {
     Serial.println("*** pair ***");
     //Parse key card data
     std::string cardKey = CARD_KEY;
     if(cardKey.length() == 56)
     {
      Serial.println(cardKey.c_str());
      std::string pairMac = cardKey.substr(1,12);
      
      pairMac = pairMac.substr(0,2)
                + ":" +pairMac.substr(2,2)
                + ":" +pairMac.substr(4,2)
                + ":" +pairMac.substr(6,2)
                + ":" +pairMac.substr(8,2)
                + ":" +pairMac.substr(10,2);
      Serial.println(pairMac.c_str());
      std::string pairKey = cardKey.substr(14,32);
      Serial.println(pairKey.c_str());
      std::string pairSerial = cardKey.substr(46,10);
      Serial.println(pairSerial.c_str());
     }
     else
     {
      Serial.println("# invalid CardKey! Pattern example:");
      Serial.println("  M followed by KeyBLE MAC length 12");
      Serial.println("  K followed by KeyBLE CardKey length 32");
      Serial.println("  Serialnumber");
      Serial.println("  MxxxxxxxxxxxxKxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxSSSSSSSSSS");
     }
     do_pair = false;
   }
  }
  
  if(waitForAnswer)
  {
    bool timeout=(millis() - starttime > LOCK_TIMEOUT *2000 +1000);
    bool finished=false;

    if ((keyble->_LockStatus != -1) || timeout)
    {
      if(keyble->_LockStatus == 1)
      {
        //Serial.println("Lockstatus 1");
        if(timeout)
        {
          finished=true;
          Serial.println("!!! Lockstatus 1 - timeout !!!");
        }
      }
      else if(keyble->_LockStatus == -1)
      {
        //Serial.println("Lockstatus -1");
        if(timeout)
        {
          keyble->_LockStatus = 9; //timeout
          finished=true;
          Serial.println("!!! Lockstatus -1 - timeout !!!");
        }
      }
      else if(keyble->_LockStatus != 1)
      {
        finished=true;
        //Serial.println("Lockstatus != 1");
      }

      if(finished)
      {
        Serial.println("# Done!");
        do
        {
          keyble->bleClient->disconnect();
          delay(100);
        }
        while(keyble->state.connectionState != DISCONNECTED && !timeout);

        delay(100);
        yield();
          
        SetWifi(true);
        
        statusUpdated=true;
        waitForAnswer=false;
      }
    }
  }

  currentMillis = millis();
  if (currentMillis - previousMillis > KeyBleRefreshInterval.toInt())
  {
    do_status = true;
    previousMillis = currentMillis;
  }
}
