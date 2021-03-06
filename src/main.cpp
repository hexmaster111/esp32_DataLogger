/*
GPS Dataloger by Hailey Gruszynski
For imbeded systems 2021

NOTES:
  Remove the SDCARD before uploading the sketch or else upload 
  may fail 
@TODO
  Change the logging peramiters to only log if speed is more then x mph
  Add some compiler options to not compile the debug stuff (i forgot what its called)

  Add delay to eject loop (added a at todo)
*/

#include <NMEAGPS.h>
#include <Arduino.h>
#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define VERSION_NUMBER 1.0 //just so i can know on the display

//Uncomment to enable debug messages
#define DEBUG

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define OLED_RESET 99

#define GPSport_h

#ifdef DEBUG
#define DEBUG_PORT Serial
#endif

#define gpsPort GPS_Serial //was GPS_Serial(1)
#define GPS_PORT_NAME "GPS_Serial 1"

//Only needed if your board is irreragluler like mine >.<
#define LED_BUILTIN 23

//-6 for central time zone
#define timeZoneTimeCorrection 6

//How offen to update the display
#define displayUpdateTime 500

//Pin for eject
#define SD_EJECT_PIN 4

//read the IO every how offten (in ms) only used for the eject button rn
#define IO_PULLING_TIME 1000

//Time to wait before rebooting after an eject (IN MS)
#define REBOOT_DELAY_EJECT 20000
//Time to wait in sec of the REBOOT_DELAY_EJECT
#define REBOOT_DELAY_DISPLAY_TIME 20
//Internal Pin for the bat voltage
#define VBAT_PIN 35
//Var used to store battery voltage
float vBatt;
//Var to store the reboot timer, only used in ejection loop
int rebootTimer;

//var to store last io read
unsigned long lastIOReadTime;

//var to store the last time the display was updated in ms
int lastDisplayUpdate;

//var used to store the session number of the logs
int sessionNumber = random(1000000);

// Our path for the log to be written to
String logFilePath = String("/") + String(sessionNumber) + String(".csv");

//IO Name
bool sd_eject;

//so we can know if there is or is not an sdcard :wink:
bool sdIn;

//This switches state every data save
bool dataSaving;

//For determing if we need to write sd data or not
//likely not needed, should be replaced with just a min speed for logging
float lastSavedLat, lastSavedLon, lastSavedAlt;

NMEAGPS gps;
gps_fix fix;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HardwareSerial GPS_Serial(1); // use UART #1

// Set these values to the offset of your timezone from GMT

static const int32_t zone_hours = -timeZoneTimeCorrection; // EST was -6L
static const int32_t zone_minutes = 0L;                    // usually zero
static const NeoGPS::clock_t zone_offset =
    zone_hours * NeoGPS::SECONDS_PER_HOUR +
    zone_minutes * NeoGPS::SECONDS_PER_MINUTE;

// Uncomment one DST changeover rule, or define your own:
#define USA_DST
//#define EU_DST

#if defined(USA_DST)
static const uint8_t springMonth = 3;
static const uint8_t springDate = 14; // latest 2nd Sunday
static const uint8_t springHour = 2;
static const uint8_t fallMonth = 11;
static const uint8_t fallDate = 7; // latest 1st Sunday
static const uint8_t fallHour = 2;
#define CALCULATE_DST

#elif defined(EU_DST)
static const uint8_t springMonth = 3;
static const uint8_t springDate = 31; // latest last Sunday
static const uint8_t springHour = 1;
static const uint8_t fallMonth = 10;
static const uint8_t fallDate = 31; // latest last Sunday
static const uint8_t fallHour = 1;
#define CALCULATE_DST
#endif

