//Author: Moragor
//Version: 1.1.0
//Created: 19-Dec-2017
//Last edited: 15-Jan-2020
//License: MIT license (X11, see bottom)


#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128        // OLED display width, in pixels
#define SCREEN_HEIGHT 64        // OLED display height, in pixels
#define OLED_MOSI   11          //The OLED display library uses software serial, so you can chose whatever pins you want
#define OLED_CLK   12
#define OLED_DC    9
#define OLED_CS    8
#define OLED_RESET 10
#define buttonPin1 7
#define buttonPin2 6
#define mosfetPin 5
#define logInterval  1000       //The time between measurements
#define sdInterval 15000        //The time between datapoints collected for the SD card
#define syncInterval 180000     //How often data will be written to the SD card
#define SD_CS 42
bool buttonState;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
float current_mA = 0;
float power_mW = 0;
float loadvoltage = 0;
float capacity_mAh = 0;
float capacity_mWh = 0;
int cycleMillis = 0;
unsigned long prevMillis = 0;
unsigned long startMillis;
byte sPassed = 0;
byte minPassed = 0;
byte hPassed = 0;
float endVoltage;
byte mode = 1;
unsigned long syncTime = 0;
unsigned long sdTime = 0;
bool sdMissing = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
Adafruit_INA219 ina219;
File logfile;

void setup()
{
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);     //This pin powers the OLED display which apparently only draws very low current (might vary between models)
  pinMode(buttonPin1, INPUT_PULLUP);
  pinMode(buttonPin2, INPUT_PULLUP);
  pinMode(mosfetPin, OUTPUT);
  //  pinMode(SD_CS, OUTPUT);
  digitalWrite(mosfetPin, LOW);
  display.begin(SSD1306_SWITCHCAPVCC);
  if (!SD.begin(SD_CS)) {
    sdMissing = 1;            //Checking if a SD card is present
  }
  ina219.begin();     // Initialize the INA219. By default the initialization will use the largest range (32V, 2A).
  // To use a slightly lower 32V, 1A range (higher precision on amps):
  //ina219.setCalibration_32V_1A();
  // Or to use a lower 16V, 400mA range (higher precision on volts and amps):
  ina219.setCalibration_16V_400mA();
  testMode();     //Starting the mode selection subroutine (I always wanted to use that word :D)
  testBattery();  //Starting the battery test
}

void loop()
{
  //nuffing
}

void testMode()
{
  while (1) {     //creates an infinite loop which is exited with "break"
    modeDisplay();      //See the modeDisplay function bellow
    int reading = digitalRead(buttonPin1);      //Debouncing the mode button. See File->Examples->02.Digital->Debounce
    if (reading != lastButtonState) {
      lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > 50) {

      if (reading != buttonState) {
        buttonState = reading;

        if (buttonState == LOW) {
          mode++;                               //Button1 switches between test modes for different battery types
          if (mode >= 4)
          {
            mode = 1;
          }
        }
      }
    }
    lastButtonState = reading;

    if (digitalRead (buttonPin2) == LOW)      //No debounce needed here. Button2 confirmes the test mode by assigning the end voltage to its variable. The while loop ends with break which ends the testMode function. testBattery is now called.
    {
      if (mode == 1)
      {
        endVoltage = 0.9;     //Alkaline batteries
        break;
      }
      else if (mode == 2)
      {
        endVoltage = 1.0;     //NiMh batteries
        break;
      }
      else
      {
        endVoltage = 2.8;     //LiPo batteries (one cell)
        break;
      }
    }
  }
}

void testBattery()
{
  digitalWrite(mosfetPin, HIGH);          //The mosfet prevents battery discharge before and after the actual test
  loadvoltage = (ina219.getBusVoltage_V() + (ina219.getShuntVoltage_mV() / 1000));      //This tests if the battery is actually charged enough for the test.
  if (endVoltage >= loadvoltage)
  {
    digitalWrite(mosfetPin, LOW);
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println();
    display.println(F("  Battery"));
    display.print(F("    low!"));
    display.display();
    return;                     //This exits the testBattery function and starts the empty loop preventing tests on an empty battery
  }
  else
  {
    if (!sdMissing) {             //The "!" makes something negative. In this case it means "if SD is not missing" or "sdMissing == 0".
      logInit();                  //This function creates a new logfile on the SD card if it's present
    }
    startMillis = millis();     //This records the time the actual test starts.
    prevMillis = startMillis;   //prevMillis is the time the last measuremant was taken
    while (loadvoltage > endVoltage) {
      cycleMillis = millis() - prevMillis;     //cycleMillis is the time the last cycle took
      prevMillis = millis();
      current_mA = ina219.getCurrent_mA();
      loadvoltage = (ina219.getBusVoltage_V() + (ina219.getShuntVoltage_mV() / 1000));
      power_mW = current_mA * loadvoltage;
      capacity_mAh = capacity_mAh + (current_mA * cycleMillis / 3600000.0);     //The capacity is added up durring the test
      capacity_mWh = capacity_mWh + (current_mA * loadvoltage * cycleMillis / 3600000.0);     //3600000 is the number of milliseconds in a hour
      sPassed = ((prevMillis - startMillis) / 1000) % 60;     //For the % operation see https://www.arduino.cc/reference/en/language/structure/arithmetic-operators/remainder/
      minPassed = ((prevMillis - startMillis) / 60000) % 60;
      hPassed = ((prevMillis - startMillis) / 3600000);

      testDisplay();      //This function displays the collected data on the OLED screen
      if ((millis() >= sdTime + sdInterval && !sdMissing) || (sdTime == 0 && !sdMissing)) {     //This writes a datapoint to the SD card every "sdInterval" milliseconds if a card is present. sdTime == 0 is for the very first datapoint at 0 seconds.
        logLog();                                                                               //Initially I recorded a datapoint every cycle, but this lead to an unnecessarily high ammount of datapoints
        sdTime = millis();
      }

      delay((logInterval - 1) - (millis() % logInterval));      //This little calculation makes sure a cycle always takes the same ammount of time
    }
  }
  if (!sdMissing) {                       //As soon as the end voltage is reached and the while loop ends a last datapoint is recorded if a SD card is present
    logLog();
  }
  digitalWrite(mosfetPin, LOW);           //The mosfet opens to prevent overdischarge
  logfile.close();                        //The logfile on the SD card is saved and closed
  display.setTextSize(1);                 //A text on the OLED indicates the end of the test. The display id not cleared, so the last measurment stays displayed.
  display.setCursor(0, 57);
  display.println(F("Test finished"));
  display.display();
}

