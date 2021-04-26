//Working SDCard IO
#include <NMEAGPS.h>
#include <Arduino.h>
#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define OLED_RESET 99

#define GPSport_h
#define DEBUG_PORT Serial

#define gpsPort GPS_Serial(1)
#define GPS_PORT_NAME "GPS_Serial 1"
//Only needed if your board is irreragluler like mine >.<
#define LED_BUILTIN 23

NMEAGPS gps;
gps_fix fix;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HardwareSerial GPS_Serial(1); // use UART #1

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

void writeFile(fs::FS &fs, const char *path, const char *message)
{
  DEBUG_PORT.printf("Writing file: %s\n", path);

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

void appendFile(fs::FS &fs, const char *path, const char *message)
{
  DEBUG_PORT.printf("Appending to file: %s\n", path);

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

void setup()
{

  pinMode(LED_BUILTIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    DEBUG_PORT.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

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
  display.display();

  delay(100);

  display.clearDisplay();
  display.setCursor(0, 0);

  DEBUG_PORT.begin(115200);
  DEBUG_PORT.println("Starting GPS_DATA_LOGGER");

  pinMode(15, INPUT_PULLUP);
  SPI.begin(14,02,15,13);
  if (!SD.begin(5))
  {
    DEBUG_PORT.println("Card Mount Failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("SDCARD MOUNT FAIL");
    display.display();
    delay(1000);
  }
  GPS_Serial.begin(9600, SERIAL_8N1, GPIO_NUM_12, GPIO_NUM_15);

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE)
  {
    DEBUG_PORT.println("No SD card attached");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("NO SDCARD ATTACHED");
    display.display();
    delay(1000);
    return;
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

  // listDir(SD, "/", 0);
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
  // DEBUG_PORT.println("Testing SDCard...");
  // testFileIO(SD, "/test.txt");
}

void gpsLoop()
{
  while (gps.available(GPS_Serial))
  {
    fix = gps.read();
  }

  if (fix.valid.location)
  {
    DEBUG_PORT.print(fix.latitude(), 6);
    DEBUG_PORT.print(',');
    DEBUG_PORT.print(fix.longitude(), 6);
    DEBUG_PORT.println();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("lat:");
    display.println(fix.latitude(), 8);
    display.print("lon:");
    display.println(fix.longitude(), 8);
    display.print("alt:");
    display.println(fix.altitude(), 8);
    display.setTextSize(2);
    display.print(fix.speed_mph(), 2);
    display.println(" MPH");
    display.display();
    display.clearDisplay();
  }
}

void loop()
{
  gpsLoop();
}