void adjustTime(NeoGPS::time_t &dt) //We only use this for the display (24 hr time hard)
{
  NeoGPS::clock_t seconds = dt; // convert date/time structure to seconds

#ifdef CALCULATE_DST
      //  Calculate DST changeover times once per reset and year!
  static NeoGPS::time_t changeover;
  static NeoGPS::clock_t springForward, fallBack;

  if ((springForward == 0) || (changeover.year != dt.year))
  {

    //  Calculate the spring changeover time (seconds)
    changeover.year = dt.year;
    changeover.month = springMonth;
    changeover.date = springDate;
    changeover.hours = springHour;
    changeover.minutes = 0;
    changeover.seconds = 0;
    changeover.set_day();
    // Step back to a Sunday, if day != SUNDAY
    changeover.date -= (changeover.day - NeoGPS::time_t::SUNDAY);
    springForward = (NeoGPS::clock_t)changeover;

    //  Calculate the fall changeover time (seconds)
    changeover.month = fallMonth;
    changeover.date = fallDate;
    changeover.hours = fallHour - 1; // to account for the "apparent" DST +1
    changeover.set_day();
    // Step back to a Sunday, if day != SUNDAY
    changeover.date -= (changeover.day - NeoGPS::time_t::SUNDAY);
    fallBack = (NeoGPS::clock_t)changeover;
  }
#endif

  //  First, offset from UTC to the local timezone
  seconds += zone_offset;

#ifdef CALCULATE_DST
  //  Then add an hour if DST is in effect
  if ((springForward <= seconds) && (seconds < fallBack))
    seconds += NeoGPS::SECONDS_PER_HOUR;
#endif

  dt = seconds; // convert seconds back to a date/time structure
}

void writeFile(fs::FS &fs, String path, const char *message) //used for the inital file write
{
#ifdef DEBUG
  DEBUG_PORT.print("Writing file: ");
  DEBUG_PORT.println(String(path));
#endif

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
#ifdef DEBUG
    DEBUG_PORT.println("Failed to open file for writing");
#endif
    return;
  }
  if (file.print(message))
  {
#ifdef DEBUG
    DEBUG_PORT.println("File written");
#endif
  }
  else
  {
#ifdef DEBUG
    DEBUG_PORT.println("Write failed");
#endif
  }
}

bool blink = true;

void appendFile(fs::FS &fs, String path, String message) //appends data to our file
{
#ifdef DEBUG
  DEBUG_PORT.print("Appending to file:");
  DEBUG_PORT.println(path);
#endif

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
#ifdef DEBUG
    DEBUG_PORT.println("Failed to open file for appending");
#endif
    return;
  }
  if (file.print(message))
  {
#ifdef DEBUG
    DEBUG_PORT.println("Message appended");
#endif
    digitalWrite(LED_BUILTIN, blink);
    blink = !blink;
  }
  else
  {
#ifdef DEBUG
    DEBUG_PORT.println("Append failed");
#endif
  }
}

/*
  This function sets up the display and displays
  the startup screen
*/

void displayInit()
{
  display.setRotation(2);
  display.display();
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Hailey"));
  display.println(F("Gruszynski"));
  display.setTextSize(1);
  display.println(F("Final Project 2021"));
  display.println(F("Imbeded Systems"));
  display.print("v");
  display.print(VERSION_NUMBER);
  display.display();

  delay(3000); //time to show the splash screen

  display.clearDisplay();
  display.setCursor(0, 0);
}

void setup()
{

  pinMode(LED_BUILTIN, OUTPUT); //Used to indicate writing
  pinMode(SD_EJECT_PIN, INPUT); //used to tell the program to be done with the sdcard
  pinMode(VBAT_PIN, INPUT);     //Used for the battery voltage pin

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
#ifdef DEBUG
    DEBUG_PORT.println(F("SSD1306 allocation failed"));
#endif
    for (;;)
      ; // Don't proceed, loop forever
  }

  displayInit(); //Start display and show beginning message
#ifdef DEBUG
  DEBUG_PORT.begin(115200); //Start the serial monitor
  DEBUG_PORT.println("Starting GPS_DATA_LOGGER");
