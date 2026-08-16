# arduino-clock

Firmware for an ESP32 digital clock on a 4x 8x8 LED matrix. It gets the time
over the network, so there is nothing to set: plug it in and it shows the right
time, including the daylight-saving switch.

The matching PCB is [esp32pcb/hodiny](https://github.com/esp32pcb/hodiny), but
nothing here is specific to that board -- any ESP32 with four MAX7219 modules
will do. Only the three pins below have to match.

## Hardware

| | |
|---|---|
| board | any ESP32 dev module |
| display | 4x MAX7219 8x8 matrix, the common "4-in-1" strip |

| display | ESP32 |
|---|---|
| CLK | GPIO26 |
| DIN | GPIO25 |
| CS | GPIO27 |
| VCC | 5V |
| GND | GND |

The pins are a choice, not a constraint -- change `CLK_PIN`, `DATA_PIN` and
`CS_PIN` at the top of the sketch if your wiring differs.

## Libraries

Install through the Arduino Library Manager, or:

```bash
arduino-cli lib install "MD_Parola@3.7.0" "MD_MAX72XX@3.4.1"
```

That is the whole dependency list. Time handling uses the ESP32 core's own SNTP
and the C library's timezone support, so there is no NTP or time library to add.

## Configuration

### WiFi

Credentials live in `secrets.h`, which is gitignored so it cannot be committed
by accident. Either generate it:

```bash
cp .env.test.example .env.test     # then fill in your SSID and password
./scripts/gen_secrets.sh
```

or, if you are in the Arduino IDE and would rather not touch a shell, create
`secrets.h` next to the sketch yourself:

```c
#pragma once
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"
```

### Timezone

`TZ_STRING` at the top of the sketch, as a POSIX TZ string. The default is
Europe/Prague:

| where | string |
|---|---|
| Prague, Berlin, Paris, Madrid, Rome | `CET-1CEST,M3.5.0,M10.5.0/3` |
| London, Dublin, Lisbon | `GMT0BST,M3.5.0/1,M10.5.0` |
| Helsinki, Athens, Kyiv | `EET-2EEST,M3.5.0/3,M10.5.0/4` |
| New York, Toronto | `EST5EDT,M3.2.0,M11.1.0` |
| Los Angeles, Vancouver | `PST8PDT,M3.2.0,M11.1.0` |
| UTC, no daylight saving | `UTC0` |

Anywhere else: [posix_tz_db](https://github.com/nayarsystems/posix_tz_db).

**Watch the sign.** POSIX inverts it -- `CET-1` means UTC**+**1, not UTC-1. It
reads backwards and it is the easiest thing here to get wrong.

Set `NTP_SERVER` to a pool near you while you are in there
(`north-america.pool.ntp.org`, `asia.pool.ntp.org`, ...).

## Building

### Arduino IDE

Board **ESP32 Dev Module**, upload speed 921600, CPU 240MHz, flash 80MHz. Open
`arduino-clock.ino` and upload. On some boards the **BOOT** button has to be held
while the upload starts.

### arduino-cli

```bash
./scripts/build.sh                    # compile
./scripts/flash.sh /dev/ttyUSB0       # compile and upload
./scripts/monitor.sh /dev/ttyUSB0 60  # serial log, 60 s then exit
```

`flash.sh` prints the MAC of the board it is about to write to -- worth a glance
if several ESP32s are plugged in, since they are hard to tell apart by port name
alone.

## What the display does

Normally: the time, `H:MM`, colon steady.

`--:--` means **the clock does not know what time it is** and is not going to
guess. That happens before the first successful sync after power-up, and again
whenever NTP has not answered for `STALE_AFTER` (2 hours, against a 15 minute
poll -- eight missed polls in a row).

Hiding the time rather than flagging it is deliberate. The ESP32 keeps counting
through a network outage, so it can display a perfectly plausible wrong time,
and a plausible wrong time on a clock is worse than no time: nothing about it
invites a second look. Dashes are unmissable and never blink.

The serial log, at 115200, says which it is:

```
[status] 2026-08-16 13:45:10 CEST  sync=10s ago fresh  wifi=up rssi=-38
[status] 2026-08-16 15:52:41 CEST  sync=7412s ago STALE  wifi=up rssi=-41
```

Note `wifi=up` on the stale line. Being associated is not evidence the time is
good -- the link can be fine while NTP replies never arrive, which is exactly
the case the indicator exists for.

## Daylight saving

The C library does it, from `TZ_STRING`. That matters because the rule is
fiddlier than it looks: the EU switches at **01:00 UTC**, not at some hour of
local time, and North America switches on entirely different Sundays. Anything
that compares dates by hand tends to get the transition day wrong by a couple of
hours, or to oscillate across it.

To check your zone without waiting for March, set `TZ_SELFTEST` to `1` and
flash. It needs no network: it pins the clock to the four instants either side
of both switches and prints what your timezone made of them.

```
[tztest] tz CET-1CEST,M3.5.0,M10.5.0/3
[tztest] ok    got 2026-03-29 01:59:00 CET      want 2026-03-29 01:59:00 CET
[tztest] ok    got 2026-03-29 03:01:00 CEST     want 2026-03-29 03:01:00 CEST
[tztest] ok    got 2026-10-25 02:59:00 CEST     want 2026-10-25 02:59:00 CEST
[tztest] ok    got 2026-10-25 02:01:00 CET      want 2026-10-25 02:01:00 CET
[tztest] PASSED (0 of 4 wrong)
```

The expected values are Europe/Prague's. If you changed `TZ_STRING` the four
instants still print and are still worth reading, but the verdict stops meaning
anything -- other zones switch on other days. Set the flag back to `0` afterwards.

## Troubleshooting

**Display stuck on `--:--`.** Check the serial log. `wifi=down` means it never
associated: wrong credentials, or out of range. `wifi=up` with `sync=never` means
NTP is not getting through -- some networks block outbound UDP 123.

**It takes a while to connect.** Time to associate varies with the access point;
the same firmware has come up in 15 seconds and, on the next boot, taken over a
minute. The clock keeps trying by itself, and forces a reconnect if the link
stays down for 30 seconds.

**Scrambled or mirrored digits.** Wrong `HARDWARE_TYPE` for your modules. Try
`PAROLA_HW` or `GENERIC_HW` instead of `FC16_HW`. If the whole display is upside
down, remove the `PA_FLIP_LR` / `PA_FLIP_UD` calls in `displayTime()` -- they are
there because the intended board mounts the matrix inverted.

**Empty serial log.** Do not use `arduino-cli monitor`: it asserts DTR/RTS when
opening the port, which holds the chip in reset. The log stays empty and it looks
like dead firmware. `scripts/monitor.sh` opens the port with both lines
deasserted.

**Upload fails.** Hold the **BOOT** button as the upload starts.

## Licence

MIT, see [LICENSE](LICENSE). Author: Vaclav Juchelka.
