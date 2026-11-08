# ble-wifi-radar

BLE & WiFi radar with spam and parking reminder, running on the ESP32 Marauder v7.

## What it is

A radar for BLE and WiFi devices, running on the ESP32 Marauder v7
board. The board is just the vessel the radar runs on — the radar
is the project.

It listens for nearby Bluetooth (BLE) and WiFi devices and shows
them as dots on a radar sweep, strongest closest to the centre.
It never joins a WiFi network and never connects to a BLE device.
It only listens.

It also has:

- **GPS spot finder** — save your current spot, or save your
  parking spot, by GPS, and get an arrow pointing back to it.
- **Parking reminder** — the saved parking spot survives a
  reboot, so you can find your car again later.
- **Battery gauge** — reads the board's fuel-gauge chip and shows
  the battery percentage on screen.
- **SD logging** — every scan round is written to
  `/sdcard/marauder_v7_radar.csv` on the microSD card, with the
  saved spots too.
- **Radar / Spam switch (v1.7)** — switch on screen between
  radar (listen-only) and spam (BLE and/or WiFi broadcast spam),
  with no reflash.

## Hardware

Plain Marauder V7 (not V7.1), ESP32 (classic, not S3/C5).

- Display: ILI9341 240x320, CS 17, DC 26, RST -1, MOSI 23 /
  MISO 19 / SCLK 18, backlight 32, SPI3 (VSPI) 27 MHz.
- 5-way stick: LEFT 13, CENTER 34, UP 36, RIGHT 39, DOWN 35.
- GPS: UART2, TX 21 / RX 22, 9600 baud.
- SD card: shared display bus (SPI3), CS 4.
- Battery gauge: MAX17048 on I2C (SDA 33 / SCL 16, address 0x36).

## The firmware file

**`firmware/marauder-v7-radar-v1-7-merged.bin`** — 1,353,776 bytes,
md5 `be2b0fc2f17fc78f9b7a733de1e58162`.

## How to flash

1. Put `marauder-v7-radar-v1-7-merged.bin` on your phone or computer.
2. Use ESPWebTool or ESPFlash. Pick the file, flash address **0x0**,
   **Unknown Format / As Raw**. The chip is the classic **esp32**.
3. Check the filename says **v1-7** before you flash — earlier
   builds shared a plain filename and an old bin got flashed by
   mistake once.

First boot shows a short display test (green border + title,
about 3 seconds), then the radar starts. The radar starts even
if GPS or the SD card fails.

## How to build it yourself

You need ESP-IDF v6.0.1 and the classic ESP32 target.

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

The merged bin is built with the `single_app_large` partition
table, 4 MB flash, DIO 40 MHz, Bluetooth BLE-only.

## Views — and how to get to MODE

There are five views: RADAR, FINDER, LIST, CENTER, MODE.

From the RADAR view, pressing RIGHT steps through FINDER, then
LIST, then CENTER. Pressing LEFT steps backwards.

**Important — the CENTER view traps you.** In the CENTER view,
LEFT and RIGHT do not change views at all: they only nudge the
whole picture left or right by 2 pixels (that view is for
centring the picture on the glass). The only way out of the
CENTER view is pushing the stick **in** (CENTER), which saves
the centre offset and takes you back to the RADAR view.

So pressing RIGHT over and over can never reach MODE, because
the CENTER view sits in the way. **To reach the MODE view,
press LEFT once from the RADAR view.** That lands you straight
on MODE.

- **RADAR** — the sweep: green rings, BLE dots blue, WiFi dots
  yellow, strongest device on the bottom lines.
- **FINDER** — GPS spot finder: save a spot or save PARKING,
  then a big arrow points you back to it.
- **LIST** — BLE and WiFi together, strongest first.
- **CENTER** — green border and cross, for nudging the picture
  true on the glass. UP/DOWN nudge up/down, LEFT/RIGHT nudge
  left/right, pushing the stick in saves the offset and goes
  back to RADAR.
- **MODE** — the Radar / Spam switch (see below).

## MODE view — Radar / Spam switch

On the MODE view you will see:

```
MODE RADAR SD OK BAT 87%
RADAR - SCAN ONLY
SPAM BLE
SPAM WIFI
SPAM BOTH
FUNNY: ON
BLE SPAM SENT 0
WIFI SPAM SENT 0
```

- UP / DOWN move the cursor. CENTER (push the stick in)
  applies the mode you picked and takes you back to the RADAR
  view. The mode you picked is marked `*ACTIVE`.
- FUNNY is the last row: CENTER on that row toggles it in place.
  FUNNY ON = the funny spam names below. FUNNY OFF = the plain
  lists. Funny is on by default.
- The last mode you picked and the Funny flag are remembered
  across reboots — but the board **always boots into RADAR**.
  It never starts up in a spam mode.

### What each mode does

- **RADAR (default, boot):** scan-only. BLE scan + passive WiFi
  scan, radar / FINDER / LIST / CENTER views, SD logging, GPS
  spot finder + parking save. No transmitting.
- **SPAM BLE:** sends non-connectable BLE advertisements, one
  rotating device name about every 800 ms. Phones nearby see a
  new BLE device with that name appear, over and over.
