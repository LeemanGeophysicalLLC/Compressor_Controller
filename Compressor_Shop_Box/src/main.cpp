#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_TCA9534.h>
#include <SparkFun_SinglePairEthernet.h>
#include <Arduino_JSON.h>

// Create Instances
TCA9534 myGPIO;
SinglePairEthernet adin1110;

// Pins
const uint8_t GPIO_AIR_SWITCH = 0;
const uint8_t GPIO_VENT_SWITCH = 1;
const uint8_t GPIO_DRAIN_SWITCH = 2;
const uint8_t GPIO_OK_INDICATOR = 3;
const uint8_t GPIO_ERROR_INDICATOR = 4;

// Constants
const uint16_t MAX_COMPRESSOR_RUN_SECONDS = 300;
byte deviceMAC[6] = {0x00, 0xE0, 0x22, 0xFE, 0xDA, 0xC9};
byte destinationMAC[6] = {0x00, 0xE0, 0x22, 0xFE, 0xDA, 0xCA};

// Function prototypes
void readSwitches();
void readSoundLevel();
void updatePanel();
void sendStates();
void error();

// Globals
bool air_on = false;
bool drain_on = false;
bool vent_on = false;
bool system_ok = true;
uint16_t seconds_compressor_running = 0;
uint16_t seconds_fan_running = 0;
bool compressor_running = false;
int sound_level = 0;
uint32_t sound_level_last_update_ms = 0;
JSONVar relayData;

// Callback for getting sound data from the utility room box
static void rxCallback(byte * data, int dataLen, byte * senderMAC)
{
    JSONVar soundData = JSON.parse((char *)data);
    sound_level = (int)soundData["soundlevel"];
    sound_level_last_update_ms = millis();
}

// Setup
void setup()
{
  Serial.begin(115200);
  Serial.println("Compressor Shop Box");
  
  Wire.begin();

  // Setup GPIO Pins
  if (myGPIO.begin() == false)
  {
    Serial.println("Error - No GPIO board detected.");
    while (1);
  }
  myGPIO.pinMode(GPIO_VENT_SWITCH, GPIO_IN);
  myGPIO.pinMode(GPIO_AIR_SWITCH, GPIO_IN);
  myGPIO.pinMode(GPIO_DRAIN_SWITCH, GPIO_IN);
  myGPIO.pinMode(GPIO_OK_INDICATOR, GPIO_OUT);
  myGPIO.pinMode(GPIO_ERROR_INDICATOR, GPIO_OUT);

  // Turn all lights on during startup
  myGPIO.digitalWrite(GPIO_OK_INDICATOR, HIGH);
  myGPIO.digitalWrite(GPIO_ERROR_INDICATOR, HIGH);

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
  // Update the switch status
  readSwitches();

  // Read the sound level and update the running counter of how long the compressor has been
  // running. Keep tally in seconds.
  readSoundLevel();

  // Update the indicator panel
  updatePanel();

  // Send the new states to the relays in the utility box
  sendStates();

  // Drop into error if necessary
  if (system_ok == false) // The system isn't okay
  {
    error();
  }

  if ((millis() - sound_level_last_update_ms) > 30000) // If we haven't got a new sound reading
  {
    error();
  }
}

// Functions
void readSwitches()
{
  /*
   * Read the state of the panel switches and update the global booleans. Swithes go low when
   * they are in the active position and we set the boolean to true.
   */
  air_on = !myGPIO.digitalRead(GPIO_AIR_SWITCH);
  drain_on = !myGPIO.digitalRead(GPIO_DRAIN_SWITCH);
  vent_on = !myGPIO.digitalRead(GPIO_VENT_SWITCH);
}


void readSoundLevel()
{
  /*
   * Read the sound level from the utility box over SPE
   */
  static uint32_t millis_compressor_start = 0;

  // Sound level is read in the callback, so no action required here other than to
  // process the data.

  if(sound_level) // Compressor is running
  {
    if (compressor_running == false)
    {
      // Wasn't running, so this is a new start!
      millis_compressor_start = millis();
      compressor_running = true;
    } 

    seconds_compressor_running = (millis() - millis_compressor_start) / 1000;
  }

  else // Compressor is not running
  {
    compressor_running = false;
  }
}


void updatePanel()
{
  /*
   * Update the panel status lights based on the current system state. This is also where we
   * determine if the system is in an error state. 
   */

  // If the compressor has been running for over 5 minutes we are in error
  if (seconds_compressor_running >= MAX_COMPRESSOR_RUN_SECONDS)
  {
    system_ok = false;
  }

  if(system_ok)
  {
    myGPIO.digitalWrite(GPIO_OK_INDICATOR, HIGH);
    myGPIO.digitalWrite(GPIO_ERROR_INDICATOR, LOW);
  }

  else
  {
    myGPIO.digitalWrite(GPIO_OK_INDICATOR, LOW);
    myGPIO.digitalWrite(GPIO_ERROR_INDICATOR, HIGH);
  }
}


void sendStates()
{
  /*
   * Send the relay states to the utility box over SPE.
   */ 

  // If the air switch is on, we make sure the compressor enable and dryer relays are on
  if (air_on)
  {
    relayData["enable"] = true;
    relayData["dryer"] = true;
  }
  else
  {
    relayData["enable"] = false;
    relayData["dryer"] = false;
  }

  // If the drain switch is on, turn on the drain relay
  if (drain_on)
  {
    relayData["drain"] = true;
  }
  else
  {
    relayData["drain"] = false;
  }

  // If the compressor is running vent the room
  if (compressor_running)
  {
    relayData["vent"] = true;
  }
  else
  {
    relayData["vent"] = false;
  }

  if (adin1110.getLinkStatus())
  {
    String jsonString = JSON.stringify(relayData);
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
}


void error()
{
  /*
   * Error state - we are in the error state where everything is shutdown and we stay here
   * until the system power is cycled. Something has gone very wrong.
   */

  // Spin forever- utility room box will shut off all relays if it does not get a new
  // packet for awhile
  while(1){}
}
