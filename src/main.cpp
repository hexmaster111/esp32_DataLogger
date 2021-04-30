/*
GPS Dataloger by Hailey Gruszynski
For imbeded systems 2021

NOTES:
  Remove the SDCARD before uploading the sketch or else upload 
  may fail 
*/

#include <NMEAGPS.h>
#include <Arduino.h>
#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define VERSION_NUMBER 0.2 //just so i can know on the display

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define OLED_RESET 99

#define GPSport_h
#define DEBUG_PORT Serial

#define gpsPort GPS_Serial //was GPS_Serial(1)
#define GPS_PORT_NAME "GPS_Serial 1"
//Only needed if your board is irreragluler like mine >.<
#define LED_BUILTIN 23

//-6 for central time zone
#define timeZoneTimeCorrection 6

//How offen to update the display
#define displayUpdateTime 500

//var to store the last time the display was updated in ms
int lastDisplayUpdate;

//var used to store the session number of the logs
int sessionNumber = random(1000000);
// Our path for the log to be written to
String logFilePath = String("/") + String(sessionNumber) + String(".txt");

//so we can know if there is or is not an sdcard :wink:
bool sdIn;

//Just a switch for the indicator on the display
bool dataSaving;

//For determing if we need to write sd data or not
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

void adjustTime(NeoGPS::time_t &dt)
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

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  DEBUG_PORT.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    DEBUG_PORT.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    DEBUG_PORT.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      DEBUG_PORT.print(" DIR : ");
      DEBUG_PORT.println(file.name());
      if (levels)
      {
        listDir(fs, file.name(), levels - 1);
      }
    }
    else
    {
      DEBUG_PORT.print(" FILE: ");
      DEBUG_PORT.print(file.name());
      DEBUG_PORT.print(" SIZE: ");
      DEBUG_PORT.println(file.size());
    }
    file = root.openNextFile();
  }
}

void createDir(fs::FS &fs, const char *path)
{
  DEBUG_PORT.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path))
  {
    DEBUG_PORT.println("Dir created");
  }
  else
  {
    DEBUG_PORT.println("mkdir failed");
  }
}

void removeDir(fs::FS &fs, const char *path)
{
  DEBUG_PORT.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path))
  {
    DEBUG_PORT.println("Dir removed");
  }
  else
  {
    DEBUG_PORT.println("rmdir failed");
  }
}

void readFile(fs::FS &fs, const char *path)
{
  DEBUG_PORT.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file)
  {
    DEBUG_PORT.println("Failed to open file for reading");
    return;
  }

  DEBUG_PORT.print("Read from file: ");
  while (file.available())
  {
    DEBUG_PORT.write(file.read());
  }
}

void writeFile(fs::FS &fs, String path, const char *message)
{
  DEBUG_PORT.print("Writing file: ");
  DEBUG_PORT.println(String(path));

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    DEBUG_PORT.println("Failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    DEBUG_PORT.println("File written");
  }
  else
  {
    DEBUG_PORT.println("Write failed");
  }
}

void appendFile(fs::FS &fs, String path, String message)
{
  DEBUG_PORT.print("Appending to file:");
  DEBUG_PORT.println(path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    DEBUG_PORT.println("Failed to open file for appending");
    return;
  }
  if (file.print(message))
  {
    DEBUG_PORT.println("Message appended");
  }
  else
  {
    DEBUG_PORT.println("Append failed");
  }
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
  DEBUG_PORT.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2))
  {
    DEBUG_PORT.println("File renamed");
  }
  else
  {
    DEBUG_PORT.println("Rename failed");
  }
}

void deleteFile(fs::FS &fs, const char *path)
{
  DEBUG_PORT.printf("Deleting file: %s\n", path);
  if (fs.remove(path))
  {
    DEBUG_PORT.println("File deleted");
  }
  else
  {
    DEBUG_PORT.println("Delete failed");
  }
}

void testFileIO(fs::FS &fs, const char *path)
{
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file)
  {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len)
    {
      size_t toRead = len;
      if (toRead > 512)
      {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    DEBUG_PORT.printf("%u bytes read for %u ms\n", flen, end);
    file.close();
  }
  else
  {
    DEBUG_PORT.println("Failed to open file for reading");
  }

  file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    DEBUG_PORT.println("Failed to open file for writing");
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++)
  {
    file.write(buf, 512);
  }
  end = millis() - start;
  DEBUG_PORT.printf("%u bytes written for %u ms\n", 2048 * 512, end);
  file.close();
}

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

  delay(1000);

  display.clearDisplay();
  display.setCursor(0, 0);
}

void setup()
{

  // pinMode(LED_BUILTIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    DEBUG_PORT.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  displayInit();

  DEBUG_PORT.begin(115200);
  DEBUG_PORT.println("Starting GPS_DATA_LOGGER");

  GPS_Serial.begin(9600, SERIAL_8N1, GPIO_NUM_34, GPIO_NUM_35);

  // pinMode(15, INPUT_PULLUP); //Uncomment if you have issues getting SPI to work

  SPI.begin(14, 02, 15, 13); //Used to redefine the default SPI pins
                             //Change this to whatever your SPI pins are
                             //remove the numbers here to use default vals

  if (!SD.begin(5))
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

  if (cardType == CARD_NONE)
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

  DEBUG_PORT.print("SD Card Type: ");
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

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  DEBUG_PORT.printf("SD Card Size: %lluMB\n", cardSize);
  if (cardSize)
  {
    sdIn = true;
    DEBUG_PORT.println("SDCARD IN");
    DEBUG_PORT.println(logFilePath);
    writeFile(SD, logFilePath, "--FileStart--");
  }

  // listDir(SD, "/", 0); //leaving this here so i know what things do
  // createDir(SD, "/mydir");
  // listDir(SD, "/", 0);
  // removeDir(SD, "/mydir");
  // listDir(SD, "/", 2);
  // writeFile(SD, "/hello.txt", "Hello ");
  // appendFile(SD, "/hello.txt", "World!\n");
  // readFile(SD, "/hello.txt");
  // deleteFile(SD, "/foo.txt");
  // renameFile(SD, "/hello.txt", "/foo.txt");
  // readFile(SD, "/foo.txt");
}

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

void displayLoop(int currentTime, int sessionNumberForLogs)
{
  if (fix.valid.location)
  {
    if (currentTime - lastDisplayUpdate >= displayUpdateTime)
    {
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.print(fix.speed_mph(), 2);
      display.println(" MPH");

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
      display.setTextSize(1);
      //print the date in mm/dd/yy
      display.println(String(fix.dateTime.month + String("/") + fix.dateTime.date + String("/") + fix.dateTime.year));
      //display the session number
      display.println(String("Session ") + String(sessionNumberForLogs));

      if (sdIn)
      {
        display.print("SD OK");
        if (dataSaving)
        {
          display.print(" ... ");
        }
      }
      else
      {
        display.print("NO SD NOT LOGGING");
      }

      display.display();
      display.clearDisplay(); //do this after so that if i have a status bar it wont clear first
      lastDisplayUpdate = currentTime;
    }
  }
}

void saveLocation(String path)
{
  if ((fix.latitude() != lastSavedLat) || (fix.longitude() != lastSavedLon) || (fix.altitude() != lastSavedAlt))
  {
    //DataFormat
    // month, day, year, hr, min, sec, LAT, LON, ALT, SPEED,

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
      appendFile(SD, path, OutputString);
    }

    dataSaving = !dataSaving;

    lastSavedLat = fix.latitude();
    lastSavedLon = fix.longitude();
    lastSavedAlt = fix.altitude();
  }
}

void loop()
{
  int currentTime = millis();

  gpsLoop();
  displayLoop(currentTime, sessionNumber);
  saveLocation(logFilePath);
}
