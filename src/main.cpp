#include <Arduino.h>
#include <esp_log.h>
#include <sstream>
#include <queue>
#include <string>
#include "eQ3.h"
#include "eQ3_constants.h"
#include "WiFi.h"
#include "PubSubClient.h"
#include <esp_wifi.h>
#include <WiFiClient.h>
#include <BLEDevice.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include "AutoConnect.h"

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
#define MQTT_PUB_STATE "/KeyBLE"
#define MQTT_PUB_LOCK_STATE "/KeyBLE/lock_state"
#define MQTT_PUB_AVAILABILITY "/KeyBLE/availability"
#define MQTT_PUB_BATT "/KeyBLE/battery"
#define MQTT_PUB_RSSI "/KeyBLE/linkquality"

#define CARD_KEY "M001AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

// ---[Variables]---------------------------------------------------------------
#pragma region
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig config;
fs::SPIFFSFS& FlashFS = SPIFFS;

eQ3* keyble;

unsigned long operationStartTime = 0;
bool do_open = false;
bool do_lock = false;
bool do_unlock = false;
bool do_status = false;
bool do_toggle = false;
bool do_pair = false;
bool wifiActive = false;
bool cmdTriggered = false;
unsigned long timeout = 0;
bool statusUpdated = false;
bool waitForAnswer = false;
bool KeyBLEConfigured = false;
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

String HomeAssistantMqttPrefix;

String mqtt_sub_command_value  = "";
String mqtt_sub_state_value = "";
String mqtt_pub_state_value  = "";
String mqtt_pub_lock_state_value = "";
String mqtt_pub_availability_value  = "";
String mqtt_pub_battery_value = "";
String mqtt_pub_rssi_value = "";