- **SPAM WIFI:** sends broadcast WiFi beacon frames for a
  rotating fake network name, channels 1 / 6 / 11, about one
  beacon a second. Phones nearby see a new fake WiFi network
  appear, over and over.
- **SPAM BOTH:** alternates a BLE advert and a WiFi beacon,
  about one combined round a second.
- **No deauth.** Nothing targets a specific network or device.
  Both spam types are broadcasts only, the same kind of
  broadcast stock Marauder's spam uses.

While spamming, the scanner idles — scans and spam never run
at the same time. Switching back to RADAR stops the spam
within about a second and the scans come back.

### Funny spam payloads (on by default)

A few of the BLE funny names: `Free Candy`,
`FBI Surveillance Van`, `Virus Detected`,
`Your Phone Is Hacked`, `Definitely Not A Tracker`,
`It Burns When I Pee`, `404 Device Not Found`.

A few of the WiFi funny names: `FBI Surveillance Van`,
`Pretty Fly For A Wi-Fi`, `It Hurts When IP`, `Virus.exe`,
`Skynet Global`, `The LAN Before Time`.

There are 10 funny names and 10 funny network names in total,
plus plain lists when FUNNY is off (BLE: `Free WiFi`,
`V7 Radar`, `BLE Beacon`, `Printer`, `Smart TV`, `Headphones`,
`Watch`, `Speaker`; WiFi: `Free WiFi`, `Guest Network`,
`Coffee Shop WiFi`, `Open WiFi`, `Hotel WiFi`, `V7 Radar`,
`Public WiFi`, `Library WiFi`).

### One phone quirk, not a board bug

A phone's Bluetooth list may show the funny names cycle once
and then stick on the first name it saw (for example
`FBI Surveillance Van`), even while the board is still sending
new names. That is the phone caching the name for that
Bluetooth address, not the board stopping. If the
`BLE SPAM SENT` number on the board is still climbing, the
board is still spamming. Turn the phone's Bluetooth off and
back on, or re-scan, and it will show the current name. A BLE
scanner app shows the names changing live.

## What you should see

On the RADAR view: green border, `BLE WIFI RADAR` centred at
the top, `GPS SEARCH SD OK BAT 87%` (or your real percentage)
underneath, green rings and sweep, blue BLE dots, yellow WiFi
dots, strongest name on the bottom lines (`B: ...` blue,
`W: ...` yellow).

In SPAM mode the status line reads e.g. `SPAM-BLE SD OK BAT 87%`
and the bottom lines read `B: SPAM 12` / `W: SPAM 34` instead of
scan results. The current mode is shown on every view.

In the serial log (115200 baud) a SPAM BLE run looks like:

```
RADAR2 1.7 starting — BLE+WiFi radar, ... + Radar/Spam switch + Funny spam
MODE: RADAR -> SPAM BLE (funny ON)
SPAM BLE #1: 'Free Candy' (funny)
SPAM BLE #2: 'FBI Surveillance Van' (funny)
MODE: SPAM BLE -> RADAR (funny ON)
BLE scan done: N devices
WiFi scan done: N nets
SD log start
```

## Version history

- **v1.7 (2026-11-07)** — Radar / Spam switch + funny spam
  (the MODE view). Hardware-tested 2026-11-08: radar sweeping,
  SD OK, BAT 56%, BLE spam verified — the sent counter climbs
  and the names rotate. One build fix: ESP-IDF v6 no longer
  includes `esp_random.h` from `esp_system.h`, so main.c
  includes it directly.
- **v1.6 (2026-11-06)** — title alignment only: the
  `BLE WIFI RADAR` title is now measured and centred (x=78,
  y=8) with a clear gap under the top border, instead of
  sitting on the border line.
- **v1.5 (2026-11-06)** — one mutex (`spi3_lock`) shared by
  the display and the SD card on SPI3. Fixed a panic/reboot
  loop: display DMA was still in flight when SD logging
  started a transfer on the same bus.
- **v1.4 (2026-11-06)** — SD fix (the card is mounted on the
  display's SPI3 bus, which is the one that is actually
  initialised, instead of SPI2 which never was) and the
  MAX17048 battery gauge.
- **v1.3 (2026-11-06)** — no full framebuffer (v1.2 tried to
  allocate a 153,600-byte framebuffer and aborted on boot).
  Two small band buffers, double-buffered, instead.

## Honest limits

- GPS needs open sky. Indoors it may never get a fix; FINDER
  will say NO GPS FIX plainly.
- Distance from signal strength is rough, not exact feet.
- No compass on the board — the finder arrow only knows your
  direction once you walk.
- WiFi is 2.4 GHz only on this board, and scans never join a
  network — this only listens. BLE is scan-only, no GATT
  connect.
- The screen is drawn a band at a time, so the frame rate is
  lower than a full-framebuffer design. The sweep still moves
  10 degrees per 250 ms tick.
- If the top line says `BAT --%`, the fuel-gauge chip did not
  answer on I2C. The radar still works.
- If the top line says `SD NOBUS / TIMEOUT / NORESP / CRC /
  NOCARD` instead of `SD OK`, the serial log names the exact
  failure — send the log lines around the mount attempt.

## License

MIT. The funny spam names are jokes; the spam is
broadcast-only and never targets a specific network or device.
