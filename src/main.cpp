#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <DHT.h>
#include <DHT_U.h>
#include "Passwords.h"

#define SWITCH1 D4
#define ONE_WIRE_BUS D7
#define RELAY1 D5 
#define RELAY2 D6
#define RELAY3 D8
#define GET_TEMP_INTERVAL 60000
#define SWITCH_INTERVAL 2000
#define MSG_MAX_LEN 100
#define DHTTYPE           DHT22     // DHT 22 (AM2302)
#define HUMIDYHISTORY_LEN 50

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char* mqtt_server = "10.1.1.201";
const char* hostName = "Well";
const char* mqttTopic = "Well";
const char* update_path = "/firmware";
const char* update_username = UPDATE_USERNAME;
const char* update_password = UPDATE_PASSWORD;

IPAddress ip(10,1,1,50);  

uint32_t delayMS;


ESP8266WebServer server ( 80 );
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_INA219 ina219;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


long lastMsg = 0;
long lastGetTemp = 0;
long lastSwitchTemp = 0;
bool relay1State = false;
bool relay2State = false;
bool relay3State = false;

char msg[MSG_MAX_LEN+1];
int value = 0;

float air_temp_last=-100;
float relative_humidity_last=-100;
float waterTemp_last=-100;
float humidityHistory[HUMIDYHISTORY_LEN];

float current_mA = 0;
float busvoltage = 0;

const char HTTP_HEAD[] PROGMEM=
"<!DOCTYPE html><html lang=\"en\" class=\"\">"
"<head>"
"<meta charset='utf-8'>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\"/>"
"<title>Cisterna</title>"
"<script>"
"var cn,x,lt;"
"x=null;"                  // Allow for abortion
"function la(p){"
  "var a='?Status=0';"
  "if(la.arguments.length==1){"
    "a='?Relay='+p;"
    "clearTimeout(lt);"
  "}"
  "if(x!=null){x.abort();}"    // Abort if no response within 2 seconds (happens on restart 1)
  "x=new XMLHttpRequest();"
  "x.onreadystatechange=function(){"
    "if(x.readyState==4&&x.status==200){"
      "document.getElementById('relayStat1').innerHTML=x.responseText;"
    "}"
  "};"
  "x.open('GET','CMD'+a,true);"
  "x.send();"
  "lt=setTimeout(la,2345);"
"}"
"setTimeout(la,2000);"
"</script>"
"<style>"
"div,fieldset,input,select{padding:5px;font-size:1em;}"
"input{width:95%;}"
"select{width:100%;}"
"textarea{resize:none;width:98%;height:318px;padding:5px;overflow:auto;}"
"body{text-align:center;font-family:verdana;}"
"td{padding:0px;}"
"button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;-webkit-transition-duration:0.4s;transition-duration:0.4s;}"
"button:hover{background-color:#006cba;}"
".p{float:left;text-align:left;}"
".q{float:right;text-align:right;}"
"</style>"
"</head>"
"<body>"
"<div style='text-align:left;display:inline-block;min-width:340px;'>"
"<div style='text-align:center;'><h3>Casa - Cisterna</h3></div>"
"<div id='l1' name='l1'><table style='width:100%'><tr><td style='width:100%'><div style='text-align:center;font-weight:bold;font-size:62px' id='relayStat1' name=''relayStat1'></div></td></tr></table></div>"
"<table style='width:100%'><tbody><tr><td style='width:100%'><button onclick='la(1);'>Toggle 1</button></td></tr></tbody></table>"
"<table style='width:100%'><tbody><tr><td style='width:100%'><button onclick='la(2);'>Toggle 2</button></td></tr></tbody></table>"
"<table style='width:100%'><tbody><tr><td style='width:100%'><button onclick='la(3);'>Toggle 3</button></td></tr></tbody></table>";

void handleRoot() {
  char tempBuff[400];
  String out = FPSTR(HTTP_HEAD);

	int sec = millis() / 1000;
	int min = sec / 60;
  int hr = min / 60;
  snprintf ( tempBuff, 400,"Uptime: %02d:%02d:%02d",hr, min % 60, sec % 60);
  String temp = "Pump Temp:"+String(waterTemp_last,2);
  out += String(tempBuff)+"</p>";
  out += "<p>"+temp+"</p>";
  temp = "Current:"+String(current_mA,2)+"mA";
  out += "<p>"+temp+"</p>";
  temp = "Voltage:"+String(busvoltage,2)+"V";
  out += "<p>"+temp+"</p>";
  out+="<img src=\"/test.svg\" />";
	server.send ( 200, "text/html", out );
}

void humidityPush(){
  for(int i=0; i<HUMIDYHISTORY_LEN-1; i++){
    humidityHistory[i]=humidityHistory[i+1];
    Serial.print(" ");
    Serial.print(humidityHistory[i]);
  }
  Serial.println(" ");
  humidityHistory[HUMIDYHISTORY_LEN-1] = relative_humidity_last;
}


void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";

	for ( uint8_t i = 0; i < server.args(); i++ ) {
		message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
	}

	server.send ( 404, "text/plain", message );
}


void relay1Toggle(){
  relay1State = !relay1State;
  digitalWrite ( RELAY1, relay1State);
  String pubTopic = "stat/"+String(mqttTopic)+"/POWER";
  client.publish(pubTopic.c_str(), relay1State?"ON":"OFF");  
}

