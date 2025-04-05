#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_TCA9534.h>

// Create Instances
TCA9534 myGPIO1;
TCA9534 myGPIO2;

// Pins on GPIO 1
const uint8_t GPIO_AIR_SWITCH = 2;
const uint8_t GPIO_DRAIN_SWITCH = 3;
const uint8_t GPIO_VENT_SWITCH = 7;
const uint8_t GPIO_OK_LED = 6;
const uint8_t GPIO_ERROR_LED = 5;
const uint8_t GPIO_SOUND_METER = 4;

// Pins on GPIO 2
const uint8_t GPIO_COMPRESSOR_RELAY = 0;
const uint8_t GPIO_DRAIN_RELAY = 1;
const uint8_t GPIO_VENT_RELAY = 2;
const uint8_t GPIO_DRYER_RELAY = 3;

// Globals
uint32_t fan_cooldown_timer_sec = 0;
uint32_t compressor_timer_sec = 0;
bool last_compressor_on = false;

void setup()
{
  Serial.begin(115200);
  Serial.println("Compressor Control Box");
  
  Wire.begin();
  delay(500);
  Serial.println("Starting GPIO1");
  // Setup GPIO Pins
  if (myGPIO1.begin(Wire, 0x27) == false)
  {
    Serial.println("Error - No GPIO 1 board detected.");
    while (1);
  }
  delay(500);
  Serial.println("Starting GPIO2");
  if (myGPIO2.begin(Wire, 0x26) == false)
  {
    Serial.println("Error - No GPIO 2 board detected.");
    while (1);
  }
  delay(500);
  Serial.println("Setting pin modes");

  myGPIO1.pinMode(GPIO_OK_LED, GPIO_OUT);
  myGPIO1.pinMode(GPIO_ERROR_LED, GPIO_OUT);
  
  /*
  while(1)
  {
    Serial.println("Lights");
    myGPIO1.digitalWrite(GPIO_OK_LED, HIGH);
    myGPIO1.digitalWrite(GPIO_ERROR_LED, LOW);
    delay(500);
    myGPIO1.digitalWrite(GPIO_OK_LED, LOW);
    myGPIO1.digitalWrite(GPIO_ERROR_LED, HIGH);
    delay(500);
  }
  */
  

  myGPIO1.pinMode(GPIO_AIR_SWITCH, GPIO_IN);
  myGPIO1.pinMode(GPIO_DRAIN_SWITCH, GPIO_IN);
  myGPIO1.pinMode(GPIO_VENT_SWITCH, GPIO_IN);
  myGPIO1.pinMode(GPIO_SOUND_METER, GPIO_IN);
  myGPIO1.pinMode(GPIO_SOUND_METER, GPIO_IN);

  myGPIO2.pinMode(GPIO_VENT_RELAY, GPIO_OUT);
  myGPIO2.pinMode(GPIO_COMPRESSOR_RELAY, GPIO_OUT);
  myGPIO2.pinMode(GPIO_DRAIN_RELAY, GPIO_OUT);
  myGPIO2.pinMode(GPIO_DRYER_RELAY, GPIO_OUT);

  Serial.println("Setting pin states");
  myGPIO1.digitalWrite(GPIO_OK_LED, HIGH);
  myGPIO1.digitalWrite(GPIO_ERROR_LED, HIGH);
  myGPIO2.digitalWrite(GPIO_VENT_RELAY, LOW);
  myGPIO2.digitalWrite(GPIO_COMPRESSOR_RELAY, LOW);
  myGPIO2.digitalWrite(GPIO_DRAIN_RELAY, LOW);
  myGPIO2.digitalWrite(GPIO_DRYER_RELAY, LOW);

  Serial.println("Wait for lights");
  delay(1000);
  myGPIO1.digitalWrite(GPIO_OK_LED, LOW);
  myGPIO1.digitalWrite(GPIO_ERROR_LED, LOW);
  Serial.println("Setup complete");
}

