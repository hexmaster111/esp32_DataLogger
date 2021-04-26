#include <NMEAGPS.h>
#include <Arduino.h>

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

#define LED_BUILTIN 23

float lastLat;
float lastLon;

NMEAGPS gps;
gps_fix fix;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

HardwareSerial GPS_Serial(1); // use UART #1

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
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
  DEBUG_PORT.println("Serial Armed hehehe");
  GPS_Serial.begin(9600, SERIAL_8N1, GPIO_NUM_12, GPIO_NUM_15);
}
void loop()
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
