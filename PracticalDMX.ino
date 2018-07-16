/*
The MIT License (MIT)

Copyright (c) 2018 Chris Moore

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Uses or is derived from:
- WiFiManager by Ken Taylor - https://github.com/kentaylor/WiFiManager 
- TickerShceulder by Toshik - https://github.com/Toshik/TickerScheduler 
- E131 by Shelby Merrick - https://github.com/forkineye/E131
*/

// WiFi and WiFiManager
#include <ESP8266WiFi.h>          // https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>          // https://github.com/kentaylor/WiFiManager

// OLED Display stuff
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>         // Library Manager
#include <Adafruit_SSD1306.h>     // Library Manager

// sACN
#include <E131.h>                 // Library Manager

// Timer
#include <TickerScheduler.h>      // https://github.com/Toshik/TickerScheduler

const int PIN_LED = LED_BUILTIN; 

/*Trigger for inititating config mode */
const int TRIGGER_PIN = D3; // Flash button on NodeMCU

// Adafruit_SSD1306 expects a reset pin, but the OLED I'm using doesn't
// have one.  Using the LED pin instead.
#define OLED_RESET LED_BUILTIN
Adafruit_SSD1306 display(OLED_RESET);

// sACN related items
E131 e131;
int fps = 0;
int frames_since_last_tick = 0;
int data_loss_counter = 0;    // Number of seconds since we last saw a frame
#define DATA_LOSS_TIMEOUT 5   // After this many seconds of no data all channels go to 0
int universe = 1;
int base_address = 1;

// Timer related items
TickerScheduler ts(1);
void tick_handler(void *);

// REST server
#define REST_PORT 80
ESP8266WebServer restServer(REST_PORT);
void rest_get_universe();
void rest_put_universe();
void rest_get_address();
void rest_put_address();
void handle_not_found();
void handle_root();

// Number of channels supported by the hardware
#define NUM_CHANS 4
uint8_t levels[NUM_CHANS];
uint8_t output_pins[NUM_CHANS];

// Overall state
#define STATE_UNKNOWN -1    // Startup state
#define STATE_CONNECTING 0  // Trying to connect to WiFi
#define STATE_CONFIG 1      // In AP mode, presenting the config page
#define STATE_CONNECTED 2   // Connected to WiFi
#define STATE_FAILED 3      // In a failed state
#define STATE_DATALOSS 4    // Connected but no sACN received 
int state = STATE_UNKNOWN;

void setup() {
  int i;

  output_pins[0] = D5;
  output_pins[1] = D6;
  output_pins[2] = D7;
  output_pins[3] = D8;
  for (i=0; i<NUM_CHANS; i++) {
    levels[i] = 0;
    pinMode(output_pins[i], OUTPUT);
    analogWrite(output_pins[i], 0);
  }

  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println("\n Starting");

  // Initialize the display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3c);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  updateDisplay();
  
  // WiFi.printDiag(Serial);
 
  if (WiFi.SSID()==""){
    Serial.println("We haven't got any access point credentials, so get them now");   
    state_change(STATE_CONFIG);
  } else {
    Serial.print("Trying to connect to ");
    Serial.println(WiFi.SSID());
    state_change(STATE_CONNECTING);
    updateDisplay();
    WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    unsigned long startedAt = millis();
    int connRes = WiFi.waitForConnectResult();
    float waited = (millis()- startedAt);
    Serial.print("After waiting ");
    Serial.print(waited/1000);
    Serial.print(" secs in setup() connection result is ");
    Serial.println(connRes);
  }
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  if ((WiFi.status()!=WL_CONNECTED) && (state == STATE_CONNECTING)) {
    // Sometimes it seems to go from DISCONNECTED to FAILED and then to CONNECTED.
    // We'll wait up to 10 seconds for it to connect
    int iter = 10;
    while(iter > 0) {
      int connRes = WiFi.status();
      Serial.print("Status is ");
      Serial.println(connRes);
      if (connRes == WL_CONNECTED) {
        state_change(STATE_CONNECTED);
        break;
      }
      delay(1000);
      --iter;
    }
    if (state != STATE_CONNECTED) {
      state_change(STATE_CONFIG);
    }
  } else {
    state_change(STATE_CONNECTED);
  }
  
  if (state == STATE_CONNECTED) {
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("failed to connect, finishing setup anyway");
  }
  updateDisplay();

  ts.add(0, 1000, tick_handler, NULL);
}


void loop() {
  // If the TRIGGER_PIN goes low, enter CONFIG mode
  if (digitalRead(TRIGGER_PIN) == LOW) {
     state_change(STATE_CONFIG);
  }

  if (state == STATE_CONFIG) {
     Serial.println("Configuration portal requested.");
     updateDisplay();
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //sets timeout in seconds until configuration portal gets turned off.
    //If not specified device will remain in configuration mode until
    //switched off via webserver or device is restarted.
    //wifiManager.setConfigPortalTimeout(600);

    //it starts an access point 
    //and goes into a blocking loop awaiting configuration
    if (!wifiManager.startConfigPortal()) {
      state_change(STATE_FAILED);
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      //if you get here you have connected to the WiFi
      state_change(STATE_CONNECTED);
      updateDisplay();
      Serial.println("connected...yeey :)");
      Serial.print("local ip: ");
      Serial.println(WiFi.localIP());
    }
    ESP.reset(); // This is a bit crude. For some unknown reason webserver can only be started once per boot up 
    // so resetting the device allows to go back into config mode again when it reboots.
    delay(5000);
  }

  
  /* Parse a packet */
  uint16_t num_channels = e131.parsePacket();
    
  /* Process channel data if we have it */
  /*
   * data[0] is DMX address 1, etc
   * We are interested in values for "base_address" through "base_address + NUM_CHAN - 1"
   * eg:  if base_address is 20 and NUM_CHAN is 4, we want DMX addresses 20-23
   * 
   * num_channels is how many channels of DMX data we got in a packet
   * 
   * if (num_channels < base_address) then we didn't get data for the our lowest address
   * if (num_channels > (base_address + NUM_CHAN -1)) then we got more than we need
   * 
   */
   
  if (num_channels >= base_address) {
      ++frames_since_last_tick;
      int first = base_address - 1;
      int last = base_address + NUM_CHANS - 1;
      if (last >= num_channels) {
        last = num_channels-1;
      }
      int j=0;
      for (int i=first; i < last; i++) {
        levels[j] = e131.data[i];
        ++j;
      }
      updateLevels();
  }

  // Let the TickerScheduler do its thing
  ts.update();

  // Handle any REST requests
  restServer.handleClient();
}

