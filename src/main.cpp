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
#include <DHT.h>
#include <DHT_U.h>
#include "Passwords.h"

#define SWITCH D6
#define ONE_WIRE_BUS D2
#define DHTPIN D3         // Pin which is connected to the DHT sensor.
#define RELAY1 D5         // Pin which is connected to the DHT sensor.
#define GET_TEMP_INTERVAL 60000 * 5
#define SWITCH_INTERVAL 2000
#define MSG_MAX_LEN 100
#define DHTTYPE           DHT22     // DHT 22 (AM2302)
#define HUMIDYHISTORY_LEN 50

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char* mqtt_server = "10.1.1.201";
const char* hostName = "SwimmingPool";
const char* mqttTopic = "SwimmingPool";
const char* update_path = "/firmware";
const char* update_username = UPDATE_USERNAME;
const char* update_password = UPDATE_PASSWORD;

IPAddress ip(10,1,1, 89);  

DHT_Unified dht(DHTPIN, DHTTYPE);
uint32_t delayMS;


ESP8266WebServer server ( 80 );
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient espClient;
PubSubClient client(espClient);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


long lastMsg = 0;
long lastGetTemp = 0;
long lastSwitchTemp = 0;
bool relay1State = false;

char msg[MSG_MAX_LEN+1];
int value = 0;

float air_temp_last=-100;
float relative_humidity_last=-100;
float waterTemp_last=-100;
float humidityHistory[HUMIDYHISTORY_LEN];

const char HTTP_HEAD[] PROGMEM=
"<!DOCTYPE html><html lang=\"en\" class=\"\">"
"<head>"
"<meta charset='utf-8'>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\"/>"
"<title>Casa Jardim</title>"
"<script>"
"var cn,x,lt;"
"x=null;"                  // Allow for abortion
"function la(p){"
  "var a='?Status=1';"
  "if(la.arguments.length==1){"
    "a='?Relay1='+p;"
    "clearTimeout(lt);"
  "}"
  "if(x!=null){x.abort();}"    // Abort if no response within 2 seconds (happens on restart 1)
  "x=new XMLHttpRequest();"
  "x.onreadystatechange=function(){"
    "if(x.readyState==4&&x.status==200){"
      "document.getElementById('relayStat').innerHTML=x.responseText;"
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
"<div style='text-align:center;'><h3>Casa - Jardim</h3></div>"
"<div id='l1' name='l1'><table style='width:100%'><tr><td style='width:100%'><div style='text-align:center;font-weight:bold;font-size:62px' id='relayStat' name=''relayStat'>ON</div></td></tr></table></div>"
"<table style='width:100%'><tbody><tr><td style='width:100%'><button onclick='la(1);'>Toggle</button></td></tr></tbody></table>";

void handleRoot() {
  char tempBuff[400];
  String out = FPSTR(HTTP_HEAD);

	int sec = millis() / 1000;
	int min = sec / 60;
  int hr = min / 60;
  snprintf ( tempBuff, 400,"Uptime: %02d:%02d:%02d",hr, min % 60, sec % 60);
  String temp = "Water Temp:"+String(waterTemp_last,2);
  out += String(tempBuff)+"</p>";
  out += "<p>"+temp+"</p>";
  temp = "Air Temp:"+String(air_temp_last,2);
  out += "<p>"+temp+"</p>";
  temp = "Air humidity:"+String(relative_humidity_last,0)+"%";
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


void dthSetup(){
  dht.begin();
    // Print temperature sensor details.
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.println("Temperature");
  Serial.print  ("Sensor:       "); Serial.println(sensor.name);
  Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" *C");
  Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" *C");
  Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" *C");  
  Serial.println("------------------------------------");
  // Print humidity sensor details.
  dht.humidity().getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.println("Humidity");
  Serial.print  ("Sensor:       "); Serial.println(sensor.name);
  Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println("%");
  Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println("%");
  Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println("%");  
  Serial.println("------------------------------------");
}

void dht_read(){
  sensors_event_t event;  
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println("Error reading temperature!");
    air_temp_last = -100;
  }
  else {
    Serial.print("Temperature: ");
    Serial.print(event.temperature);
    Serial.println(" *C");
    air_temp_last = event.temperature;
  }
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    Serial.println("Error reading humidity!");
    relative_humidity_last =-100;
  } else {
    Serial.print("Humidity: ");
    Serial.print(event.relative_humidity);
    Serial.println("%");
    relative_humidity_last = event.relative_humidity;
    humidityPush();
  }  
}

void drawGraph() {
  String out = "";
  
	char temp[100];
	out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"150\">\n";
 	out += "<rect width=\"400\" height=\"150\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
 	out += "<g stroke=\"black\">\n";
 	int y = rand() % 130;
// 	for (int x = 10; x < 390; x+= 10) {
 	for (int x = 0; x < HUMIDYHISTORY_LEN-1; x++) {
 		int y2 = rand() % 130;
 		sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", (x+10)*10, 120 - humidityHistory[x], (x+10)*10 + 20, 140 - humidityHistory[x+1]);
// 		sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", x, 140 - y, x + 10, 140 - y2);
 		out += temp;
 		y = y2;
 	}
	out += "</g>\n</svg>\n";

	server.send ( 200, "image/svg+xml", out);
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

void handleCMD() {
  if(server.method() == HTTP_GET) {
    if(server.args()>0){
      if(server.argName(0)=="Relay1"){
        if(server.arg(0)=="1"){
          relay1Toggle();
          server.send (200, "text/plain", relay1State?"ON":"OFF");
          return;
        }
      } else {
        server.send (200, "text/plain", relay1State?"ON":"OFF");
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
  pinMode (SWITCH,INPUT_PULLUP);
  pinMode ( RELAY1, OUTPUT );
  Serial.begin ( 115200 );
  IPAddress gateway(10, 1,1, 1);  
  IPAddress subnet(255, 255, 255, 0);  
  WiFi.config(ip, gateway, subnet);
  WiFi.hostname(hostName);
	WiFi.begin ( ssid, password );
	Serial.println ( "" );
  MDNS.begin(hostName);
 
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

  dthSetup();
  
  httpUpdater.setup(&server, update_path, update_username, update_password);
	server.on ( "/", handleRoot);
  server.on ( "/CMD", handleCMD);
  server.on ( "/test.svg", drawGraph);
	server.onNotFound ( handleNotFound );
	server.begin();
  Serial.println ( "HTTP server started" );
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);  
  getWaterTemp();
  dht_read();
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
  tempStr = "{'WaterTemp':'"+tempStr+"','rssi':'"+rssi+"','Relay1':'"+relay1State+"','AirTemp':'"+air_temp_last+"','RelativeHumidity':'"+relative_humidity_last+"'}";
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
    dht_read();
    lastGetTemp=now;
  }
  if (now - lastMsg > GET_TEMP_INTERVAL) {
    publishData();
    lastMsg = now;
    ++value;
  } 
  if(now-lastSwitchTemp>SWITCH_INTERVAL){
    if(digitalRead(SWITCH)==0){
      Serial.println("SWITCH==0");
      relay1Toggle();
      lastSwitchTemp = now;
    }
  }
}

