#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <Wire.h>
#include <OneWire.h>
#include <DS18B20.h>
#include <aREST.h>

#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif

#define ONE_WIRE_BUS 2  //connected on GPIO pin 2

// Create aREST instance
aREST rest = aREST();

// The port to listen for incoming TCP connections
#define LISTEN_PORT           8080

// Create an instance of the server
WiFiServer server(LISTEN_PORT);

int displayAvgIntervalsMins[] = {1, 5, 15, 30, 60}; //Number of different displays, 1 min avg, 5 min avg, 30 min avg, 60 min avg

//use a byte and ARARY_SIZE 256 which can hold 0-255. Will increment this value using ++ operator, which will roll around to 0
#define ARRAY_SIZE 1024 // To determine array_size <= (ms of data to store) / readIntervalMs
unsigned int currentTmpIndex = 0; 
int readIntervalMs = 5000;
unsigned long lastReadTime = 0;

//how often to update the display
int updateDisplayIntervalMs = 2000;
unsigned long lastUpdateDisplay = 0;
unsigned long currentDisplay = 0; //Which thing are we displaying

struct T {
  float tempurature;
  unsigned long time;  
};

T tempuratureArray[ARRAY_SIZE];

//OneWire myWire(ONE_WIRE_BUS);        // create a oneWire instance to communicate with temperature IC
DS18B20 ds(ONE_WIRE_BUS);  // pass the oneWire reference to Dallas Temperature

//https://robotzero.one/heltec-wifi-kit-32/
// the OLED used
U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 5, /* data=*/ 4, /* reset=*/ 16);

unsigned long currentTime;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  //delay(1000);

  rest.function("tempurature", tempurature);
  rest.variable("currentTmpIndex", &currentTmpIndex);
  
  Serial.println("DS18B20 Temperature IC Test");
  Serial.println("Locating devices...");

  Serial.print("Devices: ");
  Serial.println(ds.getNumberOfDevices());

  u8g2.begin();
  
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.setCursor(0, 15);
  u8g2.print("NumDevices: ");
  u8g2.print(ds.getNumberOfDevices());
  
  u8g2.sendBuffer();
  delay(1000);
  
  u8g2.setFont(u8g2_font_chikita_tf);
  u8g2.clearBuffer();
  
  //Setup WifiManager
  const char* apName = "AP-NAME";
  const char* password = "temppassword";

  u8g2.drawStr(0,16,"Please connect & configure");
  u8g2.drawStr(0,23,"SSID:");
  u8g2.drawStr(30,23, apName);
  u8g2.drawStr(0,30,"Pass:"); 
  u8g2.drawStr(30,30, password); 

  u8g2.sendBuffer();
  
  WiFiManager wifiManager;  
  wifiManager.autoConnect(apName, password);

  u8g2.clearBuffer();
  
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.setCursor(0, 15);
  u8g2.print("IP: ");
  u8g2.print(WiFi.localIP());
  
  u8g2.sendBuffer();
  
  delay(2000);
  
  // Start the server
  server.begin();
  Serial.println("Server started");
  
  u8g2.clear();

  //Do First read
  currentTime = millis();
  tempuratureArray[currentTmpIndex] = {ds.getTempF(), currentTime};
  
  lastReadTime = currentTime;
}