bool bleDirtyConfig = false;
bool mqttDirtyConfig = false;
bool configurationChanged = false;

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
				"label": "KeyBLE refresh interval, minimum is 20000 milliseconds, 0 to disable",
				"pattern": "^([0])|([2-9])([0-9]){4}([0-9]*)$",
				"placeholder": "20000"
			},
      {
				"name": "HomeAssistantMqttPrefix",
				"type": "ACInput",
				"label": "(Optional) Home Assistant MQTT topic prefix. Fill this if you want the lock to be autodiscovered by Home Assistant",
				"placeholder": "homeassistant"
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
  HomeAssistantMqttPrefix = aux["HomeAssistantMqttPrefix"].value;
  HomeAssistantMqttPrefix.trim();

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
    keyble_setting.saveElement(param, { "MqttServerName", "MqttPort", "MqttUserName", "MqttUserPass", "MqttTopic", "KeyBleMac", "KeyBleUserKey", "KeyBleUserId", "KeyBleRefreshInterval", "HomeAssistantMqttPrefix" });
    param.close();
    getParams(keyble_setting);
    KeyBLEConfigured = true;
    configurationChanged = true;
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
  if (mqttDirtyConfig || bleDirtyConfig) return;
  statusUpdated = false;
  
  //MQTT_PUB_STATE status
  batteryLow = (keyble->raw_data[1] == 0x81);
  status = keyble->_LockStatus;
  String str_status = "unknown";
  char charBufferStatus[10];

  if(status == LockStatus::UNLOCKED || status == LockStatus::OPENED)
  str_status = "UNLOCKED";
  else if(status == LockStatus::LOCKED)
  str_status = "LOCKED";
  
  String strBuffer =  String(str_status);
  strBuffer.toCharArray(charBufferStatus, 10);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_STATE).c_str()), charBufferStatus, true);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_STATE).c_str()));
  Serial.print("/");
  Serial.println(charBufferStatus);
  mqtt_pub_state_value = charBufferStatus;

  delay(100);

  //MQTT_PUB_LOCK_STATE lock status
  String str_lock_status = "";
  char charBufferLockStatus[9];

  if(status == LockStatus::MOVING)
  str_lock_status = "moving";
  else if(status == LockStatus::UNLOCKED)
  str_lock_status = "unlocked";
  else if(status == LockStatus::LOCKED)
  str_lock_status = "locked";
  else if(status == LockStatus::OPENED)
  str_lock_status = "opened";
  else if(status == LockStatus::UNKNOWN)
  str_lock_status = "unknown";

  String strBufferLockStatus = String(str_status);
  strBufferLockStatus.toCharArray(charBufferLockStatus, 9);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_LOCK_STATE).c_str()), charBufferLockStatus, true);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_LOCK_STATE).c_str()));
  Serial.print("/");
  Serial.println(charBufferLockStatus);
  mqtt_pub_lock_state_value = charBufferLockStatus;
  
  delay(100);

  //MQTT_PUB_AVAILABILITY availability
  String str_availability = (status > LockStatus::UNKNOWN) ? "online" : "offline";
  char charBufferAvailability[8];
  str_availability.toCharArray(charBufferAvailability, 8);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_AVAILABILITY).c_str()), charBufferAvailability, true);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_AVAILABILITY).c_str()));
  Serial.print("/");
  Serial.println(charBufferAvailability);
  mqtt_pub_availability_value = charBufferAvailability;

  //MQTT_PUB_BATT battery
  String str_batt = batteryLow ? "true" : "false";
  char charBufferBatt[6];
  str_batt.toCharArray(charBufferBatt, 6);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_BATT).c_str()), charBufferBatt, true);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_BATT).c_str()));
  Serial.print("/");
  Serial.println(charBufferBatt);
  mqtt_pub_battery_value = charBufferBatt;

  //MQTT_PUB_RSSI rssi
  rssi = keyble->_RSSI;
  char charBufferRssi[4];
  String strRSSI =  String(rssi);
  
  strRSSI.toCharArray(charBufferRssi, 4);
  mqttClient.publish((String(MqttTopic + MQTT_PUB_RSSI).c_str()), charBufferRssi, true);
  Serial.print("# published ");
  Serial.print((String(MqttTopic + MQTT_PUB_RSSI).c_str()));
  Serial.print("/");
  Serial.println(charBufferRssi);
  mqtt_pub_rssi_value = charBufferRssi;
         
  Serial.println("# waiting for command...");
}
// ---[HomeAssistant-Setup]--------------------------------------------------------------
void SetupHomeAssistant() {
  // Skip if dirty config
  if (mqttDirtyConfig || HomeAssistantMqttPrefix.isEmpty()) return;

  // Temporarily increase buffer size to send bigger payloads
  mqttClient.setBufferSize(500);

  Serial.println("# Setting up Home Assistant autodiscovery");

  String lock_conf = "{\"~\":\"" + MqttTopic + "\"," +
  + "\"name\":\"Eqiva Bluetooth Smart Lock\"," +
  + "\"device\":{\"identifiers\":[\"keyble_" + KeyBleMac + "\"]," +
  + "\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\",\"name\":\"Eqiva Bluetooth Smart Lock\" }," +
  + "\"uniq_id\":\"keyble_" + KeyBleMac + "\"," +
  + "\"stat_t\":\"~" + MQTT_PUB_STATE + "\"," +
  + "\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
  + "\"opt\":false," + // optimistic false, wait for actual state update
  + "\"cmd_t\":\"~" + MQTT_SUB_COMMAND + "\"}";

  Serial.println("# " + HomeAssistantMqttPrefix + "/lock/KeyBLE/config");
  mqttClient.publish((HomeAssistantMqttPrefix + "/lock/KeyBLE/config").c_str(), lock_conf.c_str(), true);

  String link_quality_conf = "{\"~\":\"" + MqttTopic + "\"," +
  + "\"name\":\"Eqiva Bluetooth Smart Lock Linkquality\"," +
  + "\"device\":{\"identifiers\":[\"keyble_" + KeyBleMac + "\"]," +
  + "\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\",\"name\":\"Eqiva Bluetooth Smart Lock\"}," +
  + "\"uniq_id\":\"keyble_" + KeyBleMac + "_linkquality\"," +
  + "\"stat_t\":\"~" + MQTT_PUB_RSSI + "\"," +
  + "\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
  + "\"icon\":\"mdi:signal\"," +
  + "\"unit_of_meas\":\"rssi\"}";

  Serial.println("# " + HomeAssistantMqttPrefix + "/sensor/KeyBLE/linkquality/config");
  mqttClient.publish((HomeAssistantMqttPrefix + "/sensor/KeyBLE/linkquality/config").c_str(), link_quality_conf.c_str(), true);

  String battery_conf = "{\"~\": \"" + MqttTopic + "\"," +
  + "\"name\":\"Eqiva Bluetooth Smart Lock Battery\"," +
  + "\"device\":{\"identifiers\":[\"keyble_" + KeyBleMac + "\"]," +
  + "\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\", \"name\":\"Eqiva Bluetooth Smart Lock\" }," +
  + "\"uniq_id\":\"keyble_" + KeyBleMac + "_battery\"," +
  + "\"stat_t\":\"~" + MQTT_PUB_BATT + "\"," +
  + "\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
  + "\"dev_cla\":\"battery\"}";
 
  Serial.println("# " + HomeAssistantMqttPrefix + "/binary_sensor/KeyBLE/battery/config");
  mqttClient.publish((HomeAssistantMqttPrefix + "/binary_sensor/KeyBLE/battery/config").c_str(), battery_conf.c_str(), true);

  // Reset buffer size to default
  mqttClient.setBufferSize(256);

  Serial.println("# Home Assistant autodiscovery configured");
}
// ---[MQTT-Setup]--------------------------------------------------------------
void SetupMqtt() {
  // Skip if dirty config
  if (mqttDirtyConfig) return;

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
      mqttDirtyConfig = false;
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
// ---[Bluetooth-Setup]--------------------------------------------------------------
void SetupBluetooth() {
  // Skip if dirty config
  if (bleDirtyConfig) return;

  SetWifi(false);
  Serial.println("# Setting up bluetooth");
  BLEDevice::init("");
  Serial.println("# Checking if KeyBle lock is in range");
  keyble = new eQ3(KeyBleMac.c_str(), KeyBleUserKey.c_str(), KeyBleUserId.toInt());
  keyble->_LockStatus = LockStatus::UNKNOWN;
  keyble->updateInfo(); // Trigger status update
  
  unsigned int timeout = millis() + 60000; // 60 secs
  
  bool is_timeout = false;
  while (!is_timeout && keyble->_LockStatus == LockStatus::UNKNOWN)
  {// loop until timeout or lock status updates
    is_timeout = millis() > timeout;
  }

  if (is_timeout)
  {
    Serial.println("# KeyBle lock device not found! Please check settings");
    bleDirtyConfig = true;
  }
  else if (keyble->_LockStatus > LockStatus::UNKNOWN)
  {
    Serial.println("# Bluetooth Setup successful");
    bleDirtyConfig = false;
  }

  SetWifi(true);
  
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
    String mqttConfStatus = (mqttDirtyConfig ? "ERROR" : "OK");
    String mqttConfStatusColor = (mqttDirtyConfig ? "red" : "green");
    String bleConfStatus = (bleDirtyConfig ? "ERROR" : "OK");
    String bleConfStatusColor = (bleDirtyConfig ? "red" : "green");

    content +=
     "<h2 align=\"center\" style=\"color:" + mqttConfStatusColor + ";margin:20px;\">MQTT Configuration: " + mqttConfStatus + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT Server: " + MqttServerName + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT Port: " + MqttPort + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT User Name: " + MqttUserName + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">MQTT Topic: " + MqttTopic + "</h2>"
     "<h2 align=\"center\" style=\"color:" + bleConfStatusColor + ";margin:20px;\">KeyBLE Configuration: " + bleConfStatus + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE MAC Address: " + KeyBleMac + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE User Key: " + KeyBleUserKey + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE User ID: " + KeyBleUserId + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE refresh interval: " + KeyBleRefreshInterval + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last battery state: " + mqtt_pub_battery_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last command received: " + mqtt_sub_command_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last rssi: " + mqtt_pub_rssi_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last state: " + mqtt_pub_state_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last lock state: " + mqtt_pub_lock_state_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">KeyBLE last availability: " + mqtt_pub_availability_value + "</h2>"
     "<h2 align=\"center\" style=\"color:green;margin:20px;\">Home Assistant MQTT Topic Prefix: " + HomeAssistantMqttPrefix + "</h2>"
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
    //Home Assistant autodiscovery
    SetupHomeAssistant();
    //Bluetooth
    SetupBluetooth();
  }
}
// ---[loop]--------------------------------------------------------------------
void loop() {

if(configurationChanged) {
  mqttDirtyConfig = false;
  bleDirtyConfig = false;

  SetupMqtt();
  SetupHomeAssistant();
  SetupBluetooth();

  configurationChanged = false;
}

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

if ((do_open || do_lock || do_unlock || do_status || do_toggle || do_pair) && !bleDirtyConfig) 
{
  //delay(200);
  SetWifi(false);
  yield();
  waitForAnswer=true;
  keyble->_LockStatus = LockStatus::UNKNOWN;
  operationStartTime = millis();

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
    if ((status == LockStatus::UNLOCKED) || (status == LockStatus::OPENED))
    {
      keyble->lock();
      do_lock = false;
    }
    if (status == LockStatus::LOCKED)
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
    bool timeout=(millis() - operationStartTime > LOCK_TIMEOUT *2000 +1000);
    bool finished=(keyble->_LockStatus > LockStatus::MOVING);

    if (finished || timeout)
    {
      if (timeout) Serial.println("# Lock timed out!");
      
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
      
      if (KeyBleRefreshInterval != "0")
      {
        // reset refresh counter
        // refresh every 5 seconds if timeout, else normal interval
        previousMillis = timeout ? millis() - KeyBleRefreshInterval.toInt() + 5000 : millis();
      }
    }
  }
  //Periodic status refresh logic, executed only if no commands are waiting
  else if (KeyBleRefreshInterval != "0" && !bleDirtyConfig) 
  {
    currentMillis = millis();
    if (currentMillis - previousMillis > KeyBleRefreshInterval.toInt())
    {
      do_status = true; //request status update
      previousMillis = currentMillis; //reset refresh counter
    }
  }
}