void relay2Toggle(){
  relay2State = !relay2State;
  digitalWrite ( RELAY2, relay2State);
  String pubTopic = "stat/"+String(mqttTopic)+"/POWER";
  client.publish(pubTopic.c_str(), relay2State?"ON":"OFF");  
}

void relay3Toggle(){
  relay3State = !relay3State;
  digitalWrite ( RELAY3, relay3State);
  String pubTopic = "stat/"+String(mqttTopic)+"/POWER";
  client.publish(pubTopic.c_str(), relay3State?"ON":"OFF");  
}


void sendRelayStatus(){
  String messageR = "Relay 1:";
  messageR+=relay1State?"ON":"OFF";
  messageR+=" Relay 2:";
  messageR+=relay2State?"ON":"OFF";
  messageR+=" Relay 3:";
  messageR+=relay3State?"ON":"OFF";
  server.send (200, "text/plain", messageR);
}


void handleCMD() {
  if(server.method() == HTTP_GET) {
    if(server.args()>0){
      if(server.argName(0)=="Relay" || server.argName(0)=="Status"){
        if(server.arg(0)=="1"){
          relay1Toggle();
        }
        if(server.arg(0)=="2"){
          relay2Toggle();
        }
        if(server.arg(0)=="3"){
          relay3Toggle();
        }
        sendRelayStatus();        
        return;
      }  
    }
  } 
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";

	for ( uint8_t i = 0; i < server.args(); i++ ) {
		message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
	}
	server.send ( 404, "text/plain", message );
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  relay1Toggle();
  Serial.println();
}

void getWaterTemp() {
  long now = millis();
  Serial.print("Requesting temperatures ");
  sensors.requestTemperatures(); // Send the command to get temperatures
  Serial.print("elapsed:");
  Serial.print(millis()-now);
  waterTemp_last = sensors.getTempCByIndex(0);
  Serial.println(" "+String(waterTemp_last,2)+"C");
}

void setup ( void ) {
  pinMode (SWITCH1,INPUT_PULLUP);
  
  pinMode ( RELAY1, OUTPUT );
  pinMode ( RELAY2, OUTPUT );
  pinMode ( RELAY3, OUTPUT );
  Serial.begin ( 115200 );
  IPAddress gateway(10, 1,1, 1);  
  IPAddress subnet(255, 255, 255, 0);  
  WiFi.config(ip, gateway, subnet);
  WiFi.hostname(hostName);
	WiFi.begin ( ssid, password );
	Serial.println ( "" );
  MDNS.begin(hostName);
  ina219.begin();
	// Wait for connection
	while ( WiFi.status() != WL_CONNECTED ) {
		delay ( 500 );
		Serial.print ( "." );
	}

	Serial.println ( "" );
	Serial.print ( "Connected to " );
	Serial.println ( ssid );
	Serial.print ( "IP address: " );
	Serial.println ( WiFi.localIP() );

	if ( MDNS.begin ( "esp8266" ) ) {
		Serial.println ( "MDNS responder started" );
  }
  
  sensors.begin();
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" temp devices.");  
  Serial.print("Parasite power is: "); 
  if (sensors.isParasitePowerMode()) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");  
  }

 
  httpUpdater.setup(&server, update_path, update_username, update_password);
	server.on ( "/", handleRoot);
  server.on ( "/CMD", handleCMD);
	server.onNotFound ( handleNotFound );
	server.begin();
  Serial.println ( "HTTP server started" );
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);  
  getWaterTemp();
}




void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      String pubTopic = "tele/"+String(mqttTopic)+"/LWT";
      client.publish(pubTopic.c_str(), "Online");
      // ... and resubscribe
      String subTopic = "cmnd/"+String(mqttTopic)+"/POWER";
      client.subscribe(subTopic.c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void publishData(){
  long rssi = WiFi.RSSI();
  String tempStr(waterTemp_last,2);
  tempStr = "{'PumpTemp':'"+tempStr+"','rssi':'"+rssi+"','Relay1':'"+relay1State+"','Relay2':'"+relay2State+"','Current':'"+current_mA+"','Voltage':'"+busvoltage+"'}";
  tempStr.toCharArray(msg, MSG_MAX_LEN);
  Serial.print("Publish message: ");
  Serial.println(msg);
  String pubTopic = "tele/"+String(mqttTopic)+"/STATE";
  client.publish(pubTopic.c_str(), msg);
}

void loop ( void ) {
  server.handleClient();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  long now = millis();
  if(now-lastGetTemp>GET_TEMP_INTERVAL){
    getWaterTemp();
    lastGetTemp=now;
    current_mA = ina219.getCurrent_mA();
    busvoltage = ina219.getBusVoltage_V();
    Serial.print("Current:       "); Serial.print(current_mA); Serial.println(" mA");
    Serial.print("Bus Voltage:   "); Serial.print(busvoltage); Serial.println(" V");
  }
  if (now - lastMsg > GET_TEMP_INTERVAL) {
    publishData();
    lastMsg = now;
    ++value;
  } 
  if(now-lastSwitchTemp>SWITCH_INTERVAL){
    if(digitalRead(SWITCH1)==0){
      Serial.println("SWITCH1==0");
      relay1Toggle();
      lastSwitchTemp = now;
    }
  }
}

