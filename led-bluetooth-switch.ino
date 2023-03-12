/****************************************************************************************************************************************
Author:      Vladimir Trifonov
Creation:    2023-03-11
Description: A POC of color or pattern switcher for WS2812 LED strip, using an ESP32 microcontroller.
  Da project can be upgraded using a BLE (Bluetooth Low Energy).

  To control da LED via a bluetooth, you can use da following Android app:
    Serial Bluetooth Terminal -- https://play.google.com/store/apps/details?id=de.kai_morich.serial_bluetooth_terminal&hl=fr&gl=US&pli=1
    Configure da following shortcuts:
  
    |===============|====================|
    |     Button    |       Value        |
    |===============|====================|
    | M1 -> red     | COLOR_255_0_0_32   |
    | M2 -> green   | COLOR_0_255_0_32   |
    | M3 -> blue    | COLOR_0_0_255_32   |
    | M4 -> pattern | PATTERN_RAINBOW_32 |
    |===============|====================|

    Color format:   {COLOR}_{R}_{G}_{B}_{brightness}
    Pattern format: {PATTERN}_{pattern_name}_{brightness}

****************************************************************************************************************************************/

#include <FastLED.h>
#include "BluetoothSerial.h"



// ********** Ambient LED **********
#define LED_STRIP_1_DATA_PIN   13
#define LED_STRIP_2_DATA_PIN   14
#define NUM_LEDS               26
#define LED_TYPE               WS2812
#define COLOR_ORDER            GRB
#define FRAMES_PER_SECOND      120

uint8_t      gHue = 0;                                         // Rotating "base color" used by many of the patterns

CRGB         ledStrip1[NUM_LEDS];
CRGB         ledStrip2[NUM_LEDS];

const String DEFAULT_LED_STYLE = "PATTERN_RAINBOW_32";         // How da LED will start


// ********** BLUETOOTH **********
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#define BLUETOOTH_NAME "LEDSwitcher"

String            btCommand;
BluetoothSerial   SerialBT;
int               btCommandDelay = 1000;


// ********** CORE **********
TaskHandle_t      CoreHandlerTaskHandle;
TaskHandle_t      LEDHandlerTaskHandle;
SemaphoreHandle_t binarySemaphore;
QueueHandle_t     queue;

const char        BT_COMMAND_DELIMITER = '_';


// ***** Parameters for all da modules used by da Bluetooth *****
// For all parameters have to create a corresponding pointer
int               paramR;
int               paramG;
int               paramB;
int               paramBrightness;

String            paramPattern;
String            paramStyle;
String            paramCommand;

// Pointers for da parameters
int              *pParamR;
int              *pParamG;
int              *pParamB;
int              *pParamBrightness;

String           *pParamPattern;
String           *pParamStyle;
String           *pParamCommand;               // REVIEW: not used, but it will be helpfull ;)



// *********************************************************************************
// *********************************************************************************
void setup() {
  Serial.begin(115200);

  delay(100);

  Serial.println("\n\n************************************************************");
  Serial.println("                .:: LED Bluetooth Switch ::.");
  Serial.println("************************************************************\n");
  


  // ***** Core handlers -- Core 1 *****
  xTaskCreatePinnedToCore(
    CoreHandlerTask,                           // Task function
    "CoreHandlerTask",                         // Name of task
    10000,                                     // Stack size of task
    NULL,                                      // Parameter of the task
    1,                                         // Priority of the task
    &CoreHandlerTaskHandle,                    // Task handle to keep track of created task
    1                                          // Pin task to core
  );


  delay(2500);


  // ***** LED handler -- Core 0 *****
  xTaskCreatePinnedToCore(
    LEDHandlerTask,                            // Task function
    "LEDHandlerTask",                          // Name of task
    10000,                                     // Stack size of task
    NULL,                                      // Parameter of the task
    1,                                         // Priority of the task
    &LEDHandlerTaskHandle,                     // Task handle to keep track of created task
    0                                          // Pin task to core
  );
  
  
  delay(1000); 
}

void CoreHandlerTask(void * pvParameters) {

  Serial.println("\n*** Executed task: [CoreHandlerTask] ***\n");

  SerialBT.begin(BLUETOOTH_NAME);
  Serial.println("* Bluetooth init [OK]");
  Serial.print("Hey! Pair me by connecting to: ");
  Serial.println(BLUETOOTH_NAME);


  while(1) {

    // Get Bluetooth commands, parse & set da values to da parameters 
    if (SerialBT.available()) {

      char incomingChar = SerialBT.read();

      if (incomingChar != '\n') {
        btCommand += String(incomingChar);
      } else{
        Serial.print("BT command received: ");
        Serial.println(btCommand);
        
        if(parseCommandAndSetParameters(btCommand)) {
          Serial.println("Bluetooth parameters set: [OK]");
        } else {
          Serial.println("Bluetooth parameters set: [X]");
        }

        btCommand = "";
      }

      //Serial.write(incomingChar);
    }
    

    // Send commands to da Bluetooth
    if (Serial.available()) {
      SerialBT.write(Serial.read());
    }

    delay(10);
  }

  vTaskDelete(NULL);
}

