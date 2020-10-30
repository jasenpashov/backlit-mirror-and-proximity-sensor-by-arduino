#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_Sensor.h>
#include <DHT.h> //DHT sensor library
#include <DNSServer.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)

#include <ESPAsyncTCP.h>
#endif
#include "ESPAsyncWebServer.h"

extern "C" {
  #include <osapi.h>
  #include <os_type.h>
}

#include "config.h"

const char* ssid      = "";
const char* password  = "";
const char* server_my = "";


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

#define ledPin     D1
#define pirPin     D2
#define DHTPIN     D3
#define TRIGGERPIN D5
#define ECHOPIN    D6

DHT dht(DHTPIN,DHT11,15);
IPAddress ip;

int mysql_channel = 17; // Mirror in the hallway
int send_data = 0;
int count_loop = 0;
long intervalTransmitWeather = 0;
long intervalStopLamp = 0;
float t_avg = 0; 
float h_avg = 0; 
float distance = 0;
boolean isDetected = false;
int autoPower = 1;
int OnOff = 0;
int targetDistance = 40;
String log_startTime;
String log_endTime;
float log_distance = 0;

AsyncWebServer server(80);
const char* PARAM_MESSAGE = "message";

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(9600);
  delay(10);
  dht.begin();
  pinMode(ledPin, OUTPUT);
  pinMode(pirPin, INPUT);
  pinMode(TRIGGERPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed!");
    return;
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  ip = WiFi.localIP();
//------------ server ----------------------------
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->print("<!DOCTYPE html><html><head><title>Mirror in the hallway</title>");
      response->print("<link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css' integrity='sha384-JcKb8q3iqJ61gNV9KGb8thSsNjpSL0n8PARn9HuZOnIxN0hoP+VmmDGMN5t9UJ0Z' crossorigin='anonymous'>");
      response->print("<link rel='stylesheet' href='https://nil.bratcheda.org/iot/css/esp8266.css'>");
      response->print("</head><body id='Mirror-hallway'>");
      response->print("<script src='https://code.jquery.com/jquery-3.5.1.slim.min.js' integrity='sha384-DfXdz2htPH0lsSSs5nCTpuj/zy4C+OGpamoFVy38MVBnE+IbbVYUew+OrCXaRkfj' crossorigin='anonymous'></script>");
      response->print("<script src='https://cdn.jsdelivr.net/npm/popper.js@1.16.1/dist/umd/popper.min.js' integrity='sha384-9/reFTGAW83EW2RDu2S0VKaIzap3H66lZH81PoYlFhbGU+6BZp6G7niu735Sk7lN' crossorigin='anonymous'></script>");
      response->print("<script src='https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/js/bootstrap.min.js' integrity='sha384-B4gt1jrGC7Jh4AgTPSdUtOBvfO8shuf57BaghqFfPlYxofvL8/KUEfYiJOMMV+rV' crossorigin='anonymous'></script>");
      response->print("<H1>Mirror in the hallway</H1>");
      response->print("<div class='container-fluid'>");
      response->printf("<p>You were trying to reach: http://%s%s</p>", request->host().c_str(), request->url().c_str());
      response->print(getState().c_str());
      response->print("<hr>");
      response->print("<p><a class='btn btn-outline-secondary' href='temperature'>temperature: ");
      response->print(getTemperature().c_str());
      response->print("</a></p>");
      response->print("<p><a class='btn btn-outline-secondary' href='humidity'>humidity: ");
      response->print(getHumidity().c_str());
      response->print("</a></p>");
      response->printf("<p><a class='btn btn-outline-secondary' href='set?distans=40'>set default distance: 40cm</a></p>");
      response->print("<p><a class='btn btn-outline-secondary' href='sendWeather '>send Weather to my server</a></p>");
      response->print("<p><a class='btn btn-outline-secondary' href='mode?power=on'>On</a></p>");
      response->print("<p><a class='btn btn-outline-secondary' href='mode?power=off'>Off</a></p>");
      response->print("<p><a class='btn btn-outline-secondary' href='mode?power=auto'>Auto</a></p>");
      response->print("</div>");
      response->print("</body></html>");
      request->send(response);
    });
    
    server.on("/sendWeather", HTTP_GET, [](AsyncWebServerRequest *request){
        send_data =1;
        request->send(200, "text/plain", "send weather");
    });

    server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/plain", getTemperature().c_str());
    });
    
    server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain", getHumidity().c_str());
    });

    // Send a GET request to <IP>/set?distans=<message>
    server.on("/set", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String message;
        if (request->hasParam("distans")) {
            message = request->getParam("distans")->value();
            targetDistance = message.toInt();
        } else {
            message = "No distans sent";
        }
        request->send(200, "text/plain", "set distans: " + message);
        
    });

        // Send a GET request to <IP>/mode?message=<message>
    server.on("/mode", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String message;
        if (request->hasParam("power")) {
            message = request->getParam("power")->value();
        } else {
            message = "No mode sent";
        }
        request->send(200, "text/plain", "set mode: " + message);
        //setStart setStop  setAuto
        if(message=="on"){
          setStart();
        }
        if(message=="off"){
          setStop();
        }
        if(message=="auto"){
          setAuto();
        }
    });


    

    // Send a GET request to <IP>/get?message=<message>
    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String message;
        if (request->hasParam(PARAM_MESSAGE)) {
            message = request->getParam(PARAM_MESSAGE)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, GET: " + message);
    });

    // Send a POST request to <IP>/post with a form field message set to <message>
    server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
        String message;
        if (request->hasParam(PARAM_MESSAGE, true)) {
            message = request->getParam(PARAM_MESSAGE, true)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, POST: " + message);
    });

    server.onNotFound(notFound);
    server.begin();
