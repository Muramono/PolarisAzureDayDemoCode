#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiType.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>

#include <AzureIoTHub.h>
#if defined(IOT_CONFIG_MQTT)
    #include <AzureIoTProtocol_MQTT.h>
#elif defined(IOT_CONFIG_HTTP)
    #include <AzureIoTProtocol_HTTP.h>
#endif

#include "sample.h"
#include "esp8266/sample_init.h"

#include <AzureIoTUtility.h>
#include <AzureIoTHub.h>
#include <AzureIoTProtocol_MQTT.h>

#include <DHT.h>

//String ssid                           = "iPhone";        // your network SSID (name)
//String pass                           = "DS4Chase";  // your network password (use for WPA, or use as key for WEP)
//String ssid                         = "Guests";        // your network SSID (name)
//String pass                         = "the way out since 1954";  // your network password (use for WPA, or use as key for WEP)
String ssid                           = "ARRIS-6D12";
String pass                           = "2W4335100270";
static const char* connectionString = "HostName=TemporaryDevHub.azure-devices.net;DeviceId=HeatSensor1;SharedAccessKey=cOl5JqrV0GrvhlDv2Kzkfq3kLDkS1qJA/weEpYATVPY=";
int lastHour; //Records what the last hour was in order to regulate the speed of messages sent
int timezone = 17; //Records what timezone the device is taking data from
int deviceDst = 1; //Records what daylight savings time the device is taking data from
int deviceMaxValue = 0;
int deviceMinValue = 0;
String deviceLocation = " ";

#define DHTPIN 2                                   // what digital pin we're connected to
#define DHTTYPE DHT22                               // DHT11 or DHT22

IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle;
IOTHUB_CLIENT_STATUS status;

WiFiClientSecure espClient;

DHT dht(DHTPIN, DHTTYPE);

void initWifi() {
    if (WiFi.status() != WL_CONNECTED) 
    {
        WiFi.stopSmartConfig();
        WiFi.enableAP(false);

        // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
        WiFi.begin(ssid.c_str(), pass.c_str());
    
        Serial.print("Waiting for Wifi connection.");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            delay(500);
        }
    
        Serial.println("Connected to wifi");

        initTime();
        initIoTHub();
    }
}

void initTime() {
    time_t epochTime;
    int tempHour = lastHour;
    if(tempHour != currentHour()) {
      configTime(timezone * 3600, deviceDst * 3600, "time1.google.com");
    }

    while (true) {
        epochTime = time(NULL);

        if (epochTime == 0) {
            Serial.println("Fetching NTP epoch time failed! Waiting 2 seconds to retry.");
            delay(2000);
        } else {
            Serial.print("Fetched NTP epoch time is: ");
            Serial.println(epochTime);
            break;
        }
    }
}

static void sendMessage(const char* message)
{
    static unsigned int messageTrackingId;
    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(message);

    if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, messageHandle, sendMessageCallback, (void*)(uintptr_t)messageTrackingId) != IOTHUB_CLIENT_OK)
    {
        Serial.println(" ERROR: Failed to hand over the message to IoTHubClient");
    }
    else
    {
      (void)printf(" Message Id: %u Sent.\r\n", messageTrackingId);
    }

    IoTHubMessage_Destroy(messageHandle);
    messageTrackingId++;
}

void sendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    unsigned int messageTrackingId = (unsigned int)(uintptr_t)userContextCallback;

    (void)printf(" Message Id: %u Received.\r\n", messageTrackingId);
}

static IOTHUBMESSAGE_DISPOSITION_RESULT IoTHubMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* userContextCallback)
{
    IOTHUBMESSAGE_DISPOSITION_RESULT result = IOTHUBMESSAGE_ACCEPTED;
    
    const char* messageId = "UNKNOWN";      // in case there is not a messageId associated with the message -- not required
    messageId = IoTHubMessage_GetMessageId(message);

    const unsigned char* buffer;
    size_t size;
    if (IoTHubMessage_GetByteArray(message, &buffer, &size) != IOTHUB_MESSAGE_OK)
    {
        Serial.println(" Error: Unable to IoTHubMessage_GetByteArray");
        result = IOTHUBMESSAGE_ABANDONED;
    }
    else
    {
        char* tempBuffer = (char*)malloc(size + 1);
        if (tempBuffer == NULL)
        {
            Serial.println(" Error: failed to malloc");
            result = IOTHUBMESSAGE_ABANDONED;
        }
        else
        {
            result = IOTHUBMESSAGE_ACCEPTED;
            (void)memcpy(tempBuffer, buffer, size);
            
            String messageStringFull((char*)tempBuffer);
            String messageString = "UNKNOWN";
            messageString = messageStringFull.substring(0,size);

/*            if (messageString.startsWith("OTA")) {
                  String fullURL = messageString.substring(messageString.indexOf("://") - 4);;
                  // t_httpUpdate_return OTAStatus = OTA.update(fullURL.c_str());
                  // if we do OTA, then we never return the IOTHUBMESSAGE_ACCEPTED and we have issues
            }*/
            
            String messageProperties = "";
            MAP_HANDLE mapProperties = IoTHubMessage_Properties(message);
            if (mapProperties != NULL)
            {
            const char*const* keys;
            const char*const* values;
            size_t propertyCount = 0;
            if (Map_GetInternals(mapProperties, &keys, &values, &propertyCount) == MAP_OK)
                {
                if (propertyCount > 0)
                    {
                    size_t index;
                    for (index = 0; index < propertyCount; index++)
                        {
                            messageProperties += keys[index];
                            messageProperties += "=";
                            messageProperties += values[index];
                            messageProperties += ",";
                        }
                    }
                }
            }

            Serial.print(" Message Id: ");
            Serial.print(messageId);
            Serial.print(" Received. Message: \"");
            Serial.print(messageString);
            Serial.print("\", Properties: \"");
            Serial.print(messageProperties);
            Serial.println("\"");
        }
        free(tempBuffer);
    }
    return result;
}

void initIoTHub() {
  iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(connectionString, MQTT_Protocol);
  if (iotHubClientHandle == NULL)
  {
      (void)printf("ERROR: Failed on IoTHubClient_LL_Create\r\n");
  } else {
    IoTHubClient_LL_SetMessageCallback(iotHubClientHandle, IoTHubMessageCallback, NULL);
  }
}

void LEDOn() {
  digitalWrite(LED_BUILTIN, LOW);
}

void LEDOff() {
  digitalWrite(LED_BUILTIN, HIGH);
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  initWifi();
  configTime(timezone * 3600, deviceDst * 3600, "time1.google.com");
  Serial.print("MAC: ");
  Serial.print(WiFi.macAddress());

  pinMode(LED_BUILTIN, OUTPUT);
  LEDOff();
}

//For reference we are using the C++ time struct tm to get the list of values which we are using for time
int currentSecond() {
  //Finding the current hour
  time_t now;
  struct tm * timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  //Seconds will sometimes go above 60 to accomodate leap seconds in certain systems
  return(timeinfo->tm_sec); //Not adding a value to seconds because it starts at 0
}

int currentMinute() {
  //Finding the current hour
  time_t now;
  struct tm * timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  return(timeinfo->tm_min); //Not adding a value to miniute because it starts at 0
}

int currentHour() {
  //Finding the current hour
  time_t now;
  struct tm * timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  //Add Num Check Here
  return(timeinfo->tm_hour+1); //Adding a value to hour because the hour array begins with a value of 0
  lastHour = (timeinfo->tm_hour+1); //Setting the last recorded hour
}

int currentDay() {
  //Finding the current hour
  time_t now;
  struct tm * timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  return(timeinfo->tm_mday - 1); //Not adding a value to day because the arrray begins with a value of 1
}

int currentMonth() {
  //Finding the current hour
  time_t now;
  struct tm * timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  return(timeinfo->tm_mon+1); //Adding a value to month because the month array begins with a value of 0
}

int currentYear() {
  //Finding the current hour
  time_t now;
  struct tm * timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  return(timeinfo->tm_year+1900); //Adding a value to year because the year array begins with a value of 1900
}

void loop() {
  initWifi();         // always checking the WiFi connection
  Serial.print("MAC: ");
  int MinCheck = currentMinute();
/*  if(MinCheck == 30){ //Running this will update with Twin data, however currently running and displaying twin data ontop of current IOTHub messaging code seems to run into power issues
    sample_run();
    } */ 
  Serial.print(WiFi.macAddress());
  Serial.print("IP Address: ");
  Serial.print(WiFi.localIP());

  LEDOn();

  int loopcounter = 0;
  // we will process every message in the Hub
  while (((IoTHubClient_LL_GetSendStatus(iotHubClientHandle, &status) == IOTHUB_CLIENT_OK) && (status == IOTHUB_CLIENT_SEND_STATUS_BUSY)) && loopcounter < 10)
  {
      LEDOn();
      Serial.println(" Hello I am running through the IOTHUB loop: " + status);
      delay(1000);
      LEDOff();
      
      IoTHubClient_LL_DoWork(iotHubClientHandle); //Problem child
      ThreadAPI_Sleep(1000);
      loopcounter++;
  }
  
  time_t now = time(nullptr);
  
  if (1 == 1) { //Sending JSON Message
      String  JSONMessage = "{\'temperature\':";
          JSONMessage += dht.readTemperature(true); //Sending the Temperature
          JSONMessage += ",";
          JSONMessage += "\'heatIndex\':";
          JSONMessage += dht.computeHeatIndex(dht.readTemperature(true),dht.readHumidity(true)); //Sending the Heat Index
          JSONMessage += ",";
          JSONMessage += "\'humidity\':";
          JSONMessage += dht.readHumidity(true); //Sending the Humidity
          JSONMessage += ",";
          JSONMessage += "\'csecond\': ";
          JSONMessage += currentSecond(); //Sending the Second
          JSONMessage += ",";
          JSONMessage += "\'cminute\': ";
          JSONMessage += currentMinute(); //Sending the Minute
          JSONMessage += ",";
          JSONMessage += "\'chour\': ";
          JSONMessage += currentHour(); //Sending the Hour
          JSONMessage += ",";
          JSONMessage += "\"cday\": ";
          JSONMessage += currentDay(); //Sending the Day
          JSONMessage += ",";
          JSONMessage += "\"cmonth\": ";
          JSONMessage += currentMonth(); //Sending the Month
          JSONMessage += ",";
          JSONMessage += "\"cyear\": ";
          JSONMessage += currentYear(); //Sending the Year
          JSONMessage += "}";
       sendMessage(JSONMessage.c_str());  //Sending the JSON Message to the IOTHub
  
  Serial.println(JSONMessage); //Printing JSON Message to the Serial Monitor
  }

Serial.println("Loop Complete"); //Notification that the Loop has been completed
  /*LEDOff();
  delay(2500);
  LEDOn();
  delay(2500);*/
}



