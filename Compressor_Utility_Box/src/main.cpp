#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_TCA9534.h>
#include <SparkFun_SinglePairEthernet.h>
#include <Arduino_JSON.h>

// Create Instances
TCA9534 myGPIO;
SinglePairEthernet adin1110;

// Pins
const uint8_t GPIO_COMPRESSOR_RELAY = 0;
const uint8_t GPIO_DRAIN_RELAY = 1;
const uint8_t GPIO_VENT_RELAY = 2;
const uint8_t GPIO_DRYER_RELAY = 3;
const uint8_t GPIO_SOUND_METER = 4;

// Constants
byte deviceMAC[6] = {0x00, 0xE0, 0x22, 0xFE, 0xDA, 0xC9};
byte destinationMAC[6] = {0x00, 0xE0, 0x22, 0xFE, 0xDA, 0xCA};

// Function prototypes
void error();

// Globals
JSONVar soundData;
uint32_t last_packet_rx = 0 ;

// Callback for getting sound data from the utility room box
static void rxCallback(byte * data, int dataLen, byte * senderMAC)
{
    JSONVar relayData = JSON.parse((char *)data);
    int enable = (int)relayData["enable"];
    int dryer = (int)relayData["dryer"];
    int drain = (int)relayData["drain"];
    int vent = (int)relayData["vent"];
    last_packet_rx = millis();

    myGPIO.digitalWrite(GPIO_COMPRESSOR_RELAY, enable);
    myGPIO.digitalWrite(GPIO_DRYER_RELAY, dryer);
    myGPIO.digitalWrite(GPIO_DRAIN_RELAY, drain);
    myGPIO.digitalWrite(GPIO_VENT_RELAY, vent);
}

// Setup
void setup()
{
  Serial.begin(115200);
  Serial.println("Compressor Utility Box");
  
  Wire.begin();

  // Setup GPIO Pins
  if (myGPIO.begin() == false)
  {
    Serial.println("Error - No GPIO board detected.");
    while (1);
  }
  myGPIO.pinMode(GPIO_SOUND_METER, GPIO_IN);
  myGPIO.pinMode(GPIO_VENT_RELAY, GPIO_OUT);
  myGPIO.pinMode(GPIO_COMPRESSOR_RELAY, GPIO_OUT);
  myGPIO.pinMode(GPIO_DRAIN_RELAY, GPIO_OUT);
  myGPIO.pinMode(GPIO_DRYER_RELAY, GPIO_OUT);

  // Turn all lights on during startup
  myGPIO.digitalWrite(GPIO_VENT_RELAY, LOW);
  myGPIO.digitalWrite(GPIO_COMPRESSOR_RELAY, LOW);
  myGPIO.digitalWrite(GPIO_DRAIN_RELAY, LOW);
  myGPIO.digitalWrite(GPIO_DRYER_RELAY, LOW);

  // Start and connect SPE
  if (!adin1110.begin(deviceMAC)) 
  {
    Serial.print("Failed to start SPE");
    while(1); //If we can't connect just stop here  
  }

  Serial.println("Device Configured, waiting for connection...");
  while (adin1110.getLinkStatus() != true);
  adin1110.setRxCallback(rxCallback);
  Serial.println("Connected");
}

// Main Loop
void loop() 
{
  // If we don't receive any relay status packets in 30 seconds, shut everything off
  if ((millis() - last_packet_rx) > 30000)
  {
    Serial.println("ERROR - No packets received, shutting down");
    error();
  }

  bool compressor_running = myGPIO.digitalRead(GPIO_SOUND_METER);
  soundData["soundlevel"] = compressor_running;

  if (adin1110.getLinkStatus())
  {
    String jsonString = JSON.stringify(soundData);
    adin1110.sendData( (byte *)jsonString.c_str(), strlen(jsonString.c_str()) + 1 );
    Serial.print("Sent (");   
    Serial.print(strlen(jsonString.c_str()));
    Serial.print(") bytes :\t");  
    Serial.println(jsonString.c_str());
  }

  else
  {
    Serial.println("No link present!");
  }

  delay(1000);
}

// Functions
void error()
{
  /*
   * Error state - we are in the error state where everything is shutdown and we stay here
   * until the system power is cycled. Something has gone very wrong.
   */
  myGPIO.digitalWrite(GPIO_VENT_RELAY, LOW);
  myGPIO.digitalWrite(GPIO_COMPRESSOR_RELAY, LOW);
  myGPIO.digitalWrite(GPIO_DRAIN_RELAY, LOW);
  myGPIO.digitalWrite(GPIO_DRYER_RELAY, LOW);
  while(1){}
}
