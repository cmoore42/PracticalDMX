

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

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false;

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
int channel_offset = 0;

// Timer related items
TickerScheduler ts(1);
void tick_handler(void *);

// Number of channels supported by the hardware
#define NUM_CHANS 4
uint8_t levels[NUM_CHANS];
uint8_t output_pins[NUM_CHANS];

// Overall state
#define STATE_UNKNOWN -1
#define STATE_CONNECTING 0
#define STATE_CONFIG 1
#define STATE_CONNECTED 2
#define STATE_FAILED 3
#define STATE_DATALOSS 4
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
  
  // WiFi.printDiag(Serial); //Remove this line if you do not want to see WiFi password printed
  if (WiFi.SSID()==""){
    Serial.println("We haven't got any access point credentials, so get them now");   
    initialConfig = true;
    state_change(STATE_CONFIG);
  } else {
    state_change(STATE_CONNECTING);
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
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

  if (WiFi.status()!=WL_CONNECTED){
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
      initialConfig = true;
    }
  } else {
    state_change(STATE_CONNECTED);
  }
  
  if (state == STATE_CONNECTED) {
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());

    e131.begin(E131_MULTICAST);
  } else {
    Serial.println("failed to connect, finishing setup anyway");
  }
  updateDisplay();

  ts.add(0, 1000, tick_handler, NULL);
}


void loop() {
  // is configuration portal requested?
  if ((digitalRead(TRIGGER_PIN) == LOW) || (initialConfig)) {
     Serial.println("Configuration portal requested.");
     state_change(STATE_CONFIG);
     updateDisplay();
     digitalWrite(PIN_LED, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
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
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
    ESP.reset(); // This is a bit crude. For some unknown reason webserver can only be started once per boot up 
    // so resetting the device allows to go back into config mode again when it reboots.
    delay(5000);
  }

  
  /* Parse a packet */
  uint16_t num_channels = e131.parsePacket();
    
  /* Process channel data if we have it */
  if (num_channels > 0) {
      ++frames_since_last_tick;
      for (int i=0; i < (num_channels > 4 ? 4 : num_channels); i++) {
        levels[i] = e131.data[i];
      }
      updateLevels();

#if 0
      Serial.printf("Universe %u / %u Channels | Packet#: %u / Errors: %u / CH1: %u\n",
              e131.universe,              // The Universe for this packet
              num_channels,               // Number of channels in this packet
              e131.stats.num_packets,     // Packet counter
              e131.stats.packet_errors,   // Packet error counter
              e131.data[0]);              // Dimmer data for Channel 1
#endif
  }

  ts.update();

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
        display.print(i+1);
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
        display.print(i+1);
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
 * The only reason this exists is so that you can see state changes
 * on the serial port, for debugging.
 */
void state_change(int new_state)
{
  Serial.print("Changing from state ");
  Serial.print(state);
  Serial.print(" to state ");
  state = new_state;
  Serial.println(state);
  
}


