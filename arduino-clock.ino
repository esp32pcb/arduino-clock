/*
 * Arduino based Firmware for ESP32 custom board https://github.com/esp32pcb/hodiny .
 * Shows digital clock on 4x8x8 matrix display.
 * Tested on Arduino v1.8.19, eps32 v2.0.3 https://github.com/espressif/arduino-esp32 Board: ESP32 Dev Module
 * Upload speed: 921600, CPU Freq: 240MHz, Flash Freq: 80MHz
 * During programming phase ........ button BOOT needs to be pressed for a while.
 * 
 * Licence: MIT, author: Vaclav Juchelka vjuchelka@gmail.com
 * This code for Arduino was mostly created by ChatGPT4.
 * 
 * This software is provided "as is", without warranty of any kind, express or implied, including but not limited to the 
 * warranties of merchantability, fitness for a particular purpose and noninfringement. In no event shall the authors or 
 * copyright holders be liable for any claim, damages or other liability, whether in an action of contract, tort or 
 * otherwise, arising from, out of or in connection with the software or the use or other dealings in the software.
 * 
 * The user acknowledges and agrees that all use of this software is at their own risk. The software is provided free of 
 * charge and, as such, the developer makes no guarantees or promises regarding its functionality, stability, or suitability 
 * for any particular purpose. The user is solely responsible for any and all consequences of using this software, 
 * including but not limited to any damage or data loss.
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <MD_Parola.h>    // https://github.com/MajicDesigns/MD_Parola        MD_Parola   3.7.0   by marco_c
#include <MD_MAX72xx.h>   // https://github.com/MajicDesigns/MD_MAX72XX       MD_MAX72XX  3.4.1   by marco_c
#include <NTPClient.h>    // https://github.com/arduino-libraries/NTPClient   NTPClient   3.2.1   by Fabrice Weinberg
#include <TimeLib.h>      // http://playground.arduino.cc/Code/Time/          Time        1.6.1   by Paul Stoffregen

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN   26  // SCK
#define DATA_PIN  25  // MOSI
#define CS_PIN    27  // SS
#define UPDATE_TIME_INTERVAL 60000 
#define UPDATE_DISPLAY_INTERVAL 500

MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

const char* ssid     = "SSID"; //Fill in YOUR network SSID
const char* password = "PASS"; //Fill in YOUT network Password

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 0, UPDATE_TIME_INTERVAL);

unsigned long prevMillisUpdateTime = 0;
unsigned long prevMillisUpdateDisplay = 0;
bool colon = false;

void setup() {
  Serial.begin(9600);
  myDisplay.begin();
  myDisplay.setIntensity(0);
  myDisplay.displayClear();
  Serial.println("My display cleared");

  Serial.print("WiFi connection:");
  WiFi.begin(ssid, password);
  while ( WiFi.status() != WL_CONNECTED ) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("WiFi connected");

  timeClient.begin();
  Serial.println("NTP started");
  timeClient.update();
  adjustTimeZone();
}

void loop() {
  if (millis() - prevMillisUpdateTime > UPDATE_TIME_INTERVAL || prevMillisUpdateTime == 0) {
    Serial.println("Update time");
    prevMillisUpdateTime = millis();
    timeClient.update();
    adjustTimeZone();
  }
  
  if (millis() - prevMillisUpdateDisplay > UPDATE_DISPLAY_INTERVAL || prevMillisUpdateDisplay == 0) {
    Serial.println("Update display");
    prevMillisUpdateDisplay = millis();

//    String timeStr = String(hour()) + ((colon = !colon) ? ":" : " ") + twoDigits(minute());
    String timeStr = String(hour()) + (":") + twoDigits(minute()); // I don't like colon to blink
  
    displayTime(timeStr);

    Serial.println("DisplayTime");
  }
}

void displayTime(String timeStr) {
  myDisplay.setZoneEffect(0, true, PA_FLIP_LR);
  myDisplay.setZoneEffect(0, true, PA_FLIP_UD);
  myDisplay.setTextAlignment(PA_CENTER);
  myDisplay.print(timeStr);
}


// Function to find the last Sunday of a given month of a given year
int findLastSunday(int year, int month) {
  // The day of the week for the first day of the month (0 = Sunday, 1 = Monday, ..., 6 = Saturday)
  // tm_wday from mktime will give us the day of the week for the 1st of the month
  struct tm timeStruct = {};
  timeStruct.tm_year = year - 1900; // Year since 1900
  timeStruct.tm_mon = month - 1;    // Month of the year (0-11, January = 0)
  timeStruct.tm_mday = 1;           // Day of the month (1-31)
  timeStruct.tm_hour = 0;
  timeStruct.tm_min = 0;
  timeStruct.tm_sec = 0;
  timeStruct.tm_isdst = -1;         // Not considering daylight saving time
  mktime(&timeStruct);

  // Find day of the week for the 1st of the month
  int firstDay = timeStruct.tm_wday;

  // Calculate the day of the last Sunday
  int daysInMonth = 31 - ((month == 4 || month == 6 || month == 9 || month == 11) ? 1 : (month == 2 ? (3 - (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) : 0));
  int day = daysInMonth - ((firstDay + daysInMonth - 1) % 7);
  return day;
}

void adjustTimeZone() {
  time_t rawTime = timeClient.getEpochTime();
  setTime(rawTime);
  
  int currentYear = year();
  int currentMonth = month();
  int currentDay = day();
  int currentWeekday = weekday();
  
  Serial.println("CurrentYear:");
  Serial.println(currentYear);
  
  Serial.println("CurrentMonth:");
  Serial.println(currentMonth);
  
  Serial.println("CurrentDay:");
  Serial.println(currentDay);
  
  Serial.println("CurrentWeekday:");
  Serial.println(currentWeekday);
  
  
  int lastSundayMarch = findLastSunday(currentYear, 3);
  int lastSundayOctober = findLastSunday(currentYear, 10);
  
  Serial.println("lastSundayMarch:");
  Serial.println(lastSundayMarch);
  
  Serial.println("lastSundayOctober:");
  Serial.println(lastSundayOctober);
  
  long timeZoneOffset = 3600; // Default time zone offset for your location in seconds
  if (currentMonth > 3 && currentMonth < 10) {
      timeZoneOffset += 3600; // Add an hour for daylight saving time
  } else if (currentMonth == 3 && currentDay >= lastSundayMarch) {
      timeZoneOffset += 3600; // Add an hour for daylight saving time if past last Sunday of March
  } else if (currentMonth == 10 && currentDay < lastSundayOctober) {
      timeZoneOffset += 3600; // Still on daylight saving time if before last Sunday of October
  }
  
  timeClient.setTimeOffset(timeZoneOffset);
}

String twoDigits(int number) {
  if (number < 10) {
    return "0" + String(number);
  }
  return String(number);
}