void LEDHandlerTask(void * pvParameters) {
  Serial.println("\n*** Executed task: [LEDHandlerTask] ***\n");

  initAmbientLED();

  linkModulesParameters();

  setDefaultValuesForModulesParameters();
  
  //testLED();

  String tempParamPattern    = "";
  int    tempParamR          = 0;
  int    tempParamG          = 0;
  int    tempParamB          = 0;
  int    tempParamBrightness = 0;

  bool   doContinuousLoop    = true;
  

  while(1) {

    normalizeModulesParameters();

    // Is a new BT command received
    if(*pParamStyle == "COLOR" && (*pParamR != tempParamR || *pParamG != tempParamG || *pParamB != tempParamB || *pParamBrightness != tempParamBrightness)) {
      tempParamR          = *pParamR;
      tempParamG          = *pParamG;
      tempParamB          = *pParamB;
      tempParamBrightness = *pParamBrightness;

      doContinuousLoop    = true;
    } else if(*pParamStyle == "PATTERN") {
      doContinuousLoop = true;
    }


    if(doContinuousLoop) {
    
      if(*pParamStyle == "PATTERN") {
        setAmbientLEDPatternStyle(*pParamPattern, *pParamBrightness);

        tempParamPattern    = *pParamPattern;
        tempParamBrightness = *pParamBrightness;
        
        doContinuousLoop = true;

      } else if(*pParamStyle == "COLOR") {
        setAmbientLEDColorStyle(*pParamR, *pParamG, *pParamB, *pParamBrightness);

        //tempParamR          = *pParamR;
        //tempParamG          = *pParamG;
        //tempParamB          = *pParamB;
        //tempParamBrightness = *pParamBrightness;

        Serial.println("   --- New color was set ---");
        
        doContinuousLoop = false;
      }

      //Serial.print("Style: ");
      //Serial.print(*pParamStyle);
      //Serial.print(" | Pattern: ");
      //Serial.print(*pParamPattern);

      //Serial.print(", R: ");
      //Serial.print(*pParamR);
      //Serial.print(" G: ");
      //Serial.print(*pParamG);
      //Serial.print(" B: ");
      //Serial.print(*pParamB);
      //Serial.print(", Brightness: ");
      //Serial.println(*pParamBrightness);

    } else {
      delay(1000);
    }
  }

  vTaskDelete(NULL);
}
// *********************************************************************************