void modeDisplay()        //This is the display for the test mode selection (battery type)
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(F("SelectMode"));
  display.setTextSize(1);
  if (mode == 1) {
    display.setTextColor(BLACK, WHITE);     //(BLACK, WHITE) highlights the background of the text to show which mode is selected
  }
  else {
    display.setTextColor(WHITE);
  }
  display.println(F("Alkaline Bat. (1.5V) "));      //With F() the text isn't loaded into ram which is nice if you're short on ram
  if (mode == 2) {
    display.setTextColor(BLACK, WHITE);
  }
  else {
    display.setTextColor(WHITE);
  }
  display.println(F("NiMh Battery (1.2V)  "));
  if (mode == 3) {
    display.setTextColor(BLACK, WHITE);
  }
  else {
    display.setTextColor(WHITE);
  }
  display.println(F("LiIon Bat. (3.6-3.8V)"));
  if (sdMissing) {
    display.setTextColor(WHITE);
    display.setCursor(0, 57);
    display.print(F("No SD card!"));
  }
  display.display();
}

void testDisplay()                  //This function displays the collected data on the OLED during the actual test
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print(F("Capacity "));
  display.print(capacity_mAh);
  display.println(F("mAh"));
  display.print(F("Energy "));
  display.setCursor(54, 8);
  display.print(capacity_mWh);
  display.println(F("mWh"));
  display.print(F("Voltage "));
  display.setCursor(54, 16);
  display.print(loadvoltage);
  display.println(F("V"));
  display.print(F("Current "));
  display.setCursor(54, 24);
  display.print(current_mA);
  display.println(F("mA"));
  display.print(F("Power "));
  display.setCursor(54, 32);
  display.print(power_mW);
  display.println(F("mW"));
  display.print(F("Time "));
  display.setCursor(54, 40);
  display.print(hPassed);
  display.print(F(":"));
  if (minPassed < 10) {             //This adds a leading zero if the value is bellow 10
    display.print(F("0"));
  }
  display.print(minPassed);
  display.print(F(":"));
  if (sPassed < 10) {
    display.print(F("0"));
  }
  display.print(sPassed);
  if (sdMissing) {
    display.setCursor(0, 49);
    display.print(F("No SD card!"));
  }
  display.display();
}

void logInit()
{
  // create a new logfile
  char filename[] = "LOGGER00.CSV";
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i / 10 + '0';           //These two lines replace the 00 in LOGGER00.CSV with rising numbers if a file already exists
    filename[7] = i % 10 + '0';           //This is waaaay too complicated for me, so thanks Adafruit.
    if (! SD.exists(filename)) {          // only open a new file if it doesn't exist
      logfile = SD.open(filename, FILE_WRITE);
      break;  // leave the loop!
    }
  }
  logfile.println(F("millis,voltage,current,power,capacity,energy"));     //This writes the first line to the logfile to name the columns
  logfile.flush();        //This command actually saves the data on the SD card
}

void logLog()     //This function saves the collected data on the SD card
{
  logfile.print(prevMillis - startMillis);        // milliseconds since start
  logfile.print(", ");                            //A CSV file is a text file with Comma Separated Values. A spreadsheet program will assign every comma separated value to a new column.
  logfile.print(loadvoltage);
  logfile.print(", ");
  logfile.print(current_mA);
  logfile.print(", ");
  logfile.print(power_mW);
  logfile.print(", ");
  logfile.print(capacity_mAh);
  logfile.print(", ");
  logfile.print(capacity_mWh);

  logfile.println();                              //A line break will start a new row in a spreadsheet

  if ((millis() - syncTime) < syncInterval) return;
  syncTime = millis();
  logfile.flush();
}



/*Copyright (c) 2020 Fabian Hunziker "Moragor"

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do
  so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
  FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */
