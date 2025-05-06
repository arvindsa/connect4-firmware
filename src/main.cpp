#include <Arduino.h>
#include <Bounce2.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ASA.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "MenuManager.h"

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

#define COLOR_RED pixels.Color(50, 0, 0)
#define COLOR_GREEN pixels.Color(0, 50, 0)
#define COLOR_BLUE pixels.Color(0, 0, 15)
#define COLOR_YELLOW pixels.Color(15, 30, 0)
#define COLOR_WHITE pixels.Color(15, 15, 30)
#define COLOR_BLACK pixels.Color(0, 0, 0)

/********************/
//  MODE
/*******************/
// 0 - Menu
// 1 - Touch Test
// 2- Touched latched
// 3 - column last
// 4 - connect5
// 5- starfield
// 7 pixelframwe
int utility = 0;
int mode = 0; // 0 = menu mode, 1 = game mode, etc.

#define LED_PIN 4
#define NUM_PIXELS 49
#define NUM_STAR_LEDS 4 // pick between 10-15
Adafruit_NeoPixel pixels(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
void pixelsoff();

struct StarLED
{
  int index;
  uint8_t r, g, b;
  uint8_t targetR, targetG, targetB;
  bool fadingIn; // true: moving to target; false: fading back to black
};

StarLED starLEDs[NUM_STAR_LEDS];
bool stars_initialized = false;
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 60; // ms between color steps
const float brightnessFactor = 0.2;      // adjust between 0.0 and 1.0

/********************/
//  SWITCHES
/*******************/
#define SW1 32 // Set as SW2 in the Schematic
#define SW2 27 // Set as SW1 in the Schematic
#define SW3 33
Bounce sw1 = Bounce();
Bounce sw2 = Bounce();
Bounce sw3 = Bounce();

/********************/
//  Touch
/*******************/
struct TouchEvent
{
  uint8_t row;
  uint8_t col;
  bool isTouched;   // true = touched, false = released
  bool lastTouched; // true = touched, false = released
};
TouchEvent lastEvent;          // stores the last touch/release event
bool lastTouchState[7][7];     // assuming 7 rows, 7 columns (max 7 inputs per sensor used)
uint8_t gridState[7][7] = {0}; // Used in many ways
Adafruit_MPR121 cap1 = Adafruit_MPR121();
Adafruit_MPR121 cap2 = Adafruit_MPR121();
uint16_t cap1_lasttouched = 0;
uint16_t cap1_currtouched = 0;
uint16_t cap2_lasttouched = 0;
uint16_t cap2_currtouched = 0;
int last_led_state = 0;
bool gameover = false;

/********************/
//  OLED Screen
/*******************/
#define SCREEN_WIDTH 128    // OLED display width, in pixels
#define SCREEN_HEIGHT 64    // OLED display height, in pixels
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
const int MAX_LINES = 8; // Maximum lines for terminal buffer
String terminalBuffer[MAX_LINES];
int currentTextSize = 2;   // Default text size
bool truncateLines = true; // Truncate instead of wrapping lines
void addToTerminal(String message);
MenuManager *menuMgr;
bool menuChanged = true;

// Example actions
void sayHello()
{
  mode = 1;
  Serial.println("Hello selected!");
  addToTerminal("Redraw");
}

void sayWorld()
{
  Serial.println("World selected!");
}

/********************/
//  Game Results
/*******************/
int winningCoords[10][2]; // store up to 10 coordinates (row, col) - assuming max g=10
int winningCount = 0;     // how many coordinates are saved

/********************/
//  Wifi Connections
/*******************/
const char *ssid = "************";
const char *password = "************";
const char *mqtt_server = "************";
const int mqtt_port = 8883;
const char *mqtt_topic = "************";
const char *mqtt_user = "************"; // replace with your EMQX username
const char *mqtt_pass = "************"; // replace with your EMQX password
bool mqtt_initialized = false;

// Use secure client for port 8883
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Optional: set to true if broker uses a self-signed cert and you want to skip validation
bool skipCertValidation = true;

int getLEDIndex(int row, int col)
{
  // Returns the LED number for neopixel from row and column
  return row * 7 + ((row & 1) ? 6 - col : col);
}

// Prototypes
void setModeConnect4();
void setStarMode();
void resetGrid();
void setupMQTT();
void setPixelFrameMode();

void setup_wifi()
{
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
    retries++;
    if (retries > 30)
    {
      Serial.println("\nWiFi connection failed. Restarting...");
      ESP.restart();
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  addToTerminal("Wifi Conn");
}

bool setLEDImageFromJson(const char *json)
{
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, json);
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  JsonArray data = doc["data"];
  if (!data || data.size() != 7)
  {
    Serial.println(F("Invalid data array"));
    return false;
  }

  for (int row = 0; row < 7; row++)
  {
    JsonArray rowData = data[row];
    if (!rowData || rowData.size() != 7)
    {
      Serial.print(F("Invalid row data at row "));
      Serial.println(row);
      return false;
    }

    for (int col = 0; col < 7; col++)
    {
      JsonArray pixel = rowData[col];
      if (!pixel || pixel.size() != 3)
      {
        Serial.print(F("Invalid pixel data at row "));
        Serial.print(row);
        Serial.print(F(", col "));
        Serial.println(col);
        return false;
      }

      int r = pixel[0];
      int g = pixel[1];
      int b = pixel[2];

      int ledIndex = getLEDIndex((6 - row), (6 - col));
      pixels.setPixelColor(ledIndex, pixels.Color(r, g, b));
    }
  }

  pixels.show();
  return true;
}

void callback(char *topic, byte *message, unsigned int length)
{
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  addToTerminal("Mqtt Recd");

  // Create a buffer with room for null terminator
  char jsonBuffer[length + 1];

  for (unsigned int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    jsonBuffer[i] = (char)message[i];
  }
  jsonBuffer[length] = '\0'; // Null terminate the string
  Serial.println();

  // Call your JSON processing function
  if (!setLEDImageFromJson(jsonBuffer))
  {
    Serial.println("Failed to process JSON data");
  }
  else
  {
    addToTerminal("Pic Update");
  }
}

void reconnect()
{
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection to ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.println(mqtt_port);

    // Connect without username/password or cert validation
    if (client.connect("ESP32Client", mqtt_user, mqtt_pass))
    {
      Serial.println("MQTT connected");
      client.subscribe(mqtt_topic);
      client.setBufferSize(1024);
      Serial.println("Subscribed to topic");
      addToTerminal("MQTT Subs");
    }
    else
    {
      Serial.print("Failed MQTT connection, rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 seconds...");
      addToTerminal("MQTT Subs Fail");
      delay(5000);
    }
  }
}

void initialize_mpr()
{
  if (!cap1.begin(0x5A))
  {
    Serial.println("MPR121 A not found, check wiring?");
    while (1)
      ;
  }
  else
  {
    Serial.println("MPR121 A found");
  }
  if (!cap2.begin(0x5B))
  {
    Serial.println("MPR121 B not found, check wiring?");
    while (1)
      ;
  }
  else
  {
    Serial.println("MPR121 B found");
  }

  // Increase charge current (default = 16uA → max = 63uA)
  cap1.writeRegister(0x5C, 0x3F); // 0x3F = 63uA
  cap2.writeRegister(0x5C, 0x3F); // 0x3F = 63uA

  // Increase charge time (default = 1us → max = 0x03 = 32us)
  cap1.writeRegister(0x5D, 0x03); // 0x03 = 32us
  cap2.writeRegister(0x5D, 0x03); // 0x03 = 32us

  // lower touch/release thresholds for sensitivity
  cap1.setThresholds(3, 3); // lower = more sensitivev
  cap2.setThresholds(3, 3); // lower = more sensitive

  cap1.writeRegister(0x2B, 0x00); // MHD_R
  cap1.writeRegister(0x2C, 0x00); // NHD_R
  cap1.writeRegister(0x2D, 0x00); // NCL_R
  cap1.writeRegister(0x2E, 0x00); // FDL_R

  cap2.writeRegister(0x2B, 0x00); // MHD_R
  cap2.writeRegister(0x2C, 0x00); // NHD_R
  cap2.writeRegister(0x2D, 0x00); // NCL_R
  cap2.writeRegister(0x2E, 0x00); // FDL_R
}