// *********************************************************************************
//                            *** Ambient LED ***
// *********************************************************************************
void initAmbientLED() {
  FastLED.addLeds<LED_TYPE, LED_STRIP_1_DATA_PIN, COLOR_ORDER>(ledStrip1, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<LED_TYPE, LED_STRIP_2_DATA_PIN, COLOR_ORDER>(ledStrip2, NUM_LEDS).setCorrection(TypicalLEDStrip);
  
  // OFF da LED
  setAmbientLEDColorStyle(0, 0, 0, 0);

  // TODO: do this separately in an other function, setting da default behaviour in da init function is a side effect.
  // If you want to just test da LED, no need to set a default value neither da queue.
  // 
  // TODO: in da futur, read those values from da ESP's EEPROM memory
  // check: https://randomnerdtutorials.com/esp32-flash-memory/


  Serial.println("* FastLED init [OK]");
  Serial.println("");
}

void testLED() {
  setAmbientLEDColorStyle(random(1, 255), random(1, 255), random(1, 255), random(1, 255));

  Serial.println("  * Testing da LED: [OK]");
}

bool setAmbientLEDColorStyle(int red, int green, int blue, int brightness) {
  if(red >= 0 && red <= 255 && green >= 0 && green <= 255 && blue >= 0 && blue <= 255 && brightness >= 0 && brightness <= 255) {
    for(int i = 0; i < NUM_LEDS; i++) {
      ledStrip1[i].setRGB(red, green, blue);
      ledStrip2[i].setRGB(red, green, blue);
    }

    FastLED.setBrightness(brightness);
    FastLED.show();
    
    
    Serial.print("Color set to: R: ");
    Serial.print(red);
    Serial.print(", G: ");
    Serial.print(green);
    Serial.print(", B: ");
    Serial.print(blue);
    Serial.print(" | Brightness: ");
    Serial.println(brightness);

    return true;
  } else {
    Serial.println("   *** WARNING! Color was not set. R, G, B or brightness parameters are not correct (0-255). ***");
  }

  return false;
}

bool setAmbientLEDPatternStyle(String pattern, int brightness) {

  // TODO: make a check of da allowed patterns
  if(true) {
    if(pattern == "RAINBOW") {
      rainbow(brightness);
    }

    return true;
  }

  return false;
}

void rainbow(int brightness) {
  FastLED.delay(1000 / FRAMES_PER_SECOND);              // Insert a delay to keep the framerate modest

  EVERY_N_MILLISECONDS(20) { gHue++; }                  // Slowly cycle the "base color" through the rainbow

  fill_rainbow(ledStrip1, NUM_LEDS, gHue, 7);
  fill_rainbow(ledStrip2, NUM_LEDS, gHue, 7);

  FastLED.setBrightness(brightness);
  FastLED.show();
}


// *********************************************************************************
//                            *** Bluetooth ***
// *********************************************************************************
bool parseCommandAndSetParameters(String command) {
  // Parse parse da string command based on a particular format and then return true if alles goed or false if not.
  // LED: pattern format: {PATTERN}_{pattern_name}_{brightness} -- COLOR_255_0_0_32
  // LED: color format:   {COLOR}_{R}_{G}_{B}_{brightness}      -- PATTERN_RAINBOW_32
  // LED: on/off da LED:  {ON} / {OFF}                          -- ON | OFF


  int    tempIntValue;
  int    i                = 0;
  String tempParamStyle   = getSegmentFromDelimitedString(command, BT_COMMAND_DELIMITER, i++);
  String tempStringValue;


  if(tempParamStyle == "PATTERN") {
    // {PATTERN}_{pattern_name}_{brightness}

    paramCommand    = command;
    paramStyle      = tempParamStyle;

    tempStringValue = getSegmentFromDelimitedString(command, BT_COMMAND_DELIMITER, i++);

    //if(isValidPattern(tempStringValue)) {
    if(true) { paramPattern = tempStringValue; }

    tempIntValue = getSegmentFromDelimitedString(command, BT_COMMAND_DELIMITER, i++).toInt();
    if(isValidUnsignedByte(tempIntValue)) { paramBrightness = tempIntValue; }
    

    return true;

  } else if(tempParamStyle == "COLOR") {
    // {COLOR}_{R}_{G}_{B}_{brightness}

    paramCommand   = command;
    paramStyle     = tempParamStyle;

    tempIntValue   = getSegmentFromDelimitedString(command, BT_COMMAND_DELIMITER, i++).toInt();
    if(isValidUnsignedByte(tempIntValue)) { paramR = tempIntValue; }


    tempIntValue = getSegmentFromDelimitedString(command, BT_COMMAND_DELIMITER, i++).toInt();
    if(isValidUnsignedByte(tempIntValue)) { paramG = tempIntValue; }


    tempIntValue = getSegmentFromDelimitedString(command, BT_COMMAND_DELIMITER, i++).toInt();
    if(isValidUnsignedByte(tempIntValue)) { paramB = tempIntValue; }


    tempIntValue = getSegmentFromDelimitedString(command, BT_COMMAND_DELIMITER, i++).toInt();
    if(isValidUnsignedByte(tempIntValue)) { paramBrightness = tempIntValue; }


    return true;
  }
  
  return false;
}


// *********************************************************************************
//                         *** Helpers & Validators ***
// *********************************************************************************
String getSegmentFromDelimitedString(String data, char separator, int index) {
  // After splitting da string data based on da separator separator will get da segment at position index
  // index is 0 based

  
  int found      = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex   = data.length() - 1;

  for(int i = 0; i <= maxIndex && found <= index; i++) {
    if(data.charAt(i) == separator || i == maxIndex) {
      found++;

      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

bool isValidUnsignedByte(int value) {
  return value >= 0 && value <= 255;
}


// *********************************************************************************
//                               *** System ***
// *********************************************************************************
void linkModulesParameters() {
  // This way da task 1 & task 2 can communicate

  pParamStyle      = &paramStyle;
  pParamPattern    = &paramPattern;
  pParamR          = &paramR;
  pParamG          = &paramG;
  pParamB          = &paramB;
  pParamBrightness = &paramBrightness;
}

void setDefaultValuesForModulesParameters() {
  // TODO: get it from da EEPROM memory
  // if no values in da EEPROM memory, set a default values for each of 3 modes: riding, resting & stopping

  parseCommandAndSetParameters(DEFAULT_LED_STYLE);
  normalizeModulesParameters();
}

void normalizeModulesParameters() {
  if(*pParamStyle == "PATTERN") {
    // TODO: check if < 0 or > 255 for da brightness

    // Clean unused values
    *pParamR          = 0;
    *pParamG          = 0;
    *pParamB          = 0;

  } else if(*pParamStyle == "COLOR") {
    // TODO: check if < 0 or > 255 for da R, G, B & da brightness

    // Clean unused values
    *pParamPattern    = "";
  }
}


// *********************************************************************************
// *********************************************************************************
void loop() { }