/**
 * Helper routine to clear the screen, set the cursor position to
 * the top left, and display a line of text.
 */
void displayPrint(char *text) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(text);
}

/**
 * Called when any level changes, to update the PWM output values
 * Also updates the display
 */
void updateLevels() {
  int i;

  for (i=0; i<NUM_CHANS; i++) {
    analogWrite(output_pins[i], levels[i] << 2); // NodeMCU supports analog values of 0-1023
  }

  updateDisplay();
}

/**
 * Update the display based on the current state.
 * This should be the only place that anything is written to
 * the display.
 */
void updateDisplay() {
  int i;

  switch(state) {
    case STATE_UNKNOWN:
      displayPrint("Initializing...");
      display.display();
      break;
    case STATE_CONFIG:
      displayPrint("Config Mode");
      display.display();
      break;
    case STATE_CONNECTING:
      displayPrint("Connecting...");
      display.display();
      break;
    case STATE_FAILED:
      displayPrint("WiFi connection failed.");
      display.display();
      break;
    case STATE_CONNECTED:
      displayPrint("Connected");
      display.println(WiFi.localIP());
      for (i=0; i<NUM_CHANS; i++) {
        display.print("Ch ");
        display.print(universe);
        display.print("/");
        display.print(base_address+i);
        display.print(": ");
        display.println(levels[i]);
      }
      display.print("FPS: ");
      display.println(fps);
      display.display();
      break;
    case STATE_DATALOSS:
      displayPrint("No Data");
      display.println(WiFi.localIP());
      for (i=0; i<NUM_CHANS; i++) {
        display.print("Ch ");
        display.print(universe);
        display.print("/");
        display.print(base_address+i);
        display.print(": ");
        display.println(levels[i]);
      }
      display.display();
      break;
    default:
      displayPrint("???");
      display.display();
      break;
  }

}

/**
 * tick_handler runs every second.
 * It has two jobs - count frames per second, and check for data loss
 */
void tick_handler(void  *arg) {
  fps = frames_since_last_tick;
  if (fps == 0) {
    // No frames in the last second
    ++data_loss_counter;
    if ((data_loss_counter > DATA_LOSS_TIMEOUT) && (state == STATE_CONNECTED)) {
      // No frames in the last DATA_LOSS_TIMEOUT seconds, set all levels to 0
      int i;
      state_change(STATE_DATALOSS);
      for (i=0; i<NUM_CHANS; i++) {
        levels[i] = 0;
      }
      updateLevels();
    }
  } else {
    data_loss_counter = 0;
    if (state == STATE_DATALOSS) {
      state_change(STATE_CONNECTED);
    }
  }
  frames_since_last_tick = 0;
  updateDisplay();
}

/**
 * Helper function to go to a new state.
 */
void state_change(int new_state)
{
  int old_state = state;
  Serial.print("Changing from state ");
  Serial.print(old_state);
  Serial.print(" to state ");
  Serial.println(new_state);

  /* Trun on the LED if we're entering CONFIG mode, otherwise turn it off */
  if (new_state == STATE_CONFIG) {
    digitalWrite(PIN_LED, LOW);
  } else {
    digitalWrite(PIN_LED, HIGH);
  }

  /*  If we just connected, start the E13.1 processing - unless this was
   *  just a transition from DATALOSS to CONNECTED, in which case E13.1
   *  was already running.
   */
  if ((new_state == STATE_CONNECTED) && (old_state != STATE_DATALOSS)) {
    /* Start the E13.1 listener */
    e131.begin(E131_MULTICAST, universe);

    /* Start the REST listener */
    restServer.on("/universe", HTTP_GET, rest_get_universe);
    restServer.on("/universe", HTTP_PUT, rest_put_universe);
    restServer.on("/address", HTTP_GET, rest_get_address);
    restServer.on("/address", HTTP_PUT, rest_put_address);
    restServer.onNotFound(handle_not_found);
    restServer.on("/", handle_root);
    restServer.begin();
    Serial.print("REST server started on port ");
    Serial.println(REST_PORT);
  }
  
  state = new_state; 
}

void rest_get_universe()
{
  String body = String(universe);
  restServer.send(200, "text/plain", body);
}

void rest_put_universe()
{
  
}

void rest_get_address()
{
  String body = String(base_address);
  restServer.send(200, "text/plain", body);  
}

void rest_put_address()
{
  
}

void handle_root()
{
  restServer.send(200, "text/plain", "Root");
}

void handle_not_found()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += restServer.uri();
  message += "\nMethod: ";
  message += (restServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += restServer.args();
  message += "\n";
  for (uint8_t i = 0; i < restServer.args(); i++) {
    message += " " + restServer.argName(i) + ": " + restServer.arg(i) + "\n";
  }
  restServer.send(404, "text/plain", message); 
}