void processTouch(bool lightUp)
{
  cap1_currtouched = cap1.touched(); // rows
  cap2_currtouched = cap2.touched(); // columns

  for (uint8_t col = 0; col < 7; col++)
  {
    bool colTouched = cap1_currtouched & _BV(col);

    for (uint8_t row = 0; row < 7; row++)
    {
      bool rowTouched = cap2_currtouched & _BV(row);
      bool isCurrentlyTouched = colTouched && rowTouched;

      if (isCurrentlyTouched && !lastTouchState[row][col])
      {
        lastTouchState[row][col] = true;
        lastEvent.row = 6 - row; // flip row
        lastEvent.col = 6 - col; // flip col
        lastEvent.isTouched = true;

        int index = getLEDIndex(lastEvent.row, lastEvent.col);
        Serial.print(lastEvent.row);
        Serial.print(",");
        Serial.print(lastEvent.col);
        Serial.print(" (");
        Serial.print(index);
        Serial.println(") is touched");

        if (lightUp)
        {
          pixels.setPixelColor(index, pixels.Color(20, 0, 0));
          pixels.show();
        }
      }
      else if (!isCurrentlyTouched && lastTouchState[row][col])
      {
        lastTouchState[row][col] = false;
        lastEvent.row = 6 - row; // flip row
        lastEvent.col = 6 - col; // flip col
        lastEvent.isTouched = false;
        int index = getLEDIndex(lastEvent.row, lastEvent.col);

        Serial.print(lastEvent.row);
        Serial.print(",");
        Serial.print(lastEvent.col);
        Serial.print(" (");
        Serial.print(index);
        Serial.println(") is released");

        if (lightUp)
        {
          pixels.setPixelColor(index, pixels.Color(0, 0, 0));
          pixels.show();
        }
      }
    }
  }
}

void initDisplay()
{
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    while (1)
      ; // Halt program
  }
  display.clearDisplay();
  display.drawBitmap(0, 0, asa_data, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
  display.display(); // Show Adafruit splash screen
  delay(1000);
  display.clearDisplay();
}

/**
 * @brief Sets the text size for the display.
 * @param textSize The desired text size.
 */
void setTextSize(int textSize)
{
  currentTextSize = textSize;
}

/**
 * @brief Sets the line truncation option.
 * @param truncate True to truncate, false to wrap text.
 */
void setTruncateLines(bool truncate)
{
  truncateLines = truncate;
}

/**
 * @brief Processes and adds a message to the display buffer.
 * @param message The message to add.
 * @param visibleLines The number of visible lines on the screen.
 */
void processAndAddToBuffer(String message, int visibleLines)
{
  int maxCharsPerLine = SCREEN_WIDTH / (6 * currentTextSize); // Calculate max characters per line

  if (truncateLines)
  {
    // Truncate message
    if (message.length() > maxCharsPerLine)
    {
      message = message.substring(0, maxCharsPerLine);
    }
  }

  // Scroll buffer up
  for (int j = 0; j < visibleLines - 1; j++)
  {
    terminalBuffer[j] = terminalBuffer[j + 1];
  }
  terminalBuffer[visibleLines - 1] = message;
}

/**
 * @brief Adds a message to the OLED terminal and updates the display.
 * @param message The message to display.
 */