#endif
  GPS_Serial.begin(9600, SERIAL_8N1, GPIO_NUM_34, GPIO_NUM_35); //Start the GPS serial

  // pinMode(15, INPUT_PULLUP); //Uncomment if you have issues getting SPI to work

  SPI.begin(14, 02, 15, 13); //Used to redefine the default SPI pins
                             //Change this to whatever your SPI pins are
                             //remove the numbers here to use default vals

  if (!SD.begin(5)) //if the sdcard fails to mount
  {
    DEBUG_PORT.println("Card Mount Failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("SDCARD MOUNT FAIL");
    display.display();
    display.clearDisplay();
    delay(1000);
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) //if there is no sdcard installed
  {
    sdIn = false;
    DEBUG_PORT.println("SDCARD OUT");
    DEBUG_PORT.println("No SD card attached");
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("NO SDCARD ATTACHED");
    display.display();
    display.clearDisplay();
    delay(1000);
  }
#ifdef DEBUG
  DEBUG_PORT.print("SD Card Type: "); //just for debugging the sdcard
  if (cardType == CARD_MMC)
  {
    DEBUG_PORT.println("MMC");
  }
  else if (cardType == CARD_SD)
  {
    DEBUG_PORT.println("SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    DEBUG_PORT.println("SDHC");
  }
  else
  {
    DEBUG_PORT.println("UNKNOWN");
  }
#endif

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
#ifdef DEBUG
  DEBUG_PORT.printf("SD Card Size: %lluMB\n", cardSize);
#endif
  //Here is where we write the CSV headder line and make our file
  if (cardSize)
  {
    sdIn = true;
#ifdef DEBUG
    DEBUG_PORT.println("SDCARD IN");
    DEBUG_PORT.println(logFilePath);
#endif
    writeFile(SD, logFilePath, "month,day,year,houre,minute,sec,lat,long,alt,speed(mph)\n");
  }
}

//Used to show the AM/PM Indicator on the display
bool isAM(int currentHoure)
{
  if (currentHoure >= 12)
  {
    return false;
  }
  else
  {
    return true;
  }
}

//Used to convert 24 hr to 12 hr lol, had to ask?
int convert24HourTo12(int currentHoure)
{
  if (currentHoure < 13)
  {
    return currentHoure;
  }
  else
  {
    return currentHoure - 12;
  }
}

//where we acutaly get our GPS data and convert it into nice data
// Thank you NEOGPS <3 you da best
void gpsLoop()
{
  while (gps.available(GPS_Serial))
  {
    fix = gps.read();

    if (fix.valid.time && fix.valid.date)
    {
      adjustTime(fix.dateTime);
    }
  }
}

//Where we take our data and display it
//Current time used for display update stuff
//sessionNumber is just so we can know what log number to find
//that drive on
void displayLoop(int currentTime, int sessionNumberForLogs,float battery_voltage)
{
  if (currentTime - lastDisplayUpdate >= displayUpdateTime) //if its time to update display
  {

    if (fix.valid.location) //and if we have a good location
    {
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.print(fix.speed_mph(), 2);
      display.println(" MPH");
      display.setTextSize(1);

      //Display the time on one line with AM/PM indication
      display.print(String(convert24HourTo12(fix.dateTime.hours)) +
                    String(":") +
                    String(fix.dateTime.minutes) +
                    String(":") +
                    String(fix.dateTime.seconds));

      if (isAM(fix.dateTime.hours))
      {
        display.println(" AM");
      }
      else
      {
        display.println(" PM");
      }

      //print the date in mm/dd/yy
      display.println(String(fix.dateTime.month +
                             String("/") + fix.dateTime.date +
                             String("/") + fix.dateTime.year));
      //display the session number
      display.println(String("Session ") + String(sessionNumberForLogs));
      //Logging indicator with blinking lights!!
      if (sdIn)
      {
        display.print("LOGGING");
        if (dataSaving)
        {
          display.print(" ... ");
        }
      }
      else
      {
        display.print("NO SD NOT LOGGING");
      }
      display.setCursor(0,50);
      display.print("Bat:");
      //display.print(map(battery_voltage, 3.2,4.2,0,100));
      display.print(battery_voltage);

      display.display();
      display.clearDisplay(); //do this after so that if i have a status bar it wont clear first
      lastDisplayUpdate = currentTime;
    }
    //If we dont have a GPS Fix display ...
    else
    {
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.println("SEARCHING FOR GPS...");
      display.setCursor(0,50);
      display.print("Bat:");
      display.print(battery_voltage);
      display.display();
      display.clearDisplay();
    }
  }
}
//Where the magic happends babyyyyyy
//not really... We just throw it all in a big old string and write that baby right to
//the sdcard and hope it works :P
void saveLocation(String path)
{
  if ((fix.latitude() != lastSavedLat) || (fix.longitude() != lastSavedLon) || (fix.altitude() != lastSavedAlt))
  {
    //DataFormat
    // month, day, year, hr, min, sec, LAT, LON, ALT, SPEED,\n

    String OutputString =
        String(fix.dateTime.month) +
        String(",") +
        String(fix.dateTime.date) +
        String(",") +
        String(fix.dateTime.year) +
        String(",") +
        String(fix.dateTime.hours) +
        String(",") +
        String(fix.dateTime.minutes) +
        String(",") +
        String(fix.dateTime.seconds) +
        String(",") +
        String(fix.latitude(), 8) +
        String(",") +
        String(fix.longitude(), 8) +
        String(",") +
        String(fix.altitude(), 8) +
        String(",") +
        String(fix.speed_mph(), 2) +
        String(",") + '\n';

    if (sdIn)
    {
      //only if the sdcard is in do we try to write to it
      appendFile(SD, path, OutputString);
      dataSaving = !dataSaving;
    }

    lastSavedLat = fix.latitude();
    lastSavedLon = fix.longitude();
    lastSavedAlt = fix.altitude();
  }
}

//Display stuff for the Ejection loop
void sdEjectScreen(int countDownTime)
{
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.println("SD EJECTED");
  if (countDownTime != -1)
  {
    display.print("Rebooting in: ");
    display.println(countDownTime);
  }
  display.display();
  display.clearDisplay();
}

//ONLY CALL THIS FUNCTION IF ITS TIME TO BE DONE
//This function will intontonly lock up the mcu
//and then call a reboot
void ejectLoop()
{
  while (sd_eject) //while the eject switch held/pressed
  {
    sdEjectScreen(-1); //show just SDEjected
    if (!digitalRead(SD_EJECT_PIN))
    {
      rebootTimer = millis(); // when the reboot button was released
      while (true)
      {
        //@TODO - Add a delay so the screen dont glitch out all the time
        //if button released
        //count up to reboot_delay_eject then reboot
        sdEjectScreen(map(millis() - rebootTimer, 0, REBOOT_DELAY_EJECT, REBOOT_DELAY_DISPLAY_TIME, 0));
        if (millis() - rebootTimer > REBOOT_DELAY_EJECT)
        {
          ESP.restart();
        }
      }
    }
  }
}

//Used to help speed up the program by not reading the buttons every loop through
//we just have to check a bool then and not do a digitalRead
void readIOPerotic(int currentTime)
{
  if (currentTime - lastIOReadTime > IO_PULLING_TIME)
  {
    vBatt = (float)(analogRead(VBAT_PIN)) / 4095 * 2 * 3.3 * 1.1;
    /*
  The ADC value is a 12-bit number, so the maximum value is 4095 (counting from 0).
  To convert the ADC integer value to a real voltage you???ll need to divide it by the maximum value of 4095,
  then double it (note above that Adafruit halves the voltage), then multiply that by the reference voltage of the ESP32 which 
  is 3.3V and then vinally, multiply that again by the ADC Reference Voltage of 1100mV.
*/

    sd_eject = digitalRead(SD_EJECT_PIN);

#ifdef DEBUG
    DEBUG_PORT.print("Vbat = ");
    DEBUG_PORT.print(vBatt);
    DEBUG_PORT.println(" Volts");
#endif
    lastIOReadTime = currentTime;
  }
  if (sd_eject)
  {
    ejectLoop();
  }
}

//Where the program dose the busness
void loop()
{
  //unsigned long loopTimer = micros(); //un comment to time the loop

  unsigned long currentTime = millis(); //get the current time

  readIOPerotic(currentTime);              //check the io if we need
  gpsLoop();                               //Check if the gps is saying anything funny
  displayLoop(currentTime, sessionNumber, vBatt); //run the display

  if (fix.valid.location & !sd_eject) //if we have a valid location and we are not ejecting
                                      //the sdcard
  {
    saveLocation(logFilePath);
  }
}
//DEBUG_PORT.println(String("LoopTime: ") + String(micros() - loopTimer)); //for checking loop time