//------------ server ----------------------------
  intervalTransmitWeather = calcTimeStop(millis(), 20);
  timeClient.begin(); 
}

void loop()
{ 
  long currentMillis = millis();
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  /*
  if(digitalRead(pirPin)==HIGH) {
    Serial.println("Detected !!!");
    isDetected= true;
  }else{
    isDetected= false;
  }
*/
distance = getDistance();
if (distance <= targetDistance) {
    Serial.println("Detected !!!");
    isDetected= true;
    log_distance = distance;
}else{
    isDetected= false;
}

//Serial.print("Distance in cm : "); // use: (position X: 0-15, position Y: 0-1, "Message you want to print")
//Serial.println(distance);

  if (autoPower==1) {
    if (isDetected) {
        digitalWrite(ledPin, HIGH);
        if(log_startTime =="" ){
          log_startTime = getMayTime();
          Serial.println(log_startTime);
        }
        intervalStopLamp= calcTimeStop(currentMillis, 1);
    } else {
        if(currentMillis >= intervalStopLamp){
          digitalWrite(ledPin, LOW);
          if(log_startTime!=""){
            sendInput();
          }
        }
    }
  }

/*
Serial.print("Temperature: ");
Serial.println(t);
Serial.print("Degrees Celcius Humidity: ");
Serial.println(h);
Serial.println("");
Serial.print(count_loop);
Serial.println("");
*/  
    t_avg = t_avg + t;
    h_avg = h_avg + h;
    count_loop++;
  
    if( currentMillis >= intervalTransmitWeather || send_data == 1){
      intervalTransmitWeather = calcTimeStop(currentMillis, 20);
      Serial.println("sendWeather");
      Serial.print(intervalTransmitWeather);
      Serial.print("||");
      Serial.print(currentMillis);
      t_avg   = t_avg/count_loop;
      h_avg   = h_avg/count_loop;
      sendWeather(t_avg,h_avg);
      count_loop = 0;
      t_avg      = 0;
      h_avg      = 0;
      send_data  = 0;
    }
 
  delay(10);
}//loop


