/**
 * Nerdforge code base from:
 * https://github.com/hansjny/Natural-Nerd/tree/master/SoundReactive2
 * GIT nr: 20a0e52 on Jun 2
 * 
 */

#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Button2.h>

#include "reactive_common.h"

// microphone read data from
#define READ_PIN A0

// local led to indicate the remote brightness
#define CTRL_LED_PIN D1

// rotary encoder with button
#define ROTARY_BTN D5
#define ROTARY_CLK D6
#define ROTARY_DT D7
// number of increments per step
#define ROTARY_STEP 10

#define HEARTBEAT_PORT 7171
#define DATA_PORT 7001
#define NUMBER_OF_CLIENTS 1

#define NETWORK_ADDR IPAddress(192,168,200,1)
#define NETWORK_ADDR_BROADCAST IPAddress(192,168,200,255)
#define SUBNET_MASK IPAddress(255,255,255,0)

const int checkDelay = 5000;
const int numOpModes = 4;

unsigned long lastChecked;
// rotary count only in 0-255 range => used to set the strip brightness!
volatile uint8_t rtrCounter; 
volatile uint8_t rtrPrevCounter;
uint8_t rtrCrtStateCLK;
uint8_t rtrLastStateCLK;


// Holds the current button state.
volatile int state;

// Holds the last time debounce was evaluated (in millis).
volatile long lastDebounceTime = 0;

// The delay threshold for debounce checking.
const int debounceDelay = 100;


Button2 btn = Button2(ROTARY_BTN);
WiFiUDP UDP;

struct led_command
{
  uint8_t opmode;
  uint32_t data;
};

bool heartbeats[NUMBER_OF_CLIENTS];

static int opMode = 1;

// function prototypes
void sendLedData(uint32_t data, uint8_t op_mode);
void waitForConnections();
void resetHeartBeats();
void readHeartBeat();
bool checkHeartBeats();


void handleRotaryThings();


// define this symbol for debugging messages on Serial
#define EPI_DEBUG
#ifdef EPI_DEBUG
  #define DEBUG_BEGIN   Serial.begin(115200);
  #define DEBUG_PRINTLN(msg) Serial.println(msg);
  #define DEBUG_PRINT(msg) Serial.print(msg);
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_BEGIN   
  #define DEBUG_PRINTLN(msg) 
  #define DEBUG_PRINT(msg) 
  #define DEBUG_PRINTF(...) 
#endif


void setup()
{
  pinMode(READ_PIN, INPUT);
  pinMode(ROTARY_CLK, INPUT);
  pinMode(ROTARY_DT, INPUT);
  pinMode(ROTARY_BTN, INPUT_PULLUP);

  pinMode(CTRL_LED_PIN, OUTPUT);

  /* WiFi Part */
  DEBUG_BEGIN;
  delay(500);
  DEBUG_PRINTLN();
  DEBUG_PRINT("Setting soft-AP ... ");
  
  //WiFi.persistent(false);
  // Configure the AP with the network mask and IP class

  WiFi.mode(WIFI_AP);
  // hide the SSID name, nobody should know about this... :-)
  WiFi.softAP("sound_reactive", "123456789", 79, 1);
  WiFi.softAPConfig(NETWORK_ADDR, NETWORK_ADDR, SUBNET_MASK);  
  WiFi.begin();
  DEBUG_PRINT("Soft-AP IP address = ");
  DEBUG_PRINTLN(WiFi.softAPIP());

  attachInterrupt(digitalPinToInterrupt(ROTARY_BTN), clicked, CHANGE);
  //btn.setClickHandler(clicked);
  //btn.setDoubleClickHandler(doubleClicked);

  UDP.begin(HEARTBEAT_PORT);
  
  resetHeartBeats();
  waitForConnections();
  lastChecked = millis();
  // initial rotary state
  rtrLastStateCLK = digitalRead(ROTARY_CLK);
  // both counters are the same initially
  rtrCounter = 10;
  rtrPrevCounter = 10;
}