void addToTerminal(String message)
{
  // return;
  int visibleLines = SCREEN_HEIGHT / (8 * currentTextSize); // Calculate visible lines

  processAndAddToBuffer(message, visibleLines);

  // Update the display
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(currentTextSize);

  for (int i = 0; i < visibleLines; i++)
  {
    display.setCursor(0, i * 8 * currentTextSize);
    display.println(terminalBuffer[i]);
  }

  display.display();
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  pixels.begin(); // Initialize NeoPixel
  pixels.clear();
  pixels.show();

  initDisplay();

  // Initialize switches with INPUT_PULLUP
  pinMode(SW1, INPUT);
  pinMode(SW2, INPUT);
  pinMode(SW3, INPUT);

  sw1.attach(SW1);
  sw2.attach(SW2);
  sw3.attach(SW3);

  sw1.interval(25);
  sw2.interval(25);
  sw3.interval(25);

  // Initialize
  initialize_mpr();

  // Define submenus
  std::vector<MenuItem> settingsMenu = {
      {"Setting 1", []()
       { Serial.println("Setting 1 chosen"); },
       {}},
      {"Setting 2", []()
       { Serial.println("Setting 2 chosen"); },
       {}}};

  std::vector<MenuItem> mainMenu = {
      {"Connect4", setModeConnect4, {}},
      {"Pixel Frame", setPixelFrameMode, {}},
      {"Stars", setStarMode, {}},
      {"LED off", pixelsoff, {}},
      {"Settings", nullptr, settingsMenu}
    };
      

  menuMgr = new MenuManager(&display);
  menuMgr->setRootMenu(mainMenu);

}

// Turn on the led RED when touched and off when released
void showTouched()
{
  // ignore if the current and last is same
  if (lastEvent.isTouched == lastEvent.lastTouched)
  {
    return;
  }
  int index = getLEDIndex(lastEvent.row, lastEvent.col);
  if (lastEvent.isTouched)
  {
    // Turn on the led
    pixels.setPixelColor(index, pixels.Color(20, 0, 0));
  }
  else
  {
    pixels.setPixelColor(index, pixels.Color(0, 0, 0));
  }
  pixels.show();
  lastEvent.lastTouched = lastEvent.isTouched;
}

void recolorLED()
{
  for (int row = 0; row < 7; row++)
  {
    for (int col = 0; col < 7; col++)
    {
      uint8_t state = gridState[row][col];
      uint32_t color;

      switch (state)
      {
      case 0:
        color = COLOR_BLACK;
        break;
      case 1:
        color = COLOR_RED;
        break;
      case 2:
        color = COLOR_GREEN;
        break;
      case 3:
        color = COLOR_BLUE;
        break;
      case 4:
        color = COLOR_YELLOW;
        break;
      case 5:
        color = COLOR_WHITE;
        break;
      default:
        color = COLOR_BLACK;
        break;
      }

      int ledIndex = getLEDIndex(row, col);
      pixels.setPixelColor(ledIndex, color);
    }
  }

  pixels.show(); // Commit changes to the LEDs
}

// FLip the led of touched in a latched mode
void flipTouched()
{
  // ignore if the current and last is same
  if (lastEvent.isTouched == lastEvent.lastTouched)
  {
    return;
  }
  int index = getLEDIndex(lastEvent.row, lastEvent.col);
  if (lastEvent.isTouched)
  {
    // Turn on the led
    gridState[lastEvent.row][lastEvent.col] = (gridState[lastEvent.row][lastEvent.col] == 1) ? 0 : 1;
  }
  pixels.show();
  lastEvent.lastTouched = lastEvent.isTouched;
  recolorLED();
}

// Turns on the last unlit led in the column
bool turnLastinColumn(int color = 1)
{
  bool result = false;
  // ignore if the current and last is same
  if (lastEvent.isTouched == lastEvent.lastTouched)
  {
    return false;
  }
  if (lastEvent.isTouched)
  {
    for (int i = 0; i < 7; i++)
    {
      if (gridState[i][lastEvent.col] == 0)
      {
        gridState[i][lastEvent.col] = color;
        result = true;
        break;
      }
    }
  }
  lastEvent.lastTouched = lastEvent.isTouched;
  recolorLED();
  return result;
}

