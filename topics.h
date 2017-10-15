#include <WiFiManager.h>

////// config values
//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "10.0.1.50";
char mqtt_port[6] = "1883";
char mqtt_user[24] = "";
char mqtt_pass[24] = "";
char unit_id[16] = "tosw0";
char group_id[16] = "toswgrp0";

// The extra parameters to be configured (can be either global or just in the setup)
// After connecting, parameter.getValue() will get you the configured value
// id/name placeholder/prompt default length
//WiFiManagerParameter custom_mqtt_server = NULL;


// these are defined after wifi connect and parameters are set (in setup())
//String sensorTopic;       // publish sensor value


#define MAX_SUBSCRIBED_TOPICS 6
String* subscribedTopics[MAX_SUBSCRIBED_TOPICS];
uint8_t noSubscribedTopics = 0;