uint32_t maxv;
void loop()
{
  uint32_t analogRaw;
  //btn.loop();
  state = HIGH;
  handleRotaryThings();

  // dim the local control LED according to the rotary encoder position.
  // only, when the current position changed. It will be turned off,
  // when the value will be submitted to the sattelites
  if (rtrCounter != rtrPrevCounter) {
    int ledval = map(rtrCounter, 0, 255, 0, 1023);
    analogWrite(CTRL_LED_PIN, ledval);
  }
  
  

  if (millis() - lastChecked > checkDelay)
  {
    if (!checkHeartBeats())
    {
      waitForConnections();
    }
    lastChecked = millis();
  }

  switch (opMode)
  {
  case 1:
    analogRaw = analogRead(READ_PIN);
    if (analogRaw <= 3)
      break;
      if (analogRaw > maxv) {
        maxv = analogRaw;
        DEBUG_PRINTLN(maxv);        
      }
    
    sendLedData(analogRaw, opMode);
    break;
  case 2:
    sendLedData(0, opMode);
    delay(10);
    break;
  case 3:
    sendLedData(0, opMode);
    delay(10);
    break;
  case 4:
    sendLedData(rtrCounter, opMode);
    delay(100);
    // turn off the local ctrl led, to preserve power
    analogWrite(CTRL_LED_PIN, 0);
    opMode = 1;
    break;
  }
  delay(4);
}

void sendLedData(uint32_t data, uint8_t op_mode)
{
  struct led_command send_data;
  send_data.opmode = op_mode;
  send_data.data = data;
  //DEBUG_PRINTLN("sending data");
  // just send a broadcast data   
  UDP.beginPacket(NETWORK_ADDR_BROADCAST, DATA_PORT);
  UDP.write((char *)&send_data, sizeof(struct led_command));
  UDP.endPacket();
}

void waitForConnections()
{
  while (true)
  {
    readHeartBeat();
    if (checkHeartBeats())
    {
      return;
    }
    delay(checkDelay);
    resetHeartBeats();
  }
}

void resetHeartBeats()
{
  for (int i = 0; i < NUMBER_OF_CLIENTS; i++)
  {
    heartbeats[i] = false;
  }
}

void readHeartBeat()
{
  struct heartbeat_message hbm;
  while (true)
  {
    int packetSize = UDP.parsePacket();
    if (!packetSize)
    {
      break;
    }
    //DEBUG_PRINTF("HB received %d \r\n", packetSize);    
    UDP.read((char *)&hbm, sizeof(struct heartbeat_message));
    if (hbm.client_id > NUMBER_OF_CLIENTS)
    {
      DEBUG_PRINTLN("Error: invalid client_id received");
      continue;
    }
    heartbeats[hbm.client_id - 1] = true;
    
  }
}

bool checkHeartBeats()
{
  for (int i = 0; i < NUMBER_OF_CLIENTS; i++)
  {
    if (!heartbeats[i])
    {
      return false;
    }
  }
  resetHeartBeats();
  return true;
}


ICACHE_RAM_ATTR void clicked(     )
{
  // Get the pin reading.
  int reading = digitalRead(ROTARY_BTN);

  // Ignore dupe readings.
  if(reading == state) return;

  boolean debounce = false;
  
  // Check to see if the change is within a debounce delay threshold.
  if((millis() - lastDebounceTime) <= debounceDelay) {
    debounce = true;
  }

  // This update to the last debounce check is necessary regardless of debounce state.
  lastDebounceTime = millis();

  // Ignore reads within a debounce delay threshold.
  if(debounce) return;  

  // All is good, persist the reading as the state.
  state = reading;  

  // if rotarty counters are different and click happens
  // then send the brightness setting opmode to the sattelites
  if (rtrCounter != rtrPrevCounter) {
    opMode = 4;
    DEBUG_PRINTF("Setting intensity! %d \r\n", opMode);
    for (int i = 0; i < 1000; i++)
      sendLedData(rtrCounter, opMode);
    
    rtrPrevCounter = rtrCounter;
    return;
  }
    
  if (opMode == numOpModes-1)
    opMode = 1;
  else
    opMode++;


  DEBUG_PRINTF("Setting opmode %d \r\n", opMode);
}

void doubleClicked(Button2& btn)
{
  opMode = 1;
  DEBUG_PRINTLN("double click!");
}



void handleRotaryThings() {
  // Read the current state of CLK
  rtrCrtStateCLK = digitalRead(ROTARY_CLK);

  // If last and current state of CLK are different, then pulse occurred
  // React to only 1 state change to avoid double count
  if (rtrCrtStateCLK != rtrLastStateCLK  && rtrCrtStateCLK == 1){

    // If the DT state is different than the CLK state then
    // the encoder is rotating CCW so decrement
    if (digitalRead(ROTARY_DT) != rtrCrtStateCLK) {
      rtrCounter -= ROTARY_STEP;
    } else {
      // Encoder is rotating CW so increment
      rtrCounter += ROTARY_STEP;
    }

    DEBUG_PRINTF("Rotary counter: %d \r\n", rtrCounter);
  }

  // Remember last CLK state
  rtrLastStateCLK = rtrCrtStateCLK;
}