void checkConsecutive(int g)
{
  for (int row = 0; row < 7; row++)
  {
    for (int col = 0; col < 7; col++)
    {
      int val = gridState[row][col];
      if (val != 1 && val != 2)
        continue; // skip if not 1 or 2

      // Check horizontal (to the right)
      if (col <= 7 - g)
      {
        bool match = true;
        for (int k = 1; k < g; k++)
        {
          if (gridState[row][col + k] != val)
          {
            match = false;
            break;
          }
        }
        if (match)
        {
          gameover = true;
          Serial.print("Horizontal match of ");
          Serial.print(val);
          Serial.println(" at:");
          winningCount = 0;
          for (int k = 0; k < g; k++)
          {
            Serial.print("(");
            Serial.print(row);
            Serial.print(",");
            Serial.print(col + k);
            Serial.print(") ");
            winningCoords[k][0] = row;     // save row
            winningCoords[k][1] = col + k; // save col
            winningCount++;
          }

          Serial.println();
        }
      }

      // Check vertical (downwards)
      if (row <= 7 - g)
      {
        bool match = true;
        for (int k = 1; k < g; k++)
        {
          if (gridState[row + k][col] != val)
          {
            match = false;
            break;
          }
        }
        if (match)
        {
          gameover = true;
          Serial.print("Vertical match of ");
          Serial.print(val);
          Serial.println(" at:");
          winningCount = 0;
          for (int k = 0; k < g; k++)
          {
            Serial.print("(");
            Serial.print(row + k);
            Serial.print(",");
            Serial.print(col);
            Serial.print(") ");
          }
          Serial.println();
        }
      }

      // Check diagonal down-right
      if (row <= 7 - g && col <= 7 - g)
      {
        bool match = true;
        for (int k = 1; k < g; k++)
        {
          if (gridState[row + k][col + k] != val)
          {
            match = false;
            break;
          }
        }
        if (match)
        {
          gameover = true;
          Serial.print("Diagonal down-right match of ");
          Serial.print(val);
          Serial.println(" at:");
          winningCount = 0;
          for (int k = 0; k < g; k++)
          {
            Serial.print("(");
            Serial.print(row + k);
            Serial.print(",");
            Serial.print(col + k);
            Serial.print(") ");
          }
          Serial.println();
        }
      }

      // Check diagonal up-right
      if (row >= g - 1 && col <= 7 - g)
      {
        bool match = true;
        for (int k = 1; k < g; k++)
        {
          if (gridState[row - k][col + k] != val)
          {
            match = false;
            break;
          }
        }
        if (match)
        {
          Serial.print("Diagonal up-right match of ");
          Serial.print(val);
          Serial.println(" at:");
          for (int k = 0; k < g; k++)
          {
            Serial.print("(");
            Serial.print(row - k);
            Serial.print(",");
            Serial.print(col + k);
            Serial.print(") ");
          }
          Serial.println();
        }
      }
    }
  }
}

void turnLastinColumn2Player()
{
  if (gameover)
  {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(3);
    display.setCursor(0,0);
    display.println("Winner");
    display.print(last_led_state);
    display.display();
    return;
  }
  int color = (last_led_state == 1) ? 2 : 1;
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Player");
  display.print(color);
  display.display();
  bool result = turnLastinColumn(color);
  if (result)
  {
    last_led_state = color;
    checkConsecutive(4);
    return;
  }
}

// Helper: get random inactive LED index
int getRandomInactiveLED()
{
  bool used[NUM_PIXELS] = {false};
  for (int i = 0; i < NUM_STAR_LEDS; i++)
  {
    used[starLEDs[i].index] = true;
  }
  int tries = 0;
  while (tries < 100)
  {
    int rnd = random(NUM_PIXELS);
    if (!used[rnd])
      return rnd;
    tries++;
  }
  return -1; // fallback
}

void assignNewTarget(StarLED &led)
{
  led.index = getRandomInactiveLED();
  led.targetR = random(50, 150) * brightnessFactor;
  led.targetG = random(50, 150) * brightnessFactor;
  led.targetB = random(50, 150) * brightnessFactor;
  led.fadingIn = true;
  led.r = 0;
  led.g = 0;
  led.b = 0;
}