void loop() {
  // Handle REST calls
  WiFiClient client = server.available();
  if (client) {
    Serial.println("REST client request");
    
    while(!client.available()){
      delay(1);
    }
    
    rest.handle(client);
  }
  
  
  currentTime = millis();
  
/*
  Serial.print("currentTime : ");
  Serial.print(currentTime);
  Serial.print(", lastReadTime : ");
  Serial.print(lastReadTime);
  Serial.print(", lastUpdateDisplay : ");
  Serial.print(lastUpdateDisplay);
  Serial.print(", tempuratureArray[currentTmpIndex-1].temp : ");
  Serial.println(tempuratureArray[currentTmpIndex-1].tempurature);
*/

  //If read interval is met then read temp and save to struct
  if (currentTime >= (lastReadTime + readIntervalMs)) {
    //TODO: Handle multiple connected sensors
    while (ds.selectNext()) {
      currentTmpIndex++;

      //If index exceeds array_size roll back to zero
      if (currentTmpIndex >= ARRAY_SIZE)
        currentTmpIndex = 0;
        
      tempuratureArray[currentTmpIndex] = {ds.getTempF(), currentTime};      
    }

    Serial.print("currentTime : ");
    Serial.print(currentTime);
    Serial.print(", currentTmpIndex : ");
    Serial.print(currentTmpIndex);
    Serial.print(", lastUpdateDisplay : ");
    Serial.print(lastUpdateDisplay);
    Serial.print(", tempuratureArray[currentTmpIndex].temp : ");
    Serial.println(tempuratureArray[currentTmpIndex].tempurature);
    
    lastReadTime += readIntervalMs; //Add the interval instead of current time since there are a few ms required for processing
  }

  //Every x interval 
  if (currentTime >= (lastUpdateDisplay + updateDisplayIntervalMs)) {
    //temporarily holds data from vals
    char charVal[10];

    int interval = displayAvgIntervalsMins[currentDisplay % (sizeof(displayAvgIntervalsMins) / sizeof(displayAvgIntervalsMins[0]))];
    
    //Check that there have been enough samples to display this average
    if (false && (currentTmpIndex * readIntervalMs) < (interval * 1000 * 60)){
      Serial.print("Not enough samples to calculate average for interval : ");
      Serial.println(interval);
      /*
      currentDisplay++;
      lastUpdateDisplay += updateDisplayIntervalMs;
      
      return;
      */
    } else {
      Serial.print("Calulating average for interval : ");
      Serial.println(interval);
      
      int i = 0;
      int idx = currentTmpIndex;
      float tempSum = 0;
      
      while (tempuratureArray[idx].time >= (currentTime - (interval * 1000 * 60))){
        tempSum += tempuratureArray[idx].tempurature;

        //Loop around
        if (idx <= 0)
          idx = ARRAY_SIZE;

        if (i >= ARRAY_SIZE) {
          Serial.println("Exceeded ARRAY_SIZE");
          break;
        }
        
        i++;
        idx--; //going backwards
      }
      
      dtostrf(tempSum/i, 5, 2, charVal); //Get the average
      
      Serial.print("*****tempSum : ");
      Serial.print(tempSum);
      Serial.print(", i : ");
      Serial.print(i);
      Serial.print(", tempSum/i : ");
      Serial.print(tempSum/i);
      Serial.print(", charVal : ");
      Serial.println(charVal);
    }

  /*  
    Serial.print("currentTmpIndex: ");
    Serial.print(currentTmpIndex);
    
    uint8_t address[8];
    ds.getAddress(address);
  
    Serial.print(" Address:");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.print(" ");
      Serial.print(address[i]);
    }
    Serial.println();
*/

    //u8g2.begin(); //handles clearing
    u8g2.clearBuffer();
    
    //u8g2.drawStr(0,16,"Tmp: ");

    //4 is mininum width, 3 is precision; float value is copied onto buff
    //dtostrf(ds.getTempF(), 4, 1, charVal);

    u8g2.setCursor(0, 11);
    u8g2.print(tempuratureArray[currentTmpIndex].tempurature);
    
    if (tempuratureArray[currentTmpIndex].tempurature > tempuratureArray[currentTmpIndex==0 ? ARRAY_SIZE-1 : currentTmpIndex-1].tempurature) { //increase
      //u8g2.setFont(open_iconic_arrow_1x);
      u8g2.print("+");
      //u8g2.setFont(u8g2_font_ncenB10_tr);
    } else if (tempuratureArray[currentTmpIndex].tempurature < tempuratureArray[currentTmpIndex==0 ? ARRAY_SIZE-1 : currentTmpIndex-1].tempurature) { //decrease
      //u8g2.setFont(open_iconic_arrow_1x);
      u8g2.print("-");
      //u8g2.setFont(u8g2_font_ncenB10_tr);
    }
      
    u8g2.print(" Temp");

    u8g2.setCursor(0, 29);
    u8g2.print(charVal);
    u8g2.print(" - ");
    u8g2.print(interval);
    u8g2.print("m Avg");

    u8g2.sendBuffer();
    
    //Increment
    currentDisplay++;
    lastUpdateDisplay += updateDisplayIntervalMs;
  }
}

int tempurature (String args) {
  Serial.print("REST function called with args : ");
  Serial.println(args);

  return currentTmpIndex;
}
