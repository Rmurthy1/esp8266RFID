#include "networking.h"
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>

#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "secureConfig.h"
#include <WiFiClientSecure.h>

JsonDocument doc;
    
ESP8266WiFiMulti WiFiMulti;
WiFiClientSecure  client;

char ssid[] = WIFI_SSID;   // your network SSID (name) 
char pass[] = WIFI_PASSWORD;   // your network password

bool lightBlink = false;

int blinking_period = 1000; // time between blinks

// this fingerprint stuff is not used at the moment, we send data insecurely.
// Fingerprint check, make sure that the certificate has not expired.
const char * fingerprint = SECRET_SHA1_FINGERPRINT; // use SECRET_SHA1_FINGERPRINT for fingerprint check

bool sendData = true;
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = THINGSPEAK_API_WRITE;

int number = 0;
int delayTime = 10000 * 2; // delay between writes to ThingSpeak in ms

// Function declarations
void sendDataToThingSpeak(String data);
void blinkLight(bool keepBlinking);
void thingSpeakWriteREST(String data);
void prepareJSON(String message);
String getValue(String data, char separator, int index);
void wifiStatusLED();
void updateRate(int rate);
void writeDataToFireBaseDatabase(String payload, String endpoint, bool &success);

// update the delay time between writes to ThingSpeak, in ms. default is 10000
void updateRate(int rate) {
  delayTime = rate;
}

// called from user of library, sends data to ThingSpeak every delayTime ms
void sendDataToThingSpeak(String data) {
  
  
  // flips between 0 and 1 every 60 seconds. (0 to min 1 = 0, 1 to 2 = 1, 2 to 3 = 0, etc) (sends data every two minutes, when 60000)
  // every (2 * 60000) seconds
  if (((millis() / delayTime) % 2)) {
   
    if (sendData == true) {
      wifiStatusLED();
      Serial.println("data sent to thinkspeak: " + (data));
      thingSpeakWriteREST(data);
      sendData = false;
    }
  } else {
    sendData = true;
  }
}

// called from loop, just blinks the light.
void blinkLight(bool keepBlinking) {
  if (lightBlink == true) {
    digitalWrite(LED_BUILTIN, (millis() / blinking_period) % 2);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}

// write to thingspeak using a global api key and parameter data
void thingSpeakWriteREST(String data) {

  // prepare the json file then make it into a string
  prepareJSON(data);
  String payload;
  serializeJson(doc, payload);
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient https;
  String endpoint = "https://api.thingspeak.com/update.json";

  if (https.begin(*client, endpoint)) {
    https.addHeader("Content-Type", "application/json");
    int httpCode = https.POST(payload);
    if (httpCode > 0) {
      Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String serverUpdate = https.getString();
        Serial.println(serverUpdate);
        //success = true; // this should be passed in as a reference
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();

  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  
}

// prepare the JSON object to be sent to ThingSpeak, this is called from sendDataToThingSpeak, which is called from the user of the library.
// the data is a string that is tokenized by the semicolon, then the tokens are counted and the data is prepared.
// the data is then put into the JSON object.
// the JSON object is then serialized into a string.
// the string is then sent to ThingSpeak.
void prepareJSON(String message) {
  doc["api_key"] = THINGSPEAK_API_WRITE;
  // tokenize the string, count the tokens, prepare the data.
  String totalTokens = getValue(message, ';', 0);
  int totalTokensCount = totalTokens.toInt();
  Serial.println("total tokens: " + totalTokensCount);
  for (int i = 0; i <= totalTokensCount; i++) {
    String field = "field"+String(i+1);
    String dataValue = getValue(message, ';', i);
    doc[field] = dataValue.toDouble();
  }
  
}

// https://stackoverflow.com/questions/9072320/split-string-into-string-array
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}


void wifiStatusLED() {
  Serial.println("wifiStatusLED");
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}


void Networking::writeDataToThingSpeak(String data) {
    sendDataToThingSpeak(data);
}

void Networking::setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  //pinMode(wifiLED, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  //blinkLight(lightBlink);
  wifiStatusLED();
}

bool Networking::isConnected() {
  return (WiFiMulti.run() == WL_CONNECTED);
}


String preparePayload(String data) {
  String payload = "{";
  payload += "\"weight\":";
  payload += data;
  payload += ",";
  payload += "\"timestamp\":{\".sv\": \"timestamp\"}}";
  return payload;
}

// firebase write, this is called from the user of the library. 
// params: payload, endpoint
// payload is the data to be sent to the database
// endpoint is the path to the database 
//#define TEMP_ENDPOINT "/data/temps/current_temp.json"
void Networking::writeDataToFireBaseDatabase(String payload, String endpoint, bool &success) {

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);

  //client->setFingerprint(fingerprint);
  // Or, if you happy to ignore the SSL certificate, then use the following line instead:
  client->setInsecure();

  HTTPClient https;

  String databaseEndpoint = String(DATABASE_ROOT) + endpoint;

  String preparedPayload = preparePayload(payload);

  Serial.print("[HTTPS] begin...\n");
  if (https.begin(*client, databaseEndpoint)) {  // HTTPS

    https.addHeader("Content-Type", "application/json");
    Serial.print("[HTTPS] GET...\n");
    // start connection and send HTTP header
    int httpCode = https.PUT(preparedPayload);  // post vs put, post gives a child and put overwrites

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTPS] GET... code: %d\n", httpCode);


      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = https.getString();
        Serial.println(payload);
        success = true; // this should be passed in as a reference
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();

  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
}