/*
   Sonoff-Touch-WallSw
   Firmware to use a Sonoff Touch device as a wall switch with integrated relay, MQTT and WiFi capabilities.

   Supports OTA update
   Mats Ekberg (C) 2017 GNU GPL v3

   Runs on this harware:
   http://sonoff.itead.cc/en/products/residential/sonoff-touch

   Flashed via USB/OTA in Arduino IDE with these parameters:
   Board:       Generic ESP8285 Module
   Flash size:  1M (64K SPIFFS)
   
   Based on this work:
   https://github.com/davidmpye/Sonoff-Touch-MQTT
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#define LONG_PRESS_MS 1000
#define SHORT_PRESS_MS 100
#define CONFIG_WIFI_PRESS_MS 5000
#define CONFIG_TOUCHES_COUNT 3

#define MQTT_CHECK_MS 30000

#define F(x) (x)

////// config values
//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "10.0.1.50";
char mqtt_port[6] = "1883";
char mqtt_user[24] = "";
char mqtt_pass[24] = "";
char unit_id[16] = "wallsw0";
char group_id[16] = "wallswgrp0";

// The extra parameters to be configured (can be either global or just in the setup)
// After connecting, parameter.getValue() will get you the configured value
// id/name placeholder/prompt default length
WiFiManagerParameter custom_mqtt_server = NULL;
WiFiManagerParameter custom_mqtt_port = NULL;
WiFiManagerParameter custom_mqtt_user = NULL;
WiFiManagerParameter custom_mqtt_pass = NULL;
WiFiManagerParameter custom_unit_id = NULL;
WiFiManagerParameter custom_group_id = NULL;
///////


#define OTA_PASS "UPDATE_PW"
#define OTA_PORT 8266

#define BUTTON_PIN 0
#define RELAY_PIN 12
#define LED_PIN 13

volatile int desiredRelayState = 0;
volatile int relayState = 0;
volatile unsigned long millisSinceChange = 0;
volatile int noOfConfigTouches = 0;

volatile boolean sendGroupEventTopic = false;
volatile boolean configWifi = false;
volatile boolean sendEvent = true;
boolean sendStatus = true;

unsigned long lastMQTTCheck = -MQTT_CHECK_MS; //This will force an immediate check on init.

WiFiClient espClient;
PubSubClient client(espClient);

bool printedWifiToSerial = false;

// these are defined after wifi connect and parameters are set (in setup())
String eventTopic;       // published when the switch is touched
String groupEventTopic;  // published when the switch was long touched
String statusTopic;      // published when the relay changes state wo switch touch
String actionTopic;      // subscribed to to change relay status

//
// Connect to MQTT broker
// Subscribe to topics, flash LED etc
//
void checkMQTTConnection() {
  Serial.print(F("MQTT conn? "));
  if (client.connected()) Serial.println(F("OK"));
  else {
    if (WiFi.status() == WL_CONNECTED) {
      //Wifi connected, attempt to connect to server
      Serial.print(F("new connection: "));
      if (client.connect(custom_unit_id.getValue(), custom_mqtt_user.getValue(), custom_mqtt_pass.getValue())) {
        Serial.println(F("connected"));
        client.subscribe(actionTopic.c_str());
      } else {
        Serial.print(F("failed, rc="));
        Serial.println(client.state());
      }
    }
    else {
      //Wifi isn't connected, so no point in trying now.
      Serial.println(F(" Not connected to WiFI AP, abandoned connect."));
    }
  }
  //Set the status LED to ON if we are connected to the MQTT server
  if (client.connected())
    digitalWrite(LED_PIN, LOW);
  else
    digitalWrite(LED_PIN, HIGH);
}


//
// MQTT message arrived, decode
// Ok payload: 1/on, 0/off, X/toggle, S/status
//
void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  Serial.print(F("MQTT sub: "));
  Serial.print(topic);
  Serial.println(F(" = "));

  if (!strcmp(topic, actionTopic.c_str())) {
    if ((char)payload[0] == '1' || ! strncasecmp_P((char *)payload, "on", length)) {
      desiredRelayState = 1;
    }
    else if ((char)payload[0] == '0' || ! strncasecmp_P((char *)payload, "off", length)) {
      desiredRelayState = 0;
    }
    else if ((char)payload[0] == 'X' || ! strncasecmp_P((char *)payload, "toggle", length)) {
      desiredRelayState = !desiredRelayState;
    }
    else if ((char)payload[0] == 'S' || ! strncasecmp_P((char *)payload, "status", length)) {
      sendStatus = true;
    }
  }
}

//
// Handle short touch
//
void shortPress() {
  desiredRelayState = !desiredRelayState; //Toggle relay state.
  sendGroupEventTopic = false;
  sendEvent = true;
  noOfConfigTouches = 0;
}

//
// Handle long touch
//
void longPress() {
  desiredRelayState = !desiredRelayState; //Toggle relay state.
  sendGroupEventTopic = true;
  sendEvent = true;
  noOfConfigTouches = 0;
}

//
// Handle looong config touch
//
void configWifiPress() {
  noOfConfigTouches++;
  if (noOfConfigTouches >= CONFIG_TOUCHES_COUNT)
    configWifi = true;
}


//
// This is executed on touch
//
void buttonChangeCallback() {
  if (digitalRead(0) == 1) {

    // Button has been released, trigger one of the two possible options.
    if (millis() - millisSinceChange > CONFIG_WIFI_PRESS_MS) {
      configWifiPress();
    }
    else if (millis() - millisSinceChange > LONG_PRESS_MS) {
      longPress();
    }
    else if (millis() - millisSinceChange > SHORT_PRESS_MS) {
      shortPress();
    }
    else {
      //Too short to register as a press
    }
  }
  else {
    //Just been pressed - do nothing until released.
  }
  millisSinceChange = millis();
}


//
// This routine handles state changes and MQTT publishing
//
void handleStatusChange() {

  //Relay state is updated via the interrupt *OR* the MQTT callback.
  if (relayState != desiredRelayState) {
    Serial.print(F("Chg state to "));
    Serial.println(desiredRelayState);

    digitalWrite(RELAY_PIN, desiredRelayState);
    relayState = desiredRelayState;
    sendStatus = true;
  }

  // publish event if touched
  if (sendEvent) {
    const char* payload = (relayState == 0) ? "0" : "1";
    Serial.print(F("MQTT pub: "));
    Serial.print(payload);
    Serial.print(F(" to "));
    if (sendGroupEventTopic) {
      Serial.println(groupEventTopic);
      client.publish(groupEventTopic.c_str(), payload);
    } else {
      Serial.println(eventTopic);
      client.publish(eventTopic.c_str(), payload);
    }
    sendEvent = false;
  }

  // publish state when requested to do so
  if (sendStatus) {
    const char* payload = (relayState == 0) ? "0" : "1";
    Serial.print(F("MQTT pub: "));
    Serial.print(payload);
    Serial.print(F(" to "));
    Serial.println(statusTopic);
    client.publish(statusTopic.c_str(), payload);
    sendStatus = false;
  }

}


//
////////// SETUP //////////
//
void setup() {
  Serial.begin(115200);
  Serial.println(F("Initialising"));
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, HIGH); //LED off.

  initWifiManager(false);

  // after wifi and parameters are configured, create publish topics
  eventTopic = String(F("event/")) + custom_unit_id.getValue() + String(F("/switch"));
  groupEventTopic = String(F("event/")) + custom_group_id.getValue() + String(F("/switch"));
  statusTopic = String(F("status/")) + custom_unit_id.getValue() + String(F("/relay"));
  // and subscribe topic
  actionTopic = String(F("action/")) + custom_unit_id.getValue() + String(F("/relay"));

  client.setServer(custom_mqtt_server.getValue(), atoi(custom_mqtt_port.getValue()));
  client.setCallback(MQTTcallback);

  // OTA setup
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(custom_unit_id.getValue());
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();

  // Enable interrupt for button press
  Serial.println(F("Enabling touch switch interrupt"));
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonChangeCallback, CHANGE);
}


//
////////// LOOP //////////
//
void loop() {
  // If we haven't printed WiFi details to Serial port yet, and WiFi now connected,
  // do so now. (just the once)
  if (!printedWifiToSerial && WiFi.status() == WL_CONNECTED) {
    Serial.println(F("WiFi connected"));
    Serial.println(F("IP address: "));
    Serial.println(WiFi.localIP());
    printedWifiToSerial = true;
  }

  // Check MQTT connection
  if (millis() - lastMQTTCheck >= MQTT_CHECK_MS) {
    checkMQTTConnection();
    lastMQTTCheck = millis();
  }

  // Handle any pending MQTT messages
  client.loop();

  // Handle any pending OTA SW updates
  ArduinoOTA.handle();

  // Handle any state change and MQTT publishing
  handleStatusChange();

  // Handle looong touch to reconfigure all parameters
  if (configWifi) {
    espClient.stop();
    delay(1000);
    initWifiManager(true);
  }

  delay(50);
}
