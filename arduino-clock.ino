/*
 * Arduino based Firmware for ESP32 custom board https://github.com/esp32pcb/hodiny .
 * Shows digital clock on 4x8x8 matrix display.
 * Tested with the esp32 core https://github.com/espressif/arduino-esp32 on 2.0.3 and 3.3.8.
 * Board: ESP32 Dev Module. Upload speed: 921600, CPU Freq: 240MHz, Flash Freq: 80MHz
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
#include <MD_Parola.h>    // https://github.com/MajicDesigns/MD_Parola        MD_Parola   3.7.0   by marco_c
#include <MD_MAX72xx.h>   // https://github.com/MajicDesigns/MD_MAX72XX       MD_MAX72XX  3.4.1   by marco_c
#include <esp_sntp.h>     // sync notification callback, so we know when time last arrived

#include "secrets.h"      // your WiFi credentials, not in git -- see the README

// ---------------------------------------------------------------------------
// Configuration -- this is the part you edit. WiFi credentials live in
// secrets.h, see the README.
// ---------------------------------------------------------------------------

// Your timezone, as a POSIX TZ string. Default is Europe/Prague.
//
// Careful, POSIX inverts the sign: "CET-1" means UTC+1, not UTC-1. "M3.5.0" is
// "last Sunday of March", "M10.5.0/3" is "last Sunday of October at 03:00".
// Getting this string right is the whole daylight-saving story -- the C library
// applies the rule, including the fact that the EU switches at 01:00 UTC rather
// than at some hour of local midnight.
//
//   Prague, Berlin, Paris, Madrid, Rome   CET-1CEST,M3.5.0,M10.5.0/3
//   London, Dublin, Lisbon                GMT0BST,M3.5.0/1,M10.5.0
//   Helsinki, Athens, Kyiv                EET-2EEST,M3.5.0/3,M10.5.0/4
//   New York, Toronto                     EST5EDT,M3.2.0,M11.1.0
//   Los Angeles, Vancouver                PST8PDT,M3.2.0,M11.1.0
//   UTC, no daylight saving at all        UTC0
//
// Any other place: https://github.com/nayarsystems/posix_tz_db
#define TZ_STRING  "CET-1CEST,M3.5.0,M10.5.0/3"

// Pick a pool close to you: europe, north-america, asia, oceania...
#define NTP_SERVER "europe.pool.ntp.org"

// How long the clock may go without hearing from NTP before it stops showing
// the time at all. Two hours is eight missed polls in a row.
#define STALE_AFTER (2UL * 60 * 60 * 1000)

// Display: four 8x8 MAX7219 modules. FC16_HW suits the common "4-in-1" boards;
// if your columns come out scrambled, try PAROLA_HW or GENERIC_HW.
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN   26  // SCK
#define DATA_PIN  25  // MOSI
#define CS_PIN    27  // SS

// Set to 1 to check the timezone instead of running the clock. Needs no
// network: it sets the clock by hand to the four instants either side of both
// daylight-saving switches and prints what your TZ_STRING made of them.
#define TZ_SELFTEST 0

// ---------------------------------------------------------------------------
// Internals below -- no need to touch these.
// ---------------------------------------------------------------------------

#define UPDATE_DISPLAY_INTERVAL 500
#define LOG_INTERVAL 15000
#define SNTP_POLL_INTERVAL (15UL * 60 * 1000)

// WiFi.reconnect() tears down whatever association is in progress, so calling it
// on a timer fights the driver instead of helping it: measured 45 s of thrashing
// on a boot that otherwise associates in 15. Look often, but only intervene once
// the link has been down long enough that the driver's own retry has clearly
// given up, and then no more than once a minute.
#define WIFI_CHECK_INTERVAL  5000
#define WIFI_DOWN_GRACE     30000
#define WIFI_RETRY_INTERVAL 60000

MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

unsigned long prevMillisUpdateDisplay = 0;
unsigned long prevMillisLog = 0;
unsigned long prevMillisWifi = 0;
unsigned long lastConnectedMs = 0;   // 0 at boot buys the first association its grace period
unsigned long lastRetryMs = 0;

// Written from the SNTP task, read from loop(). Aligned 32-bit words, so a torn
// read is not possible on this target; volatile is enough to stop the compiler
// hoisting them out of the freshness check.
volatile unsigned long lastSyncMs = 0;
volatile bool everSynced = false;

// Called by SNTP whenever it actually got an answer. This is the only evidence
// that the clock is still tied to something real -- WiFi being associated says
// nothing about whether NTP replies are getting through.
void onTimeSync(struct timeval *tv) {
  (void)tv;
  lastSyncMs = millis();
  everSynced = true;
}

// The clock is trustworthy only if SNTP has answered at least once and did so
// recently. millis() wraps after ~49 days; the unsigned subtraction is correct
// across the wrap, which a comparison against an absolute deadline would not be.
bool timeIsFresh() {
  return everSynced && (millis() - lastSyncMs) < STALE_AFTER;
}

void setup() {
  Serial.begin(115200);   // the ESP32 boot ROM logs at 115200 anyway
  myDisplay.begin();
  myDisplay.setIntensity(0);
  myDisplay.displayClear();
  Serial.println("My display cleared");

#if TZ_SELFTEST
  runTzSelfTest();
  return;                 // no WiFi, no clock -- the test is the whole run
#endif

  // Deliberately not blocking here. A clock that refuses to boot without WiFi
  // is useless exactly when you need to see that something is wrong -- it must
  // come up, show --:--, and keep trying.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);

  // Both have to be set before configTzTime(), which is what starts SNTP.
  sntp_set_sync_interval(SNTP_POLL_INTERVAL);
  sntp_set_time_sync_notification_cb(onTimeSync);

  // Sets TZ and starts SNTP in one call. From here on localtime() already
  // carries the daylight-saving offset -- nothing left to adjust by hand.
  configTzTime(TZ_STRING, NTP_SERVER);
  Serial.printf("[time] ntp %s, tz %s, stale after %lu min\n",
                NTP_SERVER, TZ_STRING, STALE_AFTER / 60000);
}

void loop() {
#if TZ_SELFTEST
  delay(1000);
  return;
#endif

  if (millis() - prevMillisUpdateDisplay > UPDATE_DISPLAY_INTERVAL || prevMillisUpdateDisplay == 0) {
    prevMillisUpdateDisplay = millis();

    struct tm now;
    if (timeIsFresh() && getLocalTime(&now, 0)) {
      String timeStr = String(now.tm_hour) + (":") + twoDigits(now.tm_min); // I don't like colon to blink
      displayTime(timeStr);
    } else {
      // Never synced, or not synced for STALE_AFTER. Show dashes and no digits
      // at all: the RTC keeps counting through an outage, so it would happily
      // display a plausible wrong time, and a plausible wrong time on a clock
      // is worse than no time. Steady, never blinking.
      displayTime("--:--");
    }
  }

  if (millis() - prevMillisWifi > WIFI_CHECK_INTERVAL) {
    prevMillisWifi = millis();
    if (WiFi.status() == WL_CONNECTED) {
      lastConnectedMs = millis();
    } else if (millis() - lastConnectedMs > WIFI_DOWN_GRACE &&
               millis() - lastRetryMs > WIFI_RETRY_INTERVAL) {
      lastRetryMs = millis();
      Serial.println("[wifi] down too long, forcing reconnect");
      WiFi.reconnect();
    }
  }

  if (millis() - prevMillisLog > LOG_INTERVAL || prevMillisLog == 0) {
    prevMillisLog = millis();
    logStatus();
  }
}

// One line every LOG_INTERVAL. Prints the zone abbreviation, so a wrong TZ
// string shows up as CET in the middle of summer instead of hiding, and how
// long ago SNTP last answered, so a stale display can be told apart from a
// broken one without guessing.
void logStatus() {
  bool up = WiFi.status() == WL_CONNECTED;
  char when[32];
  if (everSynced) {
    snprintf(when, sizeof(when), "%lus ago", (millis() - lastSyncMs) / 1000);
  } else {
    snprintf(when, sizeof(when), "never");
  }

  struct tm now;
  char stamp[48] = "--";
  if (getLocalTime(&now, 0)) {
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S %Z", &now);
  }

  Serial.printf("[status] %s  sync=%s %s  wifi=%s rssi=%d\n",
                stamp, when, timeIsFresh() ? "fresh" : "STALE",
                up ? "up" : "down", up ? WiFi.RSSI() : 0);
}

void displayTime(String timeStr) {
  myDisplay.setZoneEffect(0, true, PA_FLIP_LR);
  myDisplay.setZoneEffect(0, true, PA_FLIP_UD);
  myDisplay.setTextAlignment(PA_CENTER);
  myDisplay.print(timeStr);
}


#if TZ_SELFTEST
// The four instants that decide whether daylight saving is right. The EU
// switches at 01:00 UTC, so each pair straddles that second by a minute --
// anything that switches on a day boundary instead gets these wrong.
struct TzCase {
  time_t utc;               // seconds since the epoch, UTC
  const char *expect;       // what Europe/Prague should make of it
};

// 1774746000 = 2026-03-29 01:00:00 UTC, 1792890000 = 2026-10-25 01:00:00 UTC.
static const TzCase TZ_CASES[] = {
  { 1774746000 - 60, "2026-03-29 01:59:00 CET"  },  // a minute before the spring switch
  { 1774746000 + 60, "2026-03-29 03:01:00 CEST" },  // a minute after
  { 1792890000 - 60, "2026-10-25 02:59:00 CEST" },  // a minute before the autumn switch
  { 1792890000 + 60, "2026-10-25 02:01:00 CET"  },  // a minute after
};

#define TZ_DEFAULT "CET-1CEST,M3.5.0,M10.5.0/3"

void runTzSelfTest() {
  setenv("TZ", TZ_STRING, 1);
  tzset();                  // no NTP here: the point is to test the zone, offline

  Serial.printf("\n[tztest] tz %s\n", TZ_STRING);

  // The expectations above are Europe/Prague's, and the EU switch dates are not
  // everyone's -- North America moves on different Sundays entirely. If you
  // changed TZ_STRING, the four instants still print and are still worth
  // reading; it is only the verdict that stops meaning anything.
  bool defaultZone = strcmp(TZ_STRING, TZ_DEFAULT) == 0;
  if (!defaultZone) {
    Serial.println("[tztest] TZ_STRING is not the default -- read the values, ignore the verdict");
  }

  int bad = 0;
  for (unsigned i = 0; i < sizeof(TZ_CASES) / sizeof(TZ_CASES[0]); i++) {
    // Set the real system clock and read it back through getLocalTime(), the
    // same call loop() uses -- testing localtime_r() on a literal would prove
    // less than it looks.
    // Half a second in, so the read-back cannot tick over into the next second
    // and fail the string match for no reason.
    struct timeval tv = { .tv_sec = TZ_CASES[i].utc, .tv_usec = 500000 };
    settimeofday(&tv, NULL);

    struct tm local;
    getLocalTime(&local, 0);

    char got[64];
    strftime(got, sizeof(got), "%Y-%m-%d %H:%M:%S %Z", &local);

    bool ok = strcmp(got, TZ_CASES[i].expect) == 0;
    if (!ok) bad++;
    Serial.printf("[tztest] %s  got %-28s want %s\n",
                  ok ? "ok  " : "FAIL", got, TZ_CASES[i].expect);
  }
  Serial.printf("[tztest] %s (%d of %u wrong)\n",
                !defaultZone ? "INFORMATIONAL" : bad ? "FAILED" : "PASSED", bad,
                (unsigned)(sizeof(TZ_CASES) / sizeof(TZ_CASES[0])));
}
#endif

String twoDigits(int number) {
  if (number < 10) {
    return "0" + String(number);
  }
  return String(number);
}