void updateStarLEDs()
{
  if (millis() - lastUpdate >= updateInterval)
  {
    lastUpdate = millis();

    for (int i = 0; i < NUM_STAR_LEDS; i++)
    {
      StarLED &led = starLEDs[i];

      if (led.fadingIn)
      {
        // Step toward target
        if (led.r < led.targetR)
          led.r++;
        else if (led.r > led.targetR)
          led.r--;

        if (led.g < led.targetG)
          led.g++;
        else if (led.g > led.targetG)
          led.g--;

        if (led.b < led.targetB)
          led.b++;
        else if (led.b > led.targetB)
          led.b--;

        // Check if reached target
        if (led.r == led.targetR && led.g == led.targetG && led.b == led.targetB)
        {
          led.fadingIn = false; // start fading out
        }
      }
      else
      {
        // Step toward black
        if (led.r > 0)
        {
          led.r--;
        }
        if (led.g > 0)
        {
          led.g--;
        }
        if (led.b > 0)
        {
          led.b--;
        }

        // Check if fully black
        if (led.r == 0 && led.g == 0 && led.b == 0)
        {
          pixels.setPixelColor(led.index, pixels.Color(0, 0, 0));
          assignNewTarget(led);
        }
      }

      // Set pixel color
      pixels.setPixelColor(led.index, pixels.Color(led.r, led.g, led.b));
      // Serial.print("Set LED ");
      // Serial.print(led.index);
      // Serial.print(" color to (");
      // Serial.print(led.r);
      // Serial.print(", ");
      // Serial.print(led.g);
      // Serial.print(", ");
      // Serial.print(led.b);
      // Serial.println(")");
    }

    pixels.show();
  }
}

void initialize_stars()
{
  if (stars_initialized)
  {
    return;
  }
  randomSeed(analogRead(0));

  // initialize star LEDs
  for (int i = 0; i < NUM_STAR_LEDS; i++)
  {
    starLEDs[i].index = -1; // mark as unassigned
    assignNewTarget(starLEDs[i]);
  }
  stars_initialized = true;
}

void loop()
{
  // Update debouncers
  sw1.update();
  sw2.update();
  sw3.update();

  if (sw1.fell())
  {
    Serial.println("SW1 pressed");
    if (mode == 0)
    {
      menuMgr->select();
      menuChanged = true;
    }
    else
    {
      mode = 0;
      utility = 0;
      menuChanged = true;
    }
  }
  if (sw2.fell())
  {
    Serial.println("SW2 pressed");

    if (mode == 0)
    {
      menuMgr->navigateUp();
      menuChanged = true;
    }
  }
  if (sw3.fell())
  {
    Serial.println("SW3 pressed");
    if (mode == 0)
    {
      menuMgr->navigateDown();
      menuChanged = true;
    }
  }
  processTouch(false);
  if (menuChanged && mode == 0)
  {
    menuMgr->render();
    menuChanged = false;
  }
  switch (utility)
  {
  case 1:
    /* code */
    showTouched();
    break;

  case 2:
    flipTouched();
    break;

  case 3:
    turnLastinColumn();
    break;

  case 4:
    turnLastinColumn2Player();
    break;

  case 5:
    initialize_stars();
    updateStarLEDs();
    break;

  case 6:
    if(!mqtt_initialized){
      setupMQTT();
    }
    if (!client.connected())
    {
      reconnect();
    }
    client.loop();
    break;

  default:
    break;
  }
}

void pixelsoff(){
  pixels.clear();
  pixels.show();
}

void setModeConnect4(){
  pixelsoff();
  resetGrid();
  gameover=false;
  utility  = 4;
  mode=1;
}

void setStarMode(){
  pixelsoff();
  utility = 5;
  mode=1;
  addToTerminal("Stars");
}

void resetGrid() {
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 7; col++) {
      gridState[row][col] = 0;
    }
  }
}

void setupMQTT(){
  setup_wifi();
  if (skipCertValidation)
  {
    espClient.setInsecure(); //
    Serial.println("WARNING: Skipping certificate validation!");
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  mqtt_initialized=true; 
}

void setPixelFrameMode(){
  utility = 6;
  mode=1;
  setupMQTT();
}