void error()
{
  // Error - shut it all down and wait for a cycling of the air switch to off to reset the
  // system.
  myGPIO2.digitalWrite(GPIO_VENT_RELAY, LOW);
  myGPIO2.digitalWrite(GPIO_COMPRESSOR_RELAY, LOW);
  myGPIO2.digitalWrite(GPIO_DRAIN_RELAY, LOW);
  myGPIO2.digitalWrite(GPIO_DRYER_RELAY, LOW);
  myGPIO1.digitalWrite(GPIO_OK_LED, LOW);
  myGPIO1.digitalWrite(GPIO_ERROR_LED, HIGH);
  while(myGPIO1.digitalRead(GPIO_AIR_SWITCH) == LOW)
  {}
}

void loop()
{
  Serial.println("----------------------------------------------------------------------");
  // Start with the idea that all is okay!
  myGPIO1.digitalWrite(GPIO_OK_LED, HIGH);
  myGPIO1.digitalWrite(GPIO_ERROR_LED, LOW);

  // First check the air switch - if it is on turn the dryer and air relays on, if it is off
  // turn them off. Nothing difficult in this step.
  Serial.print("AIR: ");
  if (!myGPIO1.digitalRead(GPIO_AIR_SWITCH))
  {
    Serial.println("ON");
    myGPIO2.digitalWrite(GPIO_COMPRESSOR_RELAY, HIGH);
    myGPIO2.digitalWrite(GPIO_DRYER_RELAY, HIGH);
  }
  else
  {
    Serial.println("OFF");
    myGPIO2.digitalWrite(GPIO_COMPRESSOR_RELAY, LOW);
    myGPIO2.digitalWrite(GPIO_DRYER_RELAY, LOW);
  }

  // Next check the drain switch - make the drain realy on/off based on the switch.
  Serial.print("DRAIN: ");
  if (!myGPIO1.digitalRead(GPIO_DRAIN_SWITCH))
  {
    Serial.println("ON");
    myGPIO2.digitalWrite(GPIO_DRAIN_RELAY, HIGH);
  }
  else
  {
    Serial.println("OFF");
    myGPIO2.digitalWrite(GPIO_DRAIN_RELAY, LOW);
  }

  // Check the vent switch and record its state
  bool vent_on = !myGPIO1.digitalRead(GPIO_VENT_SWITCH);
  Serial.print("FAN: ");
  if (vent_on)
  {
    Serial.println("ON");
  }
  else
  {
    Serial.println("OFF");
  }
  // Now check the sound meter to see if the compressor is on. Also handle seeing if we changed
  // state since the last loop.
  bool compressor_on = !myGPIO1.digitalRead(GPIO_SOUND_METER);
  if (compressor_on != last_compressor_on)
  {
    if (compressor_on == false)
    {
      // We were on, but not we are not now, so set the fan_cooldown_start_ms
      fan_cooldown_timer_sec = 300;
    }
    else
    {
      // We were off, but now we are on
      compressor_timer_sec = 0;
    }
  }
  last_compressor_on = compressor_on;

  // Ok here is the "complex" logic - if the compressor is running we make sure that we have
  // not been running for more than 5 minutes. If so, something is wrong and we shutdown
  // for safety and loop forever. If the vent fan is enabled, we turn it on and run it for
  // 5 minutes after the compressor has stopped.
  if (compressor_on)
  {
    Serial.print("Compressor running for ");
    Serial.print(compressor_timer_sec);
    Serial.println(" seconds");
    compressor_timer_sec += 1;
    // Check for the safety limit
    if (compressor_timer_sec > 300)
    {
      error();
    }

    // Check if we need to be running the fan (compressor is known to be on here)
    myGPIO2.digitalWrite(GPIO_VENT_RELAY, vent_on);
  }

  // Here we handle the case of the compressor is off, but we need to run the fan in cooldown
  // mode (there is time left on the cooldown timer)
  else
  {
  if ((fan_cooldown_timer_sec > 0) && vent_on)
    {
      Serial.print("Vent fan running for ");
      Serial.print(fan_cooldown_timer_sec);
      Serial.println(" seconds");
      myGPIO2.digitalWrite(GPIO_VENT_RELAY, HIGH);
      fan_cooldown_timer_sec -= 1;
    }
    else
    {
      myGPIO2.digitalWrite(GPIO_VENT_RELAY, LOW);
    }
  }

  // Run loop once per second - timing isn't precise, but we don't care here!
  delay(1000);
}
