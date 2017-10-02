#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#define CONFIG_VERSION "002"

//flag for saving data
bool shouldSaveConfig = false;

//
// callback notifying us of the need to save config
//
void saveConfigCallback () {
  Serial.println(F("Should save config"));
  shouldSaveConfig = true;
}

//
// Init thw WiFiManager
// If zapall is true the confiuration will be completely zapped
//
void initWifiManager(boolean zapall) {

  Serial.println(F("initWifiManager"));
  if (zapall)
    Serial.println(F("Wifi zap requested"));
    
  //read configuration from FS json
  Serial.println(F("mounting FS..."));
  boolean zapFS = false;

  if (SPIFFS.begin()) {
    Serial.println(F("mounted file system"));
    if (SPIFFS.exists("/config.json")) {
      // file exists, reading and loading
      Serial.println(F("reading config file"));
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println(F("opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success())
        {
          Serial.println(F("\nparsed json"));
          if (json.containsKey("cfg_version") && !strncasecmp_P(json["cfg_version"], CONFIG_VERSION, 3))
          {
            strcpy(mqtt_server, json["mqtt_server"]);
            strcpy(mqtt_port, json["mqtt_port"]);
            strcpy(mqtt_user, json["mqtt_user"]);
            strcpy(mqtt_pass, json["mqtt_pass"]);
            strcpy(unit_id, json["unit_id"]);
            strcpy(group_id, json["group_id"]);
          }
          else
          {
            Serial.println(F("version mismatch for json config"));
            zapFS = true;
          }
        }
        else
        {
          Serial.println(F("failed to parse json config"));
          zapFS = true;
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
    zapFS = true;
  }

  // clean FS
  if (zapFS) {
    Serial.println(F("Zapping FS..."));
    SPIFFS.format();
  }

  // initialize wifimanager parameters, from default or from config
  custom_mqtt_server = WiFiManagerParameter("server", "mqtt server", mqtt_server, 40);
  custom_mqtt_port = WiFiManagerParameter("port", "mqtt port", mqtt_port, 6);
  custom_mqtt_user = WiFiManagerParameter("user", "mqtt user", mqtt_user, 24);
  custom_mqtt_pass = WiFiManagerParameter("pass", "mqtt pass", mqtt_pass, 24);
  custom_unit_id = WiFiManagerParameter("unit", "unit id", unit_id, 16);
  custom_group_id = WiFiManagerParameter("group", "group id", group_id, 16);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10, 0, 1, 99), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

  // add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_unit_id);
  wifiManager.addParameter(&custom_group_id);

  // reset settings - for testing
  if (zapall) {
    Serial.println(F("Zapping wifi manager settings..."));
    wifiManager.resetSettings();
  }

  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality(20);

  // sets timeout until configuration portal gets turned off
  // useful to make it all retry or go to sleep
  // in seconds
  //wifiManager.setTimeout(120);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "Sonoff-Touch-WallSw"
  // and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("Sonoff-Touch-WallSw", "wificonfig")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  // if you get here you have connected to the WiFi
  Serial.println("connected!");

  // read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(unit_id, custom_unit_id.getValue());
  strcpy(group_id, custom_group_id.getValue());

  // save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println(F("saving config"));
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["cfg_version"] = CONFIG_VERSION;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;
    json["unit_id"] = unit_id;
    json["group_id"] = group_id;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("failed to open config file for writing"));
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  Serial.println();
  Serial.println(F("local ip"));
  Serial.println(WiFi.localIP());
}