float getDistance() {
  long dist;
  long duration;
  digitalWrite(TRIGGERPIN, LOW);  
  delayMicroseconds(3); 
  
  digitalWrite(TRIGGERPIN, HIGH);
  delayMicroseconds(12); 
  
  digitalWrite(TRIGGERPIN, LOW);
  duration = pulseIn(ECHOPIN, HIGH);
  dist = ((duration/2) / 29.1);
  return dist;
}


int calcTimeStop(long currentMillis, int periodMinute){
    const unsigned long oneSecond = 1000;  //the value is a number of milliseconds, ie 1 second
    long rz = currentMillis+(oneSecond*(periodMinute*60));
    return rz;
}

String getMayTime(){
  timeClient.update();
  //Serial.println(timeClient.getFormattedTime());
  return timeClient.getFormattedDate();
}


void setStart() {
 Serial.println("START setStart");
 autoPower = 0;
 OnOff = 1;
 digitalWrite(ledPin, HIGH);
}

void setStop() {
  Serial.println("Stop setStop");
  autoPower = 0;
  OnOff = 0;
  digitalWrite(ledPin, LOW);
}

void setAuto(){
  Serial.println("Auto setAuto");
  autoPower = 1;
}

String getTemperature() {
  float temperature = dht.readTemperature();
  //Serial.println(temperature);
  return String(temperature);
}

String getHumidity() {
  float humidity = dht.readHumidity();
  //Serial.println(humidity);
  return String(humidity);
}

String getState() {
  String mess ="";
        mess += "<p>Temperature: ";
        mess += String(t_avg/count_loop); 
        mess += "</p>";
        mess += "<p>Humidity: ";
        mess += String(h_avg/count_loop); 
        mess += "</p>";
        mess += "<p>Count loop: ";
        mess += String(count_loop); 
        mess += "</p>";
        mess += "<p>Auto Power: ";
        mess += String(autoPower);
        mess += "</p>";
        mess += "<p>Distance: ";
        mess += String(distance);
        mess += "cm</p>";
        mess += "<p>Target distans: ";
        mess += String(targetDistance);
        mess += "cm</p>";
        mess += "<p>Last distance activated: ";
        mess += String(log_distance);
        mess += "cm</p>";
        mess += "<p>Power lamp: ";
        mess += String(digitalRead(ledPin));
        mess += "</p>";
  return String(mess);
}
  


int sendInput(){  
  WiFiClient client;
  if (client.connect(server_my,80)) {
    
    log_endTime = getMayTime();
    String postStr = "ultrasonic=2";
    postStr +="&ip=";
    postStr += String(ip.toString());
    postStr +="&log_distance=";
    postStr += String(log_distance);
    postStr +="&log_startTime=";
    postStr += String(log_startTime);
    postStr +="&log_endTime=";
    postStr += String(log_endTime);
    Serial.println(postStr);

    client.print(String("GET /iot/update_iot.php?"+postStr) + " HTTP/1.1\r\n" +
                "Host: " + server_my + "\r\n" + 
                "Connection: close\r\n\r\n");
    delay(10);
  }
    log_startTime = ""; 
    log_endTime = "";
    client.stop();
    return true;
}

void sendWeather(float t,float h){
  WiFiClient client;
  
  if (client.connect(server_my,80)) {
      Serial.println("POST: ");
      Serial.print("Temperature: ");
      Serial.print(t);
      Serial.println(" degrees Celcius Humidity: ");
      Serial.print(h);
      Serial.println("% send to Local");

      String postStr = "apiKey";
      postStr +="&field1=";
      postStr += String(t);
      postStr +="&field2=";
      postStr += String(h);
      postStr +="&field_measure1=1";
      postStr +="&field_measure2=2";
      postStr +="&channel=";
      postStr += String(mysql_channel);
      postStr +="&ip=";
      postStr += String(ip.toString());
      Serial.println(postStr);

      client.print(String("GET /iot/update_iot.php?"+postStr) + " HTTP/1.1\r\n" +
                   "Host: " + server_my + "\r\n" + 
                   "Connection: close\r\n\r\n");
      delay(10);
  }
  client.stop();
}