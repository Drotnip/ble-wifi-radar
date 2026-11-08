/*
 * Marauder V7 Radar v1.7 - built 2026-11-07, David approved:
 *   RADAR / SPAM SWITCH + FUNNY SPAM: a MODE view (LEFT/RIGHT cycle,
 *   5th view) lets David pick RADAR (scan-only, exactly v1.6) or
 *   SPAM BLE / SPAM WIFI / SPAM BOTH, on screen, no reflash.
 *   SPAM transmits only while SPAM mode is selected; switching
 *   back to RADAR stops it and scans resume. Boot is always RADAR.
 *   Funny preset (ON by default, toggleable in MODE view): funny
 *   BLE device names and funny WiFi SSIDs; Normal payloads are
 *   kept alongside as the non-funny list. NO deauth, nothing
 *   targets a specific network.
 * Marauder V7 Radar v1.6 — built 2026-11-06, David approved:
 *   TITLE ALIGNMENT FIX: the title sat on row 0 (y=0..7) and
 *   fb_border() is drawn last, so the top border line painted
 *   over the title's top pixel row; the old centred helper also
 *   added the centre-nudge offset, so the title drifted. v1.6
 *   draws the title (and all centred header lines) with
 *   fb_text_centered_at(): exact pixel y, ink-width centring
 *   x=(240-(len*6-1))/2, no offset. Title at y=8, status at
 *   y=16, version tag "1.7" top-right at (220,1).
 * Marauder V7 Radar v1.3 — built 2026-11-06, David approved, colours
 * Marauder V7 Radar v1.5 — built 2026-11-06, David approved:
 *   SPI3 MUTEX FIX (proven root cause, 2026-11-06 09:04 CST):
 *   v1.4 panicked at 13477 ms: "assert failed: spi_hal_setup_trans",
 *   after "WiFi scan done: 6 nets", inside sd_log_round(). The display
 *   DMA on SPI3 (tx_color = spi_device_queue_trans, async) was still
 *   in flight when the SD driver on the same SPI3 bus started a polling
 *   transfer (spi_device_polling_transmit -> spi_hal_setup_trans ->
 *   HAL_ASSERT(spi_ll_get_running_cmd(hw)==0)). The SPI bus lock does
 *   NOT cover in-flight DMA. v1.3 never hit it because its SD mount
 *   failed and no SD I/O ever ran; v1.4's SD fix working enabled it.
 *   v1.5 adds ONE mutex, spi3_lock, shared by display and SD on SPI3:
 *   push_frame() holds it for the whole frame and waits for the last
 *   band's DMA-done semaphore before releasing; sd_log_round() /
 *   sd_log_pin() / sd_append_open() and the SD mount task hold it for
 *   the whole fopen/fprintf/fclose (and mount) sequence. Lock order is
 *   always spi3_lock first, then gps_lock / dev_lock inside it.
 * Marauder V7 Radar v1.4 — built 2026-11-06, David approved:
 *   1. BATTERY GAUGE: MAX17048 fuel gauge on I2C SDA 33 / SCL 16
 *      (Koko configs.h MARAUDER_V7: HAS_MAX1704X), I2C address 0x36.
 *      SOC register 0x04 (% = raw/256), VCELL 0x02 (12-bit, 1.25 mV
 *      per bit), read by a bit-banged I2C master (the IDF i2c
 *      driver overflowed IRAM0 at link time — v1.3 already uses
 *      129,651 of 131,072 IRAM bytes). Read at startup and every
 *      30 s, logged as
 *      "BAT: 87% 4120 mV", shown top line as "BAT 87%" plus a small
 *      battery bar. If the gauge does not answer, "BAT --%" and the
 *      radar keeps running — never abort, never park.
 *   2. SD FIX (proven cause, not a guess): v1.3 mounted the SD card
 *      on SPI2_HOST, but SPI2 was never initialised — IDF's
 *      sdspi_host_init_device() requires spi_bus_initialize() first
 *      (esp_driver_sdspi/src/sdspi_host.c, "Initialize the SPI bus
 *      by spi_bus_initialize() before calling this function"), so
 *      spi_bus_add_device() failed ESP_ERR_INVALID_STATE and every
 *      mount failed -> "SD FAIL" on screen. The V7 SD lines are
 *      MOSI 23 / MISO 19 / SCK 18, i.e. the DISPLAY's SPI3_HOST
 *      bus (Koko configs.h MARAUDER_V7: SD_MISO/MOSI/SCK =
 *      TFT_MISO/MOSI/SCLK, SD_CS 4). v1.4 mounts SD on SPI3_HOST,
 *      the already-initialised shared display bus, CS 4 unchanged,
 *      with 2 mount tries 500 ms apart and the exact error logged.
 *      The SD_CS preset HIGH before display init is kept.
 * back in (radar GREEN, BLE BLUE, WiFi YELLOW) because the DMA race
 * below is fixed — if colours still land wrong on hardware, the
 * fallback is regular green/white.
 *
 * v1.3 fixes TWO proven root causes from v1.2 (see GOAL.md):
 *  1. v1.2 aborted before the screen ever lit: app_main() first
 *     line was heap_caps_malloc(153,600) for a full 240x320
 *     framebuffer. Largest free block on this chip is only
 *     113,840 bytes, so malloc returned NULL, the code called
 *     abort(), and the board reboot-looped every ~1.5-2 s.
 *     v1.3 has NO full framebuffer. The screen is drawn 8 rows at
 *     a time into one of TWO 3,840-byte band buffers.
 *  2. DMA buffer-reuse race (proven in IDF v6 source): tx_color()
 *     is ASYNC — it queues the DMA and returns at once. v1.2's
 *     fb_push() and the V7TEST builds reused a single buffer
 *     right after tx_color() returned, so DMA read a mix of old /
 *     new-unswapped / new-swapped pixels: bands came out
 *     blue/red/green, black gaps every ~8 rows, dotted border.
 *     v1.3 double-buffers: the buffer being drawn into is never
 *     the one DMA is reading. A buffer is only reused two bands
 *     later, after the next band's CASET/RASET (tx_param waits
 *     for all in-flight colour transfers) AND the
 *     on_color_trans_done callback has signalled its DMA done.
 *     Each band is still byte-swapped before tx_color
 *     (TFT_eSPI setSwapBytes(true) equivalent, from V7TEST 1.2).
 *  3. Backlight via LEDC PWM 5 kHz duty 255, using
 *     ledc_set_duty() + ledc_update_duty() (NOT
 *     ledc_set_duty_and_update — that needs the fade service and
 *     ESP_FAILs, proven by V7TEST 1.0). SD_CS GPIO4 is preset
 *     HIGH before display init, as Marauder does.
 * Never calls abort() or ESP_ERROR_CHECK on a malloc/display
 * failure — it logs and parks, or logs and carries on.
 *
 * Marauder V7 Radar v1.2 — port of Mini v2 Radar v2.4 to David's
 * v1.2 (2026-11-05): DC 16 -> 26, RST 5 -> none (-1) — see below.
 * v1.1 (2026-11-05): BL flip only.
 * ESP32 Marauder V7 (plain V7, NOT V7.1 — confirmed by his photo
 * 2026-11-05: "Marauder V7" / JUSTCALLMEKOKO, ESP32-WROOM-32U,
 * ATGM336H GPS, RST + DFU buttons, USB-C, battery JST, microSD).
 * Koko ESP32Marauder master v1.20.0 commit 511bb36, configs.h:
 * classic ESP32, 2.4 GHz only, no PSRAM, no NeoPixel.
 * Display ILI9341 240x320 (not ST7735 128x160), SPI pins
 * MOSI 23 / MISO 19 / SCK 18 / CS 17 / DC 26 / RST -1 (none) /
 * BL 32. v1.2 FIX (2026-11-05): v1/v1.1 used DC 16 / RST 5, the
 * Marauder Mini pins from the commented-out Mini block in
 * User_Setup_dual_nrf24.h. The ACTIVE "ESP32 Marauder" block in
 * that same file is CS 17 / DC 26 / RST -1 / MOSI 23 / SCLK 18 /
 * BL 32, driver ILI9341. Pin 16 on the V7 is the MAX1704X fuel
 * gauge I2C SCL (configs.h MARAUDER_V7: I2C_SDA 33 / I2C_SCL 16),
 * not the display DC. With DC on 16 every command/data byte was
 * mis-tagged, so the screen never initialised and stayed black
 * no matter the backlight level. Verified against two sources
 * before this build: User_Setup_dual_nrf24.h (active block) and
 * configs.h MARAUDER_V7 (buttons L13/C34/U36/R39/D35, GPS UART2
 * TX21/RX22, SD_CS 4, I2C SDA33/SCL16, 240x320 TFT_DIY).
 * GPS FIXED: UART2 TX 21 / RX 22, 9600 baud — the GPS hunt is
 * DELETED entirely, so the Mini's boot-panic suspect (GPIO12
 * probe) is gone by construction. GPIO21 is GPS TX only — never
 * drive it as TOUCH_CS. Buttons 5-way, same as Mini.
 * Partition: single-app LARGE (1500K factory), 4 MB-safe — do NOT
 * assume the Mini's 8 MB flash (WROOM-32U is commonly 4 MB).
 *
 * Mini v2 Radar v2.4 — BLE + WiFi radar with GPS spot finder and
 * save-parking, for David's red Marauder Mini v2.
 *
 * v2.4 (2026-11-05 — David reported "I'm on boot panic 4" on v2.3,
 * after his 14 s video ended BOOT 1 PANIC): only two changes from
 * v2.3, everything else kept exactly.
 *   1. GPIO12 DROPPED from the GPS hunt entirely — never probed,
 *      never configured for GPS. It was the first pin in the hunt
 *      list and the leading suspect for the panic crash (~0.9 s
 *      into the first probe). Hunt list is now 14, 15, 22, 25, 33,
 *      26, 27, then 13 last (13 last, it is also the LEFT button).
 *   2. Default centre offset X+2 Y+4 — David said those
 *      coordinates are better for the screen. That is now the
 *      starting point when no offset is saved in NVS. A saved
 *      offset in NVS still wins, and the CENTER view nudge/save
 *      still works and overrides it.
 *   3. CENTER view border inset 2 px (x=2..125, y=2..157), not
 *      on the framebuffer edge. David reported the bright static
 *      line on the right edge does NOT move with the nudge, so
 *      it is the panel's unwritten edge, not drawn content. The
 *      inset green border sits 2 px inside it and DOES move with
 *      the nudge, so he can tell them apart. RADAR view keeps
 *      the edge border.
 *
 * v2.1 fixes (David's photo, 2026-11-05): NO full-screen clear per
 * frame — persistent framebuffer, continuous sweep with fading
 * dots; centred 128x160 view (the glass is taller than square),
 * FB offsets removed, no text line clips.
 *
 * v2.2 fixes (David's video, 2026-11-05 — v2.1 still came back
 * off-centre, and the boot screen had too many words):
 *   - ONE title everywhere: "BLE&WIFI RADAR". No "RADAR V2.1",
 *     no "BLE BLUE WIFI YELLOW", no "BOOTING...", no multi-line
 *     boot clutter. The boot screen is that one title line plus
 *     one small GPS/SD status line. A tiny "2.2" sits in a corner
 *     so the build can be told apart — it is not a title line.
 *   - NO MORE GUESSED CENTRING. v2.1's fixed 0,0 offset did NOT
 *     fix it on hardware, so v2.2 does not guess another one.
 *     Instead there is a CENTER view: a green border round the
 *     framebuffer edge and a green cross at the centre, and the
 *     5-way nudges the whole view up/down/left/right by 2 px
 *     per press. CENTER saves the offset to NVS and returns to
 *     RADAR. The offset shifts ALL drawing and survives reboot.
 *     Only David's eyes on the CENTER view prove it centred —
 *     this build does not claim it is.
 *
 * v2.3 (delivered 2026-11-05 after v2.2 still boot-looped):
 * a merge of v2.2 with the v2.1 diagnostics. All of v2.2 is kept exactly:
 * continuous sweep with no per-frame full clear, persistent
 * framebuffer with fading dots, title "BLE&WIFI RADAR" only,
 * CENTER view with border/cross and button-nudge offset saved
 * to NVS, radar green / BLE blue / WiFi yellow, BLE scan-only,
 * WiFi passive, GPS spot finder, save parking, same 5-way
 * controls, same display config 2.
 *   1. Non-blocking GPS hunt: the radar starts first, the hunt
 *      runs as a background task 3 s later. app_main does NOT
 *      block on the hunt loop before starting ui/scanner tasks.
 *      Same candidate RX pins, same order, GPIO12 first.
 *   2. Boot counter + reset reason on the boot screen: boots are
 *      counted in NVS, shown as "BOOT #N REASON" (POWERON/SW/
 *      PANIC/INT_WDT/TASK_WDT/BROWNOUT) as the boot screen's small
 *      status line. Cleared after a completed scan round, so a
 *      healthy board stays at low numbers.
 *   3. Boot-4+ GPS skip: on boot #4+ with no completed scan
 *      round, the GPS hunt is skipped that boot so radar still
 *      comes up.
 * Why (from David's video, frame-by-frame): full white 0.40-0.93 s
 * and 2.83-3.37 s, exactly 0.53 s each, period exactly 2.43 s.
 * BOOTING appears at ~1.2 s and again at ~4.0 s, and BOOTING is
 * printed once per app_main, so twice in 5.5 s means the board
 * restarted. Radar view never appears. The radar code cannot
 * produce a full white flash — ui_task fills with COL_BLACK and
 * no render writes full white; the white is the LCD
 * re-initialising after a restart. Likely crash point ~0.9 s
 * into the first GPS probe, GPIO12 first in the hunt list. That
 * is NOT proven — no serial log exists — it is the leading
 * explanation. The earlier "clears to white" answer was wrong.
 *
 * Approved by David 2026-11-05. This is a NEW project: it does NOT
 * overwrite mini-v2-radar (Oct 25 mapper, got stuck on SD/GPS) or
 * mini-v2-ble-profiler-v2 (profiler v4, still on the board until
 * this is flashed). It reuses their proven code:
 *   - display + framebuffer + +6/+4 content shift + INVOFF, and the
 *     5-way direct button map, from profiler v4 (seen working);
 *   - SD mount in its own task with a timeout, from profiler v4
 *     (the Oct 25 mapper blocked on SD and never advanced — this
 *     build never blocks the radar on SD);
 *   - GPS NMEA parsing + RX-pin hunt, and passive WiFi scan, from
 *     the Oct 25 radar mapper.
 *
 * What it does:
 *   RADAR view (default): GREEN radar — green rings and sweep,
 *     centre is you. BLE devices are BLUE dots, WiFi networks are
 *     YELLOW dots. Closer to centre = stronger signal. Names show
 *     (name/SSID when known, else MAC suffix) — never numbers only.
 *     Scan-only for BLE: NO GATT connect/profile, so the profiler
 *     v4 "Max TCB" stuck-open connection leak cannot happen here.
 *     A thin green border round the frame edge stays visible here
 *     too, so centring can be checked at a glance.
 *   FINDER view: save a spot by GPS, or save PARKING, then a big
 *     arrow + distance guides you back to it. Two slots: SPOT and
 *     PARKING, saved in NVS so they survive reboot.
 *   LIST view: BLE + WiFi together, strongest first, 13 rows.
 *   CENTER view: green border + cross, nudge the view true with
 *     the 5-way, CENTER saves the offset and goes back to RADAR.
 *   SD log: /sdcard/marauder_v7_radar.csv, closed after each round.
 *
 * Controls (5-way only, no BOOT, no long-press):
 *   LEFT/RIGHT: cycle views RADAR -> FINDER -> LIST -> CENTER
 *   RADAR: CENTER = freeze/unfreeze radar
 *   FINDER: UP/DOWN = pick SPOT or PARKING slot,
 *           CENTER = save current GPS into that slot (needs a fix)
 *   LIST: UP/DOWN = scroll, CENTER = freeze/unfreeze
 *   CENTER: UP/DOWN = nudge view up/down 2 px,
 *           LEFT/RIGHT = nudge view left/right 2 px,
 *           CENTER = save offset to NVS and return to RADAR
 *           (in CENTER view, LEFT/RIGHT nudge — they do NOT
 *           change view. CENTER is the only way out of it.)
 *
 * Hardware (V7, grounded): ILI9341 240x320 — MOSI 23, SCLK 18,
 * MISO 19, CS 17, DC 26, RST -1 (none), backlight 32 driven HIGH
 * (v1.1 flipped BL LOW->HIGH; v1.2 fixes DC 16->26 and drops the
 * RST 5 toggle — see header), INVOFF, MADCTL 0x48 (MX + BGR).
 * BL / inversion / colour order are UNPROVEN on the V7 — first
 * boot screen is the display test.
 * 5-way: UP ADC GPIO36, DOWN GPIO35, CENTER GPIO34, RIGHT GPIO39
 * (pressed below 500 mV), LEFT GPIO13 digital LOW. GPIO33 sags on
 * Right and is never read. GPIO21 is GPS TX (UART2) — never
 * drive it as touch CS, no touch on the V7. SD CS 4 on the
 * shared SPI bus.
 * GPS: ATGM336H, FIXED UART2 TX 21 / RX 22, 9600 baud NMEA
 * ($GPGGA/$GPRMC). If no fix, radar still works and FINDER
 * shows NO GPS FIX plainly. A blinking GPS LED is NOT proof
 * of data — only a fix on screen proves it.
 *
 * Honest limits (also in README): GPS needs open sky; distance
 * from signal strength is rough, not exact feet; no compass on
 * board, so the finder arrow only knows your direction once you
 * walk (it uses GPS movement); the screen is an arrow + distance,
 * not a street map.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_random.h" /* esp_random(), for the WiFi spam SA/BSSID */
#include "esp_rom_sys.h" /* esp_rom_delay_us, for the bit-banged I2C gauge */
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_lcd_panel_io.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_lcd_types.h"
#include "driver/ledc.h"
#include "font5x7.h"

static const char *TAG = "RADAR2";

/* ---- V7 display pins, verified 2026-11-05 (see header) ----
 * Active block of User_Setup_dual_nrf24.h, NOT the Mini block.
 * RST is -1: the V7 display has no reset pin driven by the
 * firmware (tied to RST / 3.3V in hardware), so no GPIO is
 * configured or toggled for reset in this build. */
#define PIN_MOSI    23
#define PIN_MISO    19
#define PIN_SCLK    18
#define PIN_TFT_CS  17
#define PIN_TFT_DC  26
#define PIN_TFT_RST -1
#define PIN_TFT_BL  32
#define PIN_SD_CS   4

/* Battery gauge: MAX17048 fuel gauge, Koko configs.h MARAUDER_V7
 * (HAS_MAX1704X): I2C SDA 33 / SCL 16, address 0x36. GPIO33/16
 * are used by nothing else in this build. */
#define PIN_BAT_SDA   33
#define PIN_BAT_SCL   16
#define BAT_I2C_ADDR  0x36
#define BAT_REG_VCELL 0x02 /* 12-bit, bits 15:4, 1.25 mV per bit */
#define BAT_REG_SOC   0x04 /* MSB = whole %, LSB = 1/256 % */
#define BAT_REG_VER   0x08 /* MAX17048 = 0x0012, MAX17049 = 0x0013 */

#define LCD_W 240
#define LCD_H 320
/* V7: the glass is 240x320. No content shift: FB_OFF_X/Y are
 * 0, the view is drawn centred on the real panel. No Mini
 * +6/+4 or X+2 Y+4 offsets are carried over. */
#define FB_OFF_X 0
#define FB_OFF_Y 0

#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_GREEN  0x07E0
#define COL_DGREEN 0x03E0
#define COL_YELLOW 0xFFE0
#define COL_CYAN   0x07FF
#define COL_RED    0xF800
#define COL_ORANGE 0xFD00
#define COL_BLUE   0x001F
#define COL_DBLUE  0x000F
#define COL_DYELLOW 0x7BE0
#define COL_MAGENTA 0xF81F
#define COL_GREY   0x8410

#define MAX_BLE   40
#define MAX_WIFI  20
#define BLE_SCAN_SECONDS 5

#define VIEW_RADAR  0
#define VIEW_FINDER 1
#define VIEW_LIST   2
#define VIEW_CENTER 3
#define VIEW_MODE   4
#define VIEW_COUNT  5

#define DIR_CENTER 0
#define DIR_UP     1
#define DIR_DOWN   2
#define DIR_LEFT   3
#define DIR_RIGHT  4
#define DIR_COUNT  5

#define RADAR_CX 120
#define RADAR_CY 160
#define RADAR_R  110
#define FINDER_CX 120
#define FINDER_CY 180
#define FINDER_R  70

static esp_lcd_panel_io_handle_t lcd_io;

/* ---- v1.3 band buffers (double buffering) ---- */
#define BAND_H 8
#define BAND_ROWS (LCD_H / BAND_H)   /* 40 bands */
#define BAND_BYTES (LCD_W * BAND_H * 2)  /* 3,840 bytes */

static uint16_t *band_buf[2] = {NULL, NULL};
static SemaphoreHandle_t band_done_sem[2] = {NULL, NULL};
/* v1.5: ONE mutex shared by display and SD on SPI3. Display DMA is
 * async, so the bus lock alone cannot stop an SD polling transfer
 * starting while a colour DMA is still in flight (v1.4 panic:
 * spi_hal_setup_trans assert in sd_log_round). push_frame() holds
 * this for the whole frame; SD I/O holds it for the whole
 * fopen/fprintf/fclose (and mount). Lock order: spi3_lock first,
 * then gps_lock / dev_lock inside it — never the reverse. */
static SemaphoreHandle_t spi3_lock = NULL;
static int band_buf_idx = 0;    /* buffer being filled for the next push */
static int inflight_buf = -1;   /* buffer whose colour DMA is in flight */
static uint16_t *band_cur = NULL; /* buffer the drawing code writes into */
static int band_y0 = 0;         /* first row of the band being drawn */

/* Park here forever on any fatal failure. Never abort, never reboot. */
static void park_forever(const char *why)
{
    ESP_LOGE(TAG, "PARKED: %s — looping forever, NOT rebooting", why);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* Colour-transfer-done callback (ISR context): the DMA for the
 * buffer recorded in inflight_buf has finished, so that buffer
 * may be drawn into again. */
static bool IRAM_ATTR color_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io; (void)edata; (void)user_ctx;
    BaseType_t woken = pdFALSE;
    if (inflight_buf >= 0 && inflight_buf < 2 && band_done_sem[inflight_buf])
        xSemaphoreGiveFromISR(band_done_sem[inflight_buf], &woken);
    return woken == pdTRUE;
}

/* ---------------- centre offset (v2.2, default v2.4) ----------------
 * David centres the view himself in the CENTER view. The offset
 * shifts ALL drawing: fb_pixel() adds it, and the text functions
 * add it to glyph positions. It is saved to NVS (keys off_x/off_y
 * in namespace "radarv7") and loaded at boot, so it survives
 * reboot. Range +/-16 px, step 2 px per button press.
 * v2.4: default is X+2 Y+4 — David said those coordinates are
 * better for the screen (2026-11-05). That default applies only
 * when no offset is saved in NVS; a saved offset still wins,
 * and CENTER view nudge/save still overrides it. */
static int view_off_x = 0;
static int view_off_y = 0; /* V7 v1: fresh 240x320 layout, no Mini X+2 Y+4 */
#define VIEW_OFF_MAX 16
#define VIEW_OFF_STEP 2

static int clamp_off(int v)
{
    if (v < -VIEW_OFF_MAX) return -VIEW_OFF_MAX;
    if (v > VIEW_OFF_MAX) return VIEW_OFF_MAX;
    return v;
}

/* ---------------- band drawing (v1.3) ----------------
 * Same fb_* names as v1.2, so all render code below is unchanged —
 * but they now write into the CURRENT 8-row band buffer (band_cur)
 * only. A pixel outside the current band is simply not written:
 * push_frame() calls the render function once per band, so every
 * pixel is written exactly once, in the band that contains it. */

static void fb_fill(uint16_t color)
{
    for (int i = 0; i < LCD_W * BAND_H; i++) band_cur[i] = color;
}

static void fb_pixel(int x, int y, uint16_t color)
{
    /* v2.2: the centre offset shifts every pixel of the view. */
    x += view_off_x;
    y += view_off_y;
    if (x < 0 || x >= LCD_W || y < band_y0 || y >= band_y0 + BAND_H) return;
    band_cur[(y - band_y0) * LCD_W + x] = color;
}

/* Raw pixel write: NO centre offset. Only the tiny version tag
 * uses this — the tag is a corner marker, not part of the view. */
static void fb_pixel_raw(int x, int y, uint16_t color)
{
    if (x < 0 || x >= LCD_W || y < band_y0 || y >= band_y0 + BAND_H) return;
    band_cur[(y - band_y0) * LCD_W + x] = color;
}

static void fb_text_row(int r, const char *str, uint16_t fg, uint16_t bg)
{
    /* v2.2: glyphs shift with the centre offset; the bg fill stays
     * full-width so the row never leaves an unpainted gap behind. */
    int y0 = r * 8 + view_off_y;
    /* v1.3: skip the whole row if it does not touch this band. */
    if (y0 + 7 < band_y0 || y0 > band_y0 + BAND_H - 1) return;
    for (int bitrow = 0; bitrow < 8; bitrow++) {
        int y = y0 + bitrow;
        for (int x = 0; x < LCD_W; x++) fb_pixel_raw(x, y, bg);
    }
    int len = strlen(str);
    if (len > 40) len = 40;
    for (int cx = 0; cx < len; cx++) {
        const uint8_t *g = font5x7_get(str[cx]);
        for (int col = 0; col < 5; col++) {
            for (int bitrow = 0; bitrow < 7; bitrow++) {
                int y = y0 + bitrow;
                int x = cx * 6 + col + view_off_x;
                fb_pixel_raw(x, y, (g && (g[col] & (1 << bitrow))) ? fg : bg);
            }
        }
    }
}

/* v1.6: centred text at an EXACT pixel y, fixed header chrome.
 * Two defects in the old row-based helper are fixed here:
 *  (a) the title lived on row 0 (y=0..7) and fb_border() is drawn
 *      LAST, so the top border line (y=0) painted over the title's
 *      top pixel row — the title looked like it sat ON the border.
 *      The title now starts at y=8: 7 blank px below the border.
 *  (b) the old helper added view_off_x/view_off_y, so the title
 *      drifted whenever David nudged the centre offset. Header
 *      chrome is now fixed: it never moves with the offset (the
 *      border and the cross still do, so the centring test in the
 *      CENTER view still works).
 * Centring is measured from the actual rendered ink, not the cell
 * count: each 5x7 glyph is 5 px ink + 1 px spacing, so a string of
 * len chars has ink width len*6-1 (the last char needs no trailing
 * blank). x0 = (240 - ink_w)/2 puts the ink's centre on the
 * screen's centre (119.5). For "BLE&WIFI RADAR" (len 14, ink 83):
 * x0 = (240-83)/2 = 78, ink spans x=78..160, centre 119. The '&'
 * has no glyph in this font (blank cell), so it renders as a gap,
 * but it still counts as a cell in the width — the visible groups
 * stay centred as a whole. */
static void fb_text_centered_at(int y_px, const char *str, uint16_t fg, uint16_t bg)
{
    int len = strlen(str);
    if (len > 40) len = 40;
    int ink_w = len * 6 - 1;
    int x0 = (LCD_W - ink_w) / 2;
    if (x0 < 0) x0 = 0;
    int y0 = y_px;
    /* v1.3: skip the whole row if it does not touch this band. */
    if (y0 + 7 < band_y0 || y0 > band_y0 + BAND_H - 1) return;
    /* Fill the whole row bg first: a re-drawn centred line must
     * erase the previous line, not leave ghost pixels behind it. */
    for (int bitrow = 0; bitrow < 8; bitrow++) {
        int y = y0 + bitrow;
        for (int x = 0; x < LCD_W; x++) fb_pixel_raw(x, y, bg);
    }
    for (int cx = 0; cx < len; cx++) {
        const uint8_t *g = font5x7_get(str[cx]);
        for (int col = 0; col < 5; col++) {
            for (int bitrow = 0; bitrow < 7; bitrow++) {
                int y = y0 + bitrow;
                int x = x0 + cx * 6 + col;
                fb_pixel_raw(x, y, (g && (g[col] & (1 << bitrow))) ? fg : bg);
            }
        }
    }
}

/* v1.6: left-aligned text at an EXACT pixel y, fixed header chrome
 * (same no-offset rule as fb_text_centered_at). Only the header
 * status rows use this; body/content rows keep fb_text_row and
 * still shift with the centre offset, as before. */
static void fb_text_row_at(int y_px, const char *str, uint16_t fg, uint16_t bg)
{
    int y0 = y_px;
    if (y0 + 7 < band_y0 || y0 > band_y0 + BAND_H - 1) return;
    for (int bitrow = 0; bitrow < 8; bitrow++) {
        int y = y0 + bitrow;
        for (int x = 0; x < LCD_W; x++) fb_pixel_raw(x, y, bg);
    }
    int len = strlen(str);
    if (len > 40) len = 40;
    for (int cx = 0; cx < len; cx++) {
        const uint8_t *g = font5x7_get(str[cx]);
        for (int col = 0; col < 5; col++) {
            for (int bitrow = 0; bitrow < 7; bitrow++) {
                int y = y0 + bitrow;
                int x = cx * 6 + col;
                fb_pixel_raw(x, y, (g && (g[col] & (1 << bitrow))) ? fg : bg);
            }
        }
    }
}

/* Tiny text at pixel (x0, y0), NO centre offset, foreground only
 * (background pixels are left alone). Only the version tag
 * uses this — it is a build marker, not part of the view. */
static void fb_text_at_raw(int x0, int y0, const char *str, uint16_t fg)
{
    int len = strlen(str);
    if (len > 40) len = 40;
    for (int cx = 0; cx < len; cx++) {
        const uint8_t *g = font5x7_get(str[cx]);
        if (!g) continue;
        for (int col = 0; col < 5; col++) {
            for (int bitrow = 0; bitrow < 7; bitrow++) {
                if (!(g[col] & (1 << bitrow))) continue;
                fb_pixel_raw(x0 + cx * 6 + col, y0 + bitrow, fg);
            }
        }
    }
}

/* Tiny "1.7" in the top-right corner, so the build can be told
 * apart on screen. Not a title line. */
static void draw_version_tag(void)
{
    fb_text_at_raw(220, 1, "1.7", COL_GREY);
}

static void fb_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fb_circle(int cx, int cy, int r, uint16_t color)
{
    int cy2 = cy + view_off_y;
    if (cy2 + r < band_y0 || cy2 - r > band_y0 + BAND_H - 1) return;
    for (int a = 0; a < 360; a += 2) {
        double rad = a * 3.14159265 / 180.0;
        fb_pixel(cx + (int)(r * cos(rad)), cy + (int)(r * sin(rad)), color);
    }
}

static void fb_blip(int x, int y, int size, uint16_t color)
{
    int h = size / 2;
    int y2 = y + view_off_y;
    if (y2 + h < band_y0 || y2 - h > band_y0 + BAND_H - 1) return;
    for (int dy = -h; dy <= h; dy++)
        for (int dx = -h; dx <= h; dx++)
            fb_pixel(x + dx, y + dy, color);
}

/* 1-pixel border round the framebuffer edge. Drawn through
 * fb_pixel, so it shifts with the centre offset like everything
 * else — that is the point: when the offset is wrong, one side of
 * the border is clipped off the glass and the other side shows a
 * gap; when it is right, the border sits just inside the glass
 * on all four sides. Drawn last, after any text-row bg fill that
 * would otherwise paint over the top/bottom edge rows. */
static void fb_border(uint16_t color)
{
    for (int x = 0; x < LCD_W; x++) {
        fb_pixel(x, 0, color);
        fb_pixel(x, LCD_H - 1, color);
    }
    for (int y = 0; y < LCD_H; y++) {
        fb_pixel(0, y, color);
        fb_pixel(LCD_W - 1, y, color);
    }
}

/* 1-pixel border inset by 2 px from the framebuffer edge
 * (x=2..125, y=2..157), CENTER view only (v2.4). David reported
 * the bright static vertical line on the right edge of the glass
 * does NOT move when he nudges the offset — that line is NOT
 * drawn content, it is the panel's unwritten edge column /
 * hardware edge, so no border drawn exactly on x=127 can be
 * told apart from it. This inset border sits 2 px inside the
 * frame, clearly separate from that static edge, and it DOES
 * move with the nudge (drawn through fb_pixel, like everything
 * else) — so David can tell our border apart from the static
 * line by watching it move. The RADAR view keeps the edge
 * border, so centring stays visible there at a glance. */
static void fb_border_inset(uint16_t color)
{
    const int inset = 2;
    const int x0 = inset, x1 = LCD_W - 1 - inset;
    const int y0 = inset, y1 = LCD_H - 1 - inset;
    for (int x = x0; x <= x1; x++) {
        fb_pixel(x, y0, color);
        fb_pixel(x, y1, color);
    }
    for (int y = y0; y <= y1; y++) {
        fb_pixel(x0, y, color);
        fb_pixel(x1, y, color);
    }
}

/* Green cross at (cx, cy), arms r px each way. Through fb_pixel,
 * so it moves with the centre offset together with the title. */
static void fb_cross(int cx, int cy, int r, uint16_t color)
{
    fb_line(cx - r, cy, cx + r, cy, color);
    fb_line(cx, cy - r, cx, cy + r, color);
}

/* Push one full frame, band by band. `render` draws the current
 * view into band_cur; it is called once per band, and only writes
 * pixels in that band (the fb_* helpers enforce it).
 * Double buffering: band b is drawn into one buffer while DMA is
 * still reading the other. Before filling a buffer we wait for
 * its colour-done semaphore (given by color_done_cb when that
 * buffer's DMA finished); on timeout we log a warning and carry
 * on — the next band's CASET/RASET (tx_param) waits for all
 * in-flight colour transfers anyway, so the buffer is safe
 * either way. Returns ESP_OK, or the first tx error. */
static esp_err_t push_frame(void (*render)(void))
{
    /* v1.5: hold spi3_lock for the whole frame. SD I/O on the same
     * SPI3 bus must not start a polling transfer while a colour DMA
     * is in flight (v1.4 panic: spi_hal_setup_trans assert). The
     * lock is taken with portMAX_DELAY: the only other holder is
     * SD I/O, which is short, and the UI task is the only display
     * caller once tasks are running. */
    if (spi3_lock && xSemaphoreTake(spi3_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "push_frame: spi3_lock take failed");
        return ESP_FAIL;
    }
    esp_err_t ret = ESP_OK;
    for (int b = 0; b < BAND_ROWS; b++) {
        int i = band_buf_idx;
        if (xSemaphoreTake(band_done_sem[i], pdMS_TO_TICKS(50)) != pdTRUE) {
            ESP_LOGW(TAG, "band %d: buffer %d DMA wait timeout — continuing, tx_param still guards it", b, i);
        }
        band_cur = band_buf[i];
        band_y0 = b * BAND_H;
        fb_fill(COL_BLACK);
        render();
        /* Byte-swap before sending (TFT_eSPI setSwapBytes(true)
         * equivalent, from V7TEST 1.2): the ESP32 stores uint16_t
         * little-endian, the ILI9341 expects high-byte-first. */
        for (int k = 0; k < LCD_W * BAND_H; k++)
            band_cur[k] = __builtin_bswap16(band_cur[k]);
        int y0 = band_y0, y1 = band_y0 + BAND_H - 1;
        uint8_t caset[4] = {0, 0, 0, LCD_W - 1};
        uint8_t raset[4] = {(uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
                            (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)};
        esp_err_t err = esp_lcd_panel_io_tx_param(lcd_io, 0x2A, caset, 4);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "band %d CASET failed: %s", b, esp_err_to_name(err));
            ret = err;
            break;
        }
        err = esp_lcd_panel_io_tx_param(lcd_io, 0x2B, raset, 4);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "band %d RASET failed: %s", b, esp_err_to_name(err));
            ret = err;
            break;
        }
        inflight_buf = i;
        err = esp_lcd_panel_io_tx_color(lcd_io, 0x2C, band_cur, BAND_BYTES);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "band %d tx_color failed: %s", b, esp_err_to_name(err));
            ret = err;
            break;
        }
        band_buf_idx ^= 1;
    }
    /* v1.5: before releasing spi3_lock, wait for the LAST band's
     * DMA to finish, so no colour transfer is in flight when SD
     * I/O takes the lock next. The semaphore was consumed when the
     * last band was filled, so taking it again blocks until the
     * colour-done callback gives it. 100 ms is far above the
     * ~1.14 ms a band DMA takes; on timeout log and release anyway
     * rather than holding the lock forever. */
    if (inflight_buf >= 0 && inflight_buf < 2 && band_done_sem[inflight_buf]) {
        if (xSemaphoreTake(band_done_sem[inflight_buf], pdMS_TO_TICKS(100)) != pdTRUE)
            ESP_LOGW(TAG, "push_frame: last band DMA wait timeout — releasing spi3_lock anyway");
    }
    if (spi3_lock) xSemaphoreGive(spi3_lock);
    return ret;
}

/* ---------------- display init (ILI9341 240x320, INVOFF) ----------------
 * Bytes/order copied UNCHANGED from v1.2 lcd_init_sequence() (and
 * the V7TEST display test, which proved them: every init cmd
 * ESP_OK). The only change vs v1.2: every tx_param return value
 * is logged here, instead of being ignored. There is no ILI9341
 * panel driver in ESP-IDF v6.0.1 and v1.2 never used one — this
 * raw command sequence IS the panel init, and DISPON below (0x29)
 * IS the display-on step. */

static esp_err_t lcd_cmd(uint8_t cmd)
{
    esp_err_t err = esp_lcd_panel_io_tx_param(lcd_io, cmd, NULL, 0);
    ESP_LOGI(TAG, "init cmd 0x%02X -> %s", cmd, esp_err_to_name(err));
    return err;
}

static esp_err_t lcd_data(uint8_t cmd, const uint8_t *params, size_t len)
{
    esp_err_t err = esp_lcd_panel_io_tx_param(lcd_io, cmd, params, len);
    ESP_LOGI(TAG, "init cmd 0x%02X (%u bytes) -> %s", cmd, (unsigned)len, esp_err_to_name(err));
    return err;
}

static esp_err_t lcd_init_sequence(void)
{
    esp_err_t err;
    /* v1.2: NO reset toggle. PIN_TFT_RST is -1 on the V7 — the
     * display reset is tied to RST / 3.3V in hardware, not driven
     * by a GPIO. SWRESET (0x01) below is the only reset sent. */
    vTaskDelay(pdMS_TO_TICKS(150));
    err = lcd_cmd(0x01); /* SWRESET */
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));
    /* v1.2: settings first, SLPOUT after (TFT_eSPI order) */
    uint8_t p3[3] = {0x03, 0x80, 0x02};
    err = lcd_data(0xEF, p3, 3); if (err != ESP_OK) return err;
    p3[0] = 0x00; p3[1] = 0xC1; p3[2] = 0x30;
    err = lcd_data(0xCF, p3, 3); if (err != ESP_OK) return err;
    uint8_t p4[4] = {0x64, 0x03, 0x12, 0x81};
    err = lcd_data(0xED, p4, 4); if (err != ESP_OK) return err;
    p3[0] = 0x85; p3[1] = 0x00; p3[2] = 0x78;
    err = lcd_data(0xE8, p3, 3); if (err != ESP_OK) return err;
    uint8_t p5[5] = {0x39, 0x2C, 0x00, 0x34, 0x02};
    err = lcd_data(0xCB, p5, 5); if (err != ESP_OK) return err;
    uint8_t p1[1] = {0x20};
    err = lcd_data(0xF7, p1, 1); if (err != ESP_OK) return err;
    uint8_t p2[2] = {0x00, 0x00};
    err = lcd_data(0xEA, p2, 2); if (err != ESP_OK) return err;
    p1[0] = 0x23;
    err = lcd_data(0xC0, p1, 1); if (err != ESP_OK) return err; /* Power control 1 */
    p1[0] = 0x10;
    err = lcd_data(0xC1, p1, 1); if (err != ESP_OK) return err; /* Power control 2 */
    p2[0] = 0x3E; p2[1] = 0x28;
    err = lcd_data(0xC5, p2, 2); if (err != ESP_OK) return err; /* VCOM control 1 */
    p1[0] = 0x86;
    err = lcd_data(0xC7, p1, 1); if (err != ESP_OK) return err; /* VCOM control 2 */
    p1[0] = 0x48;
    err = lcd_data(0x36, p1, 1); if (err != ESP_OK) return err; /* MADCTL: MX + BGR, portrait */
    p1[0] = 0x55;
    err = lcd_data(0x3A, p1, 1); if (err != ESP_OK) return err; /* COLMOD: 16-bit */
    p2[0] = 0x00; p2[1] = 0x18;
    err = lcd_data(0xB1, p2, 2); if (err != ESP_OK) return err; /* Frame rate */
    p3[0] = 0x08; p3[1] = 0x82; p3[2] = 0x27;
    err = lcd_data(0xB6, p3, 3); if (err != ESP_OK) return err; /* Display function */
    p1[0] = 0x00;
    err = lcd_data(0xF2, p1, 1); if (err != ESP_OK) return err;
    p1[0] = 0x01;
    err = lcd_data(0x26, p1, 1); if (err != ESP_OK) return err; /* Gamma set */
    uint8_t gamma_p[15] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                           0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    err = lcd_data(0xE0, gamma_p, 15); if (err != ESP_OK) return err;
    uint8_t gamma_n[15] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                           0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
    err = lcd_data(0xE1, gamma_n, 15); if (err != ESP_OK) return err;
    err = lcd_cmd(0x11); /* SLPOUT */
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(120));
    err = lcd_cmd(0x29); /* Display ON */
    if (err != ESP_OK) return err;
    err = lcd_cmd(0x20); /* INVOFF — start as on the Mini */
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

/* ---------------- scan tables ---------------- */

typedef struct {
    uint8_t bda[6];
    char name[32];
    int rssi;
} ble_dev_t;

typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    int rssi;
} wifi_net_t;

static ble_dev_t ble_table[MAX_BLE];
static int ble_count = 0;
static wifi_net_t wifi_table[MAX_WIFI];
static int wifi_count = 0;
/* ---------------- v1.7 Radar / Spam mode ----------------
 * op_mode: 0=RADAR (scan-only, v1.6 behaviour), 1=SPAM BLE,
 * 2=SPAM WIFI, 3=SPAM BOTH. Always 0 at boot, whatever was
 * saved — never boot into transmitting spam. mode_sel is the
 * MODE-view cursor (0..3 = a mode, 4 = FUNNY toggle).
 * spam_funny: 1 = funny payloads (default), 0 = normal ones.
 * scan_active: 1 while scanner_task is mid-round; spam_task
 * waits for it to clear so spam never starts while a BLE/WiFi
 * scan is still running. Counters are what the screen shows
 * in SPAM mode instead of stale scan results. */
static volatile int op_mode = 0;
static volatile int mode_sel = 0;
static volatile int spam_funny = 1;
static volatile int scan_active = 0;
static volatile int spam_ble_sent = 0;
static volatile int spam_wifi_sent = 0;
static volatile int spam_ble_on = 0;
static SemaphoreHandle_t sem_adv_data, sem_adv_start, sem_adv_stop;
static const char *const funny_ble_names[10] = {
    "Free Candy", "FBI Surveillance Van", "Virus Detected",
    "Your Phone Is Hacked", "Definitely Not A Tracker", "Send Noods",
    "It Burns When I Pee", "404 Device Not Found", "Mom's iPhone",
    "Error 404",
};
static const char *const normal_ble_names[8] = {
    "Free WiFi", "V7 Radar", "BLE Beacon", "Printer",
    "Smart TV", "Headphones", "Watch", "Speaker",
};
static const char *const funny_wifi_ssids[10] = {
    "Free WiFi", "FBI Surveillance Van", "Pretty Fly For A Wi-Fi",
    "It Hurts When IP", "Virus.exe", "No Free WiFi Here",
    "Tell My WiFi Love Her", "404 Network Not Found",
    "Skynet Global", "The LAN Before Time",
};
static const char *const normal_wifi_ssids[8] = {
    "Free WiFi", "Guest Network", "Coffee Shop WiFi", "Open WiFi",
    "Hotel WiFi", "V7 Radar", "Public WiFi", "Library WiFi",
};
static const char *mode_short_str(int m)
{
    switch (m) {
    case 1: return "SPAM-BLE";
    case 2: return "SPAM-WIFI";
    case 3: return "SPAM-BOTH";
    default: return "RADAR";
    }
}
static const char *mode_name_str(int m)
{
    switch (m) {
    case 1: return "SPAM BLE";
    case 2: return "SPAM WIFI";
    case 3: return "SPAM BOTH";
    default: return "RADAR";
    }
}
static const char *spam_ble_name(int i)
{
    if (spam_funny) return funny_ble_names[i % 10];
    return normal_ble_names[i % 8];
}
static const char *spam_wifi_ssid(int i)
{
    if (spam_funny) return funny_wifi_ssids[i % 10];
    return normal_wifi_ssids[i % 8];
}

static SemaphoreHandle_t dev_lock;
static SemaphoreHandle_t ui_lock;
static SemaphoreHandle_t gps_lock;
static SemaphoreHandle_t sem_scan_param, sem_scan_done;

static void mac_short(const uint8_t *bda, char *out, int out_size)
{
    snprintf(out, out_size, "%02X%02X%02X", bda[3], bda[4], bda[5]);
}

static void name_copy(char *dst, const uint8_t *src, int len)
{
    int n = 0;
    for (int i = 0; i < len && n < 31; i++) {
        uint8_t c = src[i];
        if (c == 0) break;
        if (c >= 0x20 && c <= 0x7E && c != ',') dst[n++] = (char)c;
    }
    dst[n] = '\0';
}

static void parse_adv_name(const uint8_t *buf, int len, ble_dev_t *dev)
{
    int i = 0;
    while (i + 1 < len) {
        int reclen = buf[i];
        if (reclen == 0 || i + reclen >= len) break;
        uint8_t type = buf[i + 1];
        const uint8_t *val = &buf[i + 2];
        int vlen = reclen - 1;
        if (type == 0x09) name_copy(dev->name, val, vlen);
        else if (type == 0x08 && dev->name[0] == '\0') name_copy(dev->name, val, vlen);
        i += reclen + 1;
    }
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        xSemaphoreGive(sem_scan_param);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
        if (xSemaphoreTake(dev_lock, pdMS_TO_TICKS(100)) != pdTRUE) break;
        int idx = -1;
        for (int i = 0; i < ble_count; i++) {
            if (memcmp(ble_table[i].bda, param->scan_rst.bda, 6) == 0) { idx = i; break; }
        }
        if (idx < 0) {
            if (ble_count >= MAX_BLE) { xSemaphoreGive(dev_lock); break; }
            idx = ble_count++;
            memset(&ble_table[idx], 0, sizeof(ble_dev_t));
            memcpy(ble_table[idx].bda, param->scan_rst.bda, 6);
            ble_table[idx].rssi = -127;
        }
        ble_dev_t *dev = &ble_table[idx];
        if (param->scan_rst.rssi > dev->rssi) dev->rssi = param->scan_rst.rssi;
        parse_adv_name(param->scan_rst.ble_adv, param->scan_rst.adv_data_len, dev);
        if (param->scan_rst.scan_rsp_len > 0) {
            parse_adv_name(param->scan_rst.ble_adv + param->scan_rst.adv_data_len,
                           param->scan_rst.scan_rsp_len, dev);
        }
        xSemaphoreGive(dev_lock);
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
    case ESP_GAP_BLE_SCAN_TIMEOUT_EVT:
        xSemaphoreGive(sem_scan_done);
        break;
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if (sem_adv_data) xSemaphoreGive(sem_adv_data);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (sem_adv_start) xSemaphoreGive(sem_adv_start);
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (sem_adv_stop) xSemaphoreGive(sem_adv_stop);
        break;
    default:
        break;
    }
}

/* ---------------- SD card (mount in own task, never blocks UI) ---------------- */

static int sd_ready = 0;
static int sd_checked = 0; /* 1 once the mount task has reported */
static const char *sd_status_str = "SD ?";
static SemaphoreHandle_t sem_sd_mount;
static int sd_mount_result = 0;

/* Short tag for the SD failure, for the 40-char status line.
 * The full esp_err_to_name() string goes in the serial log. */
static char sd_fail_tag[10] = "";

static const char *sd_fail_short(esp_err_t err)
{
    switch (err) {
        case ESP_ERR_INVALID_STATE:   return "NOBUS";
        case ESP_ERR_TIMEOUT:         return "TIMEOUT";
        case ESP_ERR_INVALID_RESPONSE:return "NORESP";
        case ESP_ERR_INVALID_CRC:     return "CRC";
        case ESP_ERR_NOT_FOUND:       return "NOCARD";
        case ESP_FAIL:                return "FAIL";
        default:                      return "ERR";
    }
}

static void sd_mount_task(void *arg)
{
    (void)arg;
    /* v1.4 SD FIX: v1.3 mounted on SPI2_HOST, but SPI2 was never
     * initialised, so spi_bus_add_device() failed with
     * ESP_ERR_INVALID_STATE on every boot -> "SD FAIL". The V7 SD
     * lines are MOSI 23 / MISO 19 / SCK 18 — the DISPLAY's bus,
     * SPI3_HOST, already initialised in app_main before this task
     * starts (Koko configs.h MARAUDER_V7: SD_MISO/MOSI/SCK =
     * TFT_MISO/MOSI/SCLK). SD joins that shared bus as a second
     * device, CS 4 unchanged. The SPI bus driver serialises
     * display DMA and SD transfers, as Marauder itself does. */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SPI3_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    esp_err_t err = ESP_FAIL;
    /* v1.5: hold spi3_lock for the whole mount. Mounting is an SD
     * polling transfer on SPI3 and must not start while display DMA
     * is in flight (v1.4 panic). 5 s timeout: a frame holds the lock
     * for well under a second, so a timeout means something is
     * genuinely stuck — log it and give up the mount, never block
     * this task forever. */
    int lock_ok = 0;
    if (spi3_lock && xSemaphoreTake(spi3_lock, pdMS_TO_TICKS(5000)) == pdTRUE) {
        lock_ok = 1;
    } else {
        ESP_LOGW(TAG, "SD mount: spi3_lock take timeout — mount skipped");
    }
    if (lock_ok) {
        for (int attempt = 1; attempt <= 2 && err != ESP_OK; attempt++) {
            ESP_LOGI(TAG, "SD mount try %d/2: SPI3_HOST (shared display bus), CS 4", attempt);
            err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount_cfg, &card);
            ESP_LOGI(TAG, "SD mount try %d -> %s", attempt, esp_err_to_name(err));
            if (err != ESP_OK && attempt == 1) vTaskDelay(pdMS_TO_TICKS(500));
        }
        xSemaphoreGive(spi3_lock);
    }
    sd_mount_result = (err == ESP_OK) ? 1 : 0;
    if (err != ESP_OK) {
        snprintf(sd_fail_tag, sizeof(sd_fail_tag), "%s", sd_fail_short(err));
        ESP_LOGW(TAG, "SD mount failed: %s (tag %s)", esp_err_to_name(err), sd_fail_tag);
    } else {
        ESP_LOGI(TAG, "SD mounted");
        sdmmc_card_print_info(stdout, card);
    }
    xSemaphoreGive(sem_sd_mount);
    vTaskDelete(NULL);
}

/* Check (without blocking) whether the mount task has reported. */
static void sd_poll_result(void)
{
    if (sd_checked) return;
    if (xSemaphoreTake(sem_sd_mount, 0) == pdTRUE) {
        sd_checked = 1;
        if (sd_mount_result == 1) { sd_ready = 1; sd_status_str = "SD OK"; }
        else sd_status_str = "SD FAIL";
    }
}

static void csv_clean(char *dst, const char *src, int maxlen)
{
    int n = 0;
    for (int i = 0; src[i] && n < maxlen - 1; i++) {
        char c = src[i];
        if (c == ',' || c == '\n' || c == '\r') c = ' ';
        dst[n++] = c;
    }
    dst[n] = '\0';
}

/* ---------------- Battery gauge: MAX17048 on I2C SDA 33 / SCL 16 ----------------
 * Koko configs.h MARAUDER_V7: HAS_MAX1704X, I2C address 0x36.
 * SOC reg 0x04: MSB = whole %, LSB = 1/256 %. VCELL reg 0x02:
 * 12-bit value in bits 15:4, 1.25 mV per bit. VERSION reg 0x08
 * reads 0x0012 on a MAX17048. Read at startup and every 30 s by
 * bat_task; the radar never blocks on it. If the gauge does not
 * answer, bat_ok stays 0, the screen shows "BAT --%", and the
 * radar keeps running — never abort, never park. */
static volatile int bat_pct = -1;  /* -1 = unknown / no gauge */
static volatile int bat_mv = 0;
static volatile int bat_ok = 0;

/* Bit-banged I2C master for the MAX17048. Deliberately NOT the
 * IDF i2c driver: v1.3 already fills IRAM to 129,651 of 131,072
 * bytes, and adding esp_driver_i2c overflowed IRAM0 by 668 bytes
 * at link time. Bit-banging is ~60 lines in flash, costs no IRAM,
 * and the gauge is read only once every 30 s. Open-drain lines
 * with internal pull-ups on (the Adafruit board also has 10k
 * pull-ups); 5 us half-bit ~= 100 kHz. The gauge never stretches
 * the clock, so no SCL polling is needed. */
static void bat_i2c_delay(void) { esp_rom_delay_us(5); }

static void bat_i2c_sda(int v)
{
    if (v) { gpio_set_direction(PIN_BAT_SDA, GPIO_MODE_INPUT); }
    else { gpio_set_level(PIN_BAT_SDA, 0); gpio_set_direction(PIN_BAT_SDA, GPIO_MODE_OUTPUT_OD); }
}

static void bat_i2c_scl(int v)
{
    if (v) { gpio_set_direction(PIN_BAT_SCL, GPIO_MODE_INPUT); }
    else { gpio_set_level(PIN_BAT_SCL, 0); gpio_set_direction(PIN_BAT_SCL, GPIO_MODE_OUTPUT_OD); }
}

static void bat_i2c_start(void)
{
    bat_i2c_sda(1); bat_i2c_scl(1); bat_i2c_delay();
    bat_i2c_sda(0); bat_i2c_delay();
    bat_i2c_scl(0); bat_i2c_delay();
}

static void bat_i2c_stop(void)
{
    bat_i2c_sda(0); bat_i2c_scl(1); bat_i2c_delay();
    bat_i2c_sda(1); bat_i2c_delay();
}

static int bat_i2c_write_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        bat_i2c_sda((b >> i) & 1);
        bat_i2c_delay();
        bat_i2c_scl(1); bat_i2c_delay();
        bat_i2c_scl(0); bat_i2c_delay();
    }
    bat_i2c_sda(1); /* release for ACK */
    bat_i2c_delay();
    bat_i2c_scl(1); bat_i2c_delay();
    int ack = (gpio_get_level(PIN_BAT_SDA) == 0);
    bat_i2c_scl(0); bat_i2c_delay();
    return ack;
}

static uint8_t bat_i2c_read_byte(int ack)
{
    uint8_t b = 0;
    bat_i2c_sda(1); /* release, slave drives */
    for (int i = 7; i >= 0; i--) {
        bat_i2c_delay();
        bat_i2c_scl(1); bat_i2c_delay();
        if (gpio_get_level(PIN_BAT_SDA)) b |= (1 << i);
        bat_i2c_scl(0); bat_i2c_delay();
    }
    bat_i2c_sda(ack ? 0 : 1);
    bat_i2c_delay();
    bat_i2c_scl(1); bat_i2c_delay();
    bat_i2c_scl(0); bat_i2c_delay();
    bat_i2c_sda(1);
    return b;
}

static esp_err_t bat_read_reg16(uint8_t reg, uint16_t *out)
{
    bat_i2c_start();
    if (!bat_i2c_write_byte((BAT_I2C_ADDR << 1) | 0)) { bat_i2c_stop(); return ESP_ERR_INVALID_RESPONSE; }
    if (!bat_i2c_write_byte(reg)) { bat_i2c_stop(); return ESP_ERR_INVALID_RESPONSE; }
    bat_i2c_start(); /* repeated start */
    if (!bat_i2c_write_byte((BAT_I2C_ADDR << 1) | 1)) { bat_i2c_stop(); return ESP_ERR_INVALID_RESPONSE; }
    uint8_t hi = bat_i2c_read_byte(1);
    uint8_t lo = bat_i2c_read_byte(0);
    bat_i2c_stop();
    *out = (uint16_t)((hi << 8) | lo);
    return ESP_OK;
}

static void bat_read_once(void)
{
    uint16_t soc = 0, vcell = 0;
    esp_err_t err = bat_read_reg16(BAT_REG_SOC, &soc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BAT: SOC read failed: %s (keeping last value)", esp_err_to_name(err));
        return;
    }
    err = bat_read_reg16(BAT_REG_VCELL, &vcell);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BAT: VCELL read failed: %s (keeping last value)", esp_err_to_name(err));
        return;
    }
    int pct = (int)(soc / 256);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int mv = (int)(((vcell >> 4) * 125) / 100); /* 1.25 mV per bit */
    bat_pct = pct;
    bat_mv = mv;
    bat_ok = 1;
    ESP_LOGI(TAG, "BAT: %d%% %d mV (SOC raw 0x%04x VCELL raw 0x%04x)", pct, mv, soc, vcell);
}

static void bat_task(void *arg)
{
    (void)arg;
    /* Idle the bus high, with internal pull-ups on. */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BAT_SDA) | (1ULL << PIN_BAT_SCL),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_BAT_SDA, 1);
    gpio_set_level(PIN_BAT_SCL, 1);
    /* Probe: VERSION register should read 0x0012 (MAX17048). */
    uint16_t ver = 0;
    esp_err_t err = bat_read_reg16(BAT_REG_VER, &ver);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BAT: no gauge at I2C 0x36 (VERSION read: %s) — showing BAT --%%", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "BAT: MAX17048 found, VERSION 0x%04x", ver);
    for (;;) {
        bat_read_once();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* "BAT 87%" or "BAT --%" for the top status line. */
static void bat_status_str(char *dst, size_t n)
{
    if (bat_ok && bat_pct >= 0) snprintf(dst, n, "BAT %d%%", bat_pct);
    else snprintf(dst, n, "BAT --%%");
}

/* Small battery bar on the top status row (row 1, y 9..14),
 * top-right corner, drawn with raw pixels (no centre offset).
 * Outline green, fill green/yellow/red by level, grey outline
 * with no fill when the gauge is unknown. Called after the
 * status text row, so the row's bg fill cannot paint over it. */
static void draw_battery_bar(void)
{
    const int x0 = 206, y0 = 9, w = 28, h = 6;
    uint16_t col = COL_GREY;
    if (bat_ok && bat_pct >= 0) {
        if (bat_pct >= 50) col = COL_GREEN;
        else if (bat_pct >= 20) col = COL_YELLOW;
        else col = COL_RED;
    }
    for (int x = x0; x < x0 + w; x++) {
        fb_pixel_raw(x, y0, col);
        fb_pixel_raw(x, y0 + h - 1, col);
    }
    for (int y = y0; y < y0 + h; y++) {
        fb_pixel_raw(x0, y, col);
        fb_pixel_raw(x0 + w - 1, y, col);
        fb_pixel_raw(x0 + w, y, col); /* nub */
        fb_pixel_raw(x0 + w, y + 1, col);
    }
    if (bat_ok && bat_pct >= 0) {
        int fill = (bat_pct * (w - 2)) / 100;
        for (int x = x0 + 1; x < x0 + 1 + fill; x++)
            for (int y = y0 + 1; y < y0 + h - 1; y++)
                fb_pixel_raw(x, y, col);
    }
}

/* ---------------- GPS: NMEA parsing, hunt, heading ---------------- */

typedef struct {
    int fix;
    int sats;
    double lat;
    double lon;
    double heading_deg; /* course over ground, from movement */
    int heading_valid;
} gps_state_t;

static gps_state_t gps_state = {0, 0, 0.0, 0.0, 0.0, 0};
static int gps_rx_pin = -1;   /* -1 = no GPS UART found */
static int gps_found = 0;
static double gps_prev_lat = 0.0, gps_prev_lon = 0.0;
static int gps_prev_set = 0;

static double nmea_to_deg(const char *field, char hemi)
{
    if (!field || field[0] == '\0') return 0.0;
    double v = atof(field);
    int deg = (int)(v / 100);
    double minutes = v - deg * 100;
    double out = deg + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W') out = -out;
    return out;
}

static int nmea_split(char *line, char *fields[], int max_fields)
{
    int n = 0;
    fields[n++] = line;
    for (char *p = line; *p && n < max_fields; p++) {
        if (*p == ',') { *p = '\0'; fields[n++] = p + 1; }
    }
    return n;
}

static double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * 3.14159265 / 180.0;
    double dlon = (lon2 - lon1) * 3.14159265 / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * 3.14159265 / 180.0) * cos(lat2 * 3.14159265 / 180.0) *
               sin(dlon / 2) * sin(dlon / 2);
    if (a > 1.0) a = 1.0;
    return 2 * 6371000.0 * asin(sqrt(a));
}

static double bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double la1 = lat1 * 3.14159265 / 180.0, la2 = lat2 * 3.14159265 / 180.0;
    double dlon = (lon2 - lon1) * 3.14159265 / 180.0;
    double y = sin(dlon) * cos(la2);
    double x = cos(la1) * sin(la2) - sin(la1) * cos(la2) * cos(dlon);
    double b = atan2(y, x) * 180.0 / 3.14159265;
    if (b < 0) b += 360.0;
    return b;
}

static void gps_parse_line(char *line)
{
    if (strncmp(line, "$GNGGA", 6) != 0 && strncmp(line, "$GPGGA", 6) != 0 &&
        strncmp(line, "$GNRMC", 6) != 0 && strncmp(line, "$GPRMC", 6) != 0) return;
    int is_gga = (line[3] == 'G' && line[4] == 'G' && line[5] == 'A');
    char *f[20];
    int n = nmea_split(line, f, 20);
    if (xSemaphoreTake(gps_lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    double new_lat = gps_state.lat, new_lon = gps_state.lon;
    int new_fix = gps_state.fix;
    if (is_gga && n >= 8) {
        int quality = atoi(f[6]);
        new_fix = (quality > 0) ? 1 : 0;
        gps_state.sats = atoi(f[7]);
        if (new_fix) { new_lat = nmea_to_deg(f[2], f[3][0]); new_lon = nmea_to_deg(f[4], f[5][0]); }
    } else if (!is_gga && n >= 7) {
        if (f[2][0] == 'A') {
            new_fix = 1;
            new_lat = nmea_to_deg(f[3], f[4][0]);
            new_lon = nmea_to_deg(f[5], f[6][0]);
        } else new_fix = 0;
    }
    if (new_fix) {
        if (gps_prev_set) {
            double moved = haversine_m(gps_prev_lat, gps_prev_lon, new_lat, new_lon);
            if (moved > 7.0) {
                gps_state.heading_deg = bearing_deg(gps_prev_lat, gps_prev_lon, new_lat, new_lon);
                gps_state.heading_valid = 1;
                gps_prev_lat = new_lat; gps_prev_lon = new_lon;
            }
        } else {
            gps_prev_lat = new_lat; gps_prev_lon = new_lon; gps_prev_set = 1;
        }
        gps_state.lat = new_lat; gps_state.lon = new_lon;
    }
    gps_state.fix = new_fix;
    xSemaphoreGive(gps_lock);
}

static void gps_task(void *arg)
{
    (void)arg;
    uint8_t buf[256];
    char line[128];
    int line_len = 0;
    while (1) {
        int n = uart_read_bytes(UART_NUM_2, buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                line[line_len] = '\0';
                if (line_len > 0) gps_parse_line(line);
                line_len = 0;
            } else if (c != '\r' && line_len < (int)sizeof(line) - 1) {
                line[line_len++] = c;
            }
        }
    }
}

/* ---------------- boot counter + reset reason (v2.3, ported from v2.1) ----------------
 * David's video (2026-11-05) shows the BOOTING screen more than once in
 * 5.5 s, with ~0.5 s of full white between screens. BOOTING is only
 * printed once per app_main, so that means the board is restarting.
 * Count boots in NVS and print the count + reset reason on the boot
 * screen, so a reboot loop is visible at a glance instead of guessed.
 * The counter is cleared after the first full scan round completes on a
 * boot where GPS is up (V7: fixed UART, no hunt). */
static int boot_count = 0;
static int hunt_done = 0; /* 1 once GPS is up (V7: fixed UART init) */

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

static void boot_counter_bump(void)
{
    nvs_handle_t h;
    if (nvs_open("radarv7", NVS_READWRITE, &h) != ESP_OK) return;
    int32_t n = 0;
    nvs_get_i32(h, "boot_n", &n);
    boot_count = (int)n + 1;
    nvs_set_i32(h, "boot_n", boot_count);
    nvs_commit(h);
    nvs_close(h);
}

static void boot_counter_clear(void)
{
    nvs_handle_t h;
    if (nvs_open("radarv7", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "boot_n", 0);
    nvs_commit(h);
    nvs_close(h);
    boot_count = 0;
}

/* GPS: FIXED pins on the V7 (v1) — the hunt is DELETED.
 * Koko configs.h HAS_GPS, UART2, TX 21 / RX 22; David's photo
 * confirms the ATGM336H module on his V7. No probing, no pin
 * list, no GPIO12 — the Mini's boot-panic suspect is gone by
 * construction. GPIO21 is GPS TX only: never drive it as
 * TOUCH_CS (the setup file defines TOUCH_CS 21, but Marauder's
 * V7 has no touch — buttons only). hunt_done is set to 1 here
 * so the scanner's boot-counter clear (after a full scan round)
 * still works exactly as before. */
static void gps_fixed_init(void)
{
    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_param_config(UART_NUM_2, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART param config failed");
        hunt_done = 1;
        return;
    }
    if (uart_set_pin(UART_NUM_2, 21, 22, -1, -1) != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART pin config failed");
        hunt_done = 1;
        return;
    }
    if (uart_driver_install(UART_NUM_2, 4096, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART driver install failed");
        hunt_done = 1;
        return;
    }
    gps_rx_pin = 22;
    gps_found = 1;
    hunt_done = 1;
    ESP_LOGI(TAG, "GPS fixed: UART2 TX 21 / RX 22, 9600 baud (ATGM336H)");
    xTaskCreate(gps_task, "gps_task", 4096, NULL, 5, NULL);
}

/* ---------------- saved spots: SPOT + PARKING in NVS ---------------- */

typedef struct {
    int valid;
    double lat;
    double lon;
} saved_pin_t;

static saved_pin_t pin_spot = {0, 0.0, 0.0};
static saved_pin_t pin_parking = {0, 0.0, 0.0};

static void pins_load(void)
{
    nvs_handle_t h;
    if (nvs_open("radarv7", NVS_READONLY, &h) != ESP_OK) return;
    int32_t la = 0, lo = 0; uint8_t ok = 0;
    if (nvs_get_i32(h, "spot_lat", &la) == ESP_OK &&
        nvs_get_i32(h, "spot_lon", &lo) == ESP_OK &&
        nvs_get_u8(h, "spot_ok", &ok) == ESP_OK && ok) {
        pin_spot.valid = 1; pin_spot.lat = la / 1e6; pin_spot.lon = lo / 1e6;
    }
    if (nvs_get_i32(h, "park_lat", &la) == ESP_OK &&
        nvs_get_i32(h, "park_lon", &lo) == ESP_OK &&
        nvs_get_u8(h, "park_ok", &ok) == ESP_OK && ok) {
        pin_parking.valid = 1; pin_parking.lat = la / 1e6; pin_parking.lon = lo / 1e6;
    }
    /* Centre offset (v2.2): the in-memory default is 0,0 on the
     * V7 (fresh 240x320 layout — no Mini X+2 Y+4), so a saved
     * value only overrides
     * it when one exists. Clamped, so a corrupt or out-of-range
     * value can never push the view off the glass for good. */
    int32_t ms = 0;
    if (nvs_get_i32(h, "mode_saved", &ms) == ESP_OK && ms >= 0 && ms <= 3) mode_sel = (int)ms;
    uint8_t fn = 1;
    if (nvs_get_u8(h, "funny", &fn) == ESP_OK) spam_funny = fn ? 1 : 0;
    int32_t ox = 0, oy = 0;
    if (nvs_get_i32(h, "off_x", &ox) == ESP_OK) view_off_x = clamp_off((int)ox);
    if (nvs_get_i32(h, "off_y", &oy) == ESP_OK) view_off_y = clamp_off((int)oy);
    nvs_close(h);
}

/* Centre offset save (v2.2): called from the CENTER view when
 * David presses CENTER. Survives reboot and re-flash of this
 * same firmware (same NVS namespace as the saved spots). */
static void offset_save(void)
{
    nvs_handle_t h;
    if (nvs_open("radarv7", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "off_x", view_off_x);
    nvs_set_i32(h, "off_y", view_off_y);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "centre offset saved X=%d Y=%d", view_off_x, view_off_y);
}

/* v1.7: persist the last applied mode (as the MODE-view cursor)
 * and the Funny flag. The boot mode itself is NOT persisted —
 * op_mode is always 0 (RADAR) at boot, by construction. */
static void mode_save(void)
{
    nvs_handle_t h;
    if (nvs_open("radarv7", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "mode_saved", op_mode);
    nvs_set_u8(h, "funny", spam_funny ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

/* v1.7: apply a mode. Called from input_task (which already holds
 * ui_lock). Switching to RADAR stops transmitting: the spam task
 * checks op_mode before every send and stops advertising after
 * its current ~800 ms advert hold, so spam is off within ~1 s.
 * Switching to SPAM does not start anything here — spam_task
 * starts once scanner_task is idle (scan_active == 0). */
static void set_op_mode(int m)
{
    if (m < 0 || m > 3) return;
    int old = op_mode;
    op_mode = m;
    mode_sel = m;
    mode_save();
    ESP_LOGI(TAG, "MODE: %s -> %s (funny %s)", mode_name_str(old),
             mode_name_str(m), spam_funny ? "ON" : "OFF");
}

static void pin_save(int is_parking, double lat, double lon)
{
    nvs_handle_t h;
    if (nvs_open("radarv7", NVS_READWRITE, &h) != ESP_OK) return;
    const char *la_k = is_parking ? "park_lat" : "spot_lat";
    const char *lo_k = is_parking ? "park_lon" : "spot_lon";
    const char *ok_k = is_parking ? "park_ok" : "spot_ok";
    nvs_set_i32(h, la_k, (int32_t)(lat * 1e6));
    nvs_set_i32(h, lo_k, (int32_t)(lon * 1e6));
    nvs_set_u8(h, ok_k, 1);
    nvs_commit(h);
    nvs_close(h);
    if (is_parking) { pin_parking.valid = 1; pin_parking.lat = lat; pin_parking.lon = lon; }
    else { pin_spot.valid = 1; pin_spot.lat = lat; pin_spot.lon = lon; }
}

/* ---------------- UI state + rendering ---------------- */

static int ui_view = VIEW_RADAR;
static int ui_sel = 0;        /* LIST scroll index */
static int ui_frozen = 0;
static int ui_finder_slot = 0; /* 0 = SPOT, 1 = PARKING */
static char ui_msg[22] = "";
static TickType_t ui_msg_until = 0;
static int sweep_deg = 0;

static const char *compass8(double deg)
{
    static const char *names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int i = (int)((deg + 22.5) / 45.0) % 8;
    return names[i];
}

static int rssi_to_radius(int rssi)
{
    if (rssi >= -40) return 6;
    if (rssi <= -90) return RADAR_R;
    return 6 + ((-40 - rssi) * (RADAR_R - 6)) / 50;
}

/* ---------------- v2.1 radar rendering: persistent framebuffer ----------------
 * v2.0 did fb_fill(COL_BLACK) + full render + fb_push() every 250 ms.
 * That full clear/redraw every frame is what David saw as a white
 * flash. v2.1 fills the framebuffer to black ONCE, on view change
 * (ui_task), and per tick only erases the old sweep line and the old
 * blip squares, redraws the rings, the new sweep line and the blips,
 * and redraws the text rows in place (each text row overwrites only
 * its own 8 px row). Nothing is ever wiped whole-screen per frame.
 * Dots fade behind the sweep: bright just after the sweep passes
 * them, dim for a while, then off — by angular distance behind the
 * sweep, never by wiping the screen. */

/* v1.3: no prev-blip state. Each frame is drawn fresh, band
 * by band, from the current sweep angle — nothing to erase. */

static void radar_rings(void)
{
    fb_circle(RADAR_CX, RADAR_CY, 36, COL_DGREEN);
    fb_circle(RADAR_CX, RADAR_CY, 74, COL_DGREEN);
    fb_circle(RADAR_CX, RADAR_CY, RADAR_R, COL_GREEN);
}

static void radar_sweep_line(int deg, uint16_t color)
{
    double rad = deg * 3.14159265 / 180.0;
    fb_line(RADAR_CX, RADAR_CY,
            RADAR_CX + (int)(RADAR_R * cos(rad)),
            RADAR_CY + (int)(RADAR_R * sin(rad)), color);
}

static void radar_text_rows(void)
{
    /* v2.2: the title is one line only — "BLE&WIFI RADAR",
     * centred, nothing else on that line. Device counts are
     * already visible as dots and on the B:/W: lines below.
     * v1.6: title at y=8 (7 px clear of the top border at y=0),
     * status at y=16 — both fixed header chrome, never shifted
     * by the centre offset. */
    fb_text_centered_at(8, "BLE&WIFI RADAR", COL_GREEN, COL_BLACK);
    char status[40];
    char batstr[10];
    bat_status_str(batstr, sizeof(batstr));
    int fix = 0, sats = 0;
    if (xSemaphoreTake(gps_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        fix = gps_state.fix; sats = gps_state.sats;
        xSemaphoreGive(gps_lock);
    }
    /* SD tag: "OK", "?" while mounting, or the short fail reason
     * (NOBUS / TIMEOUT / NORESP / CRC / NOCARD / FAIL) so the
     * screen says why, not just FAIL. Full error is in the log. */
    char sdtag[10];
    if (sd_ready) snprintf(sdtag, sizeof(sdtag), "OK");
    else if (sd_checked) snprintf(sdtag, sizeof(sdtag), "%s", sd_fail_tag[0] ? sd_fail_tag : "FAIL");
    else snprintf(sdtag, sizeof(sdtag), "?");
    if (op_mode != 0) snprintf(status, sizeof(status), "%s SD %s %s", mode_short_str(op_mode), sdtag, batstr);
    else if (ui_frozen) snprintf(status, sizeof(status), "FROZEN SD %s %s", sdtag, batstr);
    else if (!gps_found) snprintf(status, sizeof(status), "GPS NONE SD %s %s", sdtag, batstr);
    else if (fix) snprintf(status, sizeof(status), "GPS FIX %d SD %s %s", sats, sdtag, batstr);
    else snprintf(status, sizeof(status), "GPS SEARCH SD %s %s", sdtag, batstr);
    fb_text_row_at(16, status, COL_YELLOW, COL_BLACK);

    char best_ble[34] = "", best_wifi[34] = "";
    if (xSemaphoreTake(dev_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        int best_ble_rssi = -127, best_wifi_rssi = -127;
        for (int i = 0; i < ble_count; i++) {
            if (ble_table[i].rssi > best_ble_rssi) {
                best_ble_rssi = ble_table[i].rssi;
                if (ble_table[i].name[0] != '\0')
                    snprintf(best_ble, sizeof(best_ble), "%.30s", ble_table[i].name);
                else mac_short(ble_table[i].bda, best_ble, sizeof(best_ble));
            }
        }
        for (int i = 0; i < wifi_count; i++) {
            if (wifi_table[i].rssi > best_wifi_rssi) {
                best_wifi_rssi = wifi_table[i].rssi;
                snprintf(best_wifi, sizeof(best_wifi), "%.30s", wifi_table[i].ssid);
            }
        }
        xSemaphoreGive(dev_lock);
    }
    char line[42];
    if (op_mode == 1 || op_mode == 3) snprintf(line, sizeof(line), "B: SPAM %d", (int)spam_ble_sent);
    else if (op_mode != 0) snprintf(line, sizeof(line), "B: OFF");
    else if (best_ble[0] != '\0') snprintf(line, sizeof(line), "B: %.30s", best_ble);
    else snprintf(line, sizeof(line), "B: --");
    fb_text_row(38, line, COL_BLUE, COL_BLACK);
    /* A transient message (e.g. OFFSET SAVED after leaving the
     * CENTER view) takes the bottom row for a few seconds. */
    int msg_active = (ui_msg[0] != '\0' && xTaskGetTickCount() < ui_msg_until);
    if (msg_active) fb_text_row(39, ui_msg, COL_YELLOW, COL_BLACK);
    else {
        if (op_mode == 2 || op_mode == 3) snprintf(line, sizeof(line), "W: SPAM %d", (int)spam_wifi_sent);
        else if (op_mode != 0) snprintf(line, sizeof(line), "W: OFF");
        else if (best_wifi[0] != '\0') snprintf(line, sizeof(line), "W: %.30s", best_wifi);
        else snprintf(line, sizeof(line), "W: --");
        fb_text_row(39, line, COL_YELLOW, COL_BLACK);
    }
}

/* Draw the blips for the current sweep position, with fading.
 * Golden-angle spread so dots do not pile on one line, radius by
 * signal strength (strong = close to centre). Age = degrees the
 * sweep has moved past the dot's angle: <45 bright, <150 dim,
 * older than that off (already erased by erase_prev_blips). */
static void radar_draw_blips(int sweep_now)
{
    if (xSemaphoreTake(dev_lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    for (int i = 0; i < ble_count; i++) {
        double ang = fmod(i * 137.0, 360.0);
        int age = (sweep_now - (int)ang) % 360;
        if (age < 0) age += 360;
        if (age >= 150) continue;
        int r = rssi_to_radius(ble_table[i].rssi);
        int bx = RADAR_CX + (int)(r * cos(ang * 3.14159265 / 180.0));
        int by = RADAR_CY + (int)(r * sin(ang * 3.14159265 / 180.0));
        uint16_t col = (age < 45) ? COL_BLUE : COL_DBLUE;
        fb_blip(bx, by, ble_table[i].rssi >= -50 ? 5 : 3, col);
    }
    for (int i = 0; i < wifi_count; i++) {
        double ang = fmod(i * 137.0 + 60.0, 360.0);
        int age = (sweep_now - (int)ang) % 360;
        if (age < 0) age += 360;
        if (age >= 150) continue;
        int r = rssi_to_radius(wifi_table[i].rssi);
        int bx = RADAR_CX + (int)(r * cos(ang * 3.14159265 / 180.0));
        int by = RADAR_CY + (int)(r * sin(ang * 3.14159265 / 180.0));
        uint16_t col = (age < 45) ? COL_YELLOW : COL_DYELLOW;
        fb_blip(bx, by, wifi_table[i].rssi >= -50 ? 5 : 3, col);
    }
    xSemaphoreGive(dev_lock);
}

/* v1.3: stateless full render. Each frame is drawn fresh, band
 * by band, from the current sweep angle and scan tables — no
 * incremental erase, no persistent framebuffer. Dots fade behind
 * the sweep by angular distance, as before. */
static void render_frame_radar(void)
{
    radar_text_rows();
    draw_battery_bar();
    radar_rings();
    radar_sweep_line(sweep_deg, COL_GREEN);
    radar_draw_blips(sweep_deg);
    fb_blip(RADAR_CX, RADAR_CY, 3, COL_WHITE);
    /* Border last: the title/status rows' bg fill would otherwise
     * paint over the top/bottom edge rows. */
    fb_border(COL_GREEN);
    draw_version_tag();
}

static void render_finder(void)
{
    /* v2.2: the title is one line only — "BLE&WIFI RADAR".
     * v1.6: title at y=8, clear of the border; header rows fixed. */
    fb_text_centered_at(8, "BLE&WIFI RADAR", COL_GREEN, COL_BLACK);
    saved_pin_t *pin = ui_finder_slot ? &pin_parking : &pin_spot;
    const char *slot_name = ui_finder_slot ? "PARKING" : "SPOT";
    char line[40];
    if (op_mode != 0) snprintf(line, sizeof(line), "> %.7s %.5s %s", slot_name, pin->valid ? "SAVED" : "--", mode_short_str(op_mode));
    else snprintf(line, sizeof(line), "> %.7s %.5s", slot_name, pin->valid ? "SAVED" : "--");
    fb_text_row_at(16, line, COL_WHITE, COL_BLACK);

    int msg_active = (ui_msg[0] != '\0' && xTaskGetTickCount() < ui_msg_until);

    gps_state_t g;
    if (xSemaphoreTake(gps_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        g = gps_state;
        xSemaphoreGive(gps_lock);
    } else {
        g.fix = 0; g.sats = 0; g.lat = 0; g.lon = 0; g.heading_valid = 0; g.heading_deg = 0;
    }

    if (!gps_found) {
        fb_text_row(3, "GPS NOT FOUND", COL_RED, COL_BLACK);
        fb_text_row(4, "RADAR STILL WORKS", COL_WHITE, COL_BLACK);
        fb_text_row(5, "GPS UART 21/22", COL_WHITE, COL_BLACK);
    } else if (!pin->valid) {
        fb_text_row(3, "NO PIN SAVED YET", COL_YELLOW, COL_BLACK);
        fb_text_row(4, "GO TO THE SPOT", COL_WHITE, COL_BLACK);
        fb_text_row(5, "CENTER TO SAVE IT", COL_WHITE, COL_BLACK);
    } else if (!g.fix) {
        fb_text_row(3, "NO GPS FIX", COL_RED, COL_BLACK);
        fb_text_row(4, "GO OUTSIDE", COL_WHITE, COL_BLACK);
        fb_text_row(5, "WAIT FOR SATS", COL_WHITE, COL_BLACK);
    } else {
        double dist_m = haversine_m(g.lat, g.lon, pin->lat, pin->lon);
        double brg = bearing_deg(g.lat, g.lon, pin->lat, pin->lon);
        if (dist_m < 10.0) {
            fb_text_row(3, "YOU ARE HERE", COL_GREEN, COL_BLACK);
            snprintf(line, sizeof(line), "%.0f FT", dist_m * 3.28084);
            fb_text_row(4, line, COL_GREEN, COL_BLACK);
        } else if (dist_m < 1609.0) {
            snprintf(line, sizeof(line), "%.0f FT", dist_m * 3.28084);
            fb_text_row(3, line, COL_GREEN, COL_BLACK);
            snprintf(line, sizeof(line), "%s %.0f DEG", compass8(brg), brg);
            fb_text_row(4, line, COL_WHITE, COL_BLACK);
        } else {
            snprintf(line, sizeof(line), "%.1f MI", dist_m / 1609.34);
            fb_text_row(3, line, COL_GREEN, COL_BLACK);
            snprintf(line, sizeof(line), "%s %.0f DEG", compass8(brg), brg);
            fb_text_row(4, line, COL_WHITE, COL_BLACK);
        }
        if (g.heading_valid) {
            fb_text_row(5, "ARROW = WALK DIR", COL_WHITE, COL_BLACK);
        } else {
            fb_text_row(5, "N-UP: WALK TO TURN", COL_YELLOW, COL_BLACK);
        }
        /* Big arrow, centred on the panel (64, 88), radius 30.
         * Relative to your walking direction when known, else
         * north-up. */
        double rel = g.heading_valid ? (brg - g.heading_deg) : brg;
        double arad = (rel - 90.0) * 3.14159265 / 180.0;
        int ax = FINDER_CX + (int)(FINDER_R * cos(arad));
        int ay = FINDER_CY + (int)(FINDER_R * sin(arad));
        fb_circle(FINDER_CX, FINDER_CY, FINDER_R, COL_DGREEN);
        fb_line(FINDER_CX, FINDER_CY, ax, ay, COL_GREEN);
        double h1 = arad + 2.5, h2 = arad - 2.5;
        fb_line(ax, ay, ax + (int)(10 * cos(h1)), ay + (int)(10 * sin(h1)), COL_GREEN);
        fb_line(ax, ay, ax + (int)(10 * cos(h2)), ay + (int)(10 * sin(h2)), COL_GREEN);
        fb_blip(FINDER_CX, FINDER_CY, 3, COL_WHITE);
    }
    /* Other slot, and the footer — moved to the bottom rows of the
     * 160-row panel (17/18/19), so they sit at the bottom of the
     * glass instead of mid-screen. */
    saved_pin_t *other = ui_finder_slot ? &pin_spot : &pin_parking;
    snprintf(line, sizeof(line), "%.7s: %.5s", ui_finder_slot ? "SPOT" : "PARKING",
             other->valid ? "SAVED" : "--");
    fb_text_row(37, line, COL_GREY, COL_BLACK);
    snprintf(line, sizeof(line), "GPS %s %d SATS", g.fix ? "FIX" : "NO FIX", g.sats);
    fb_text_row(38, line, COL_WHITE, COL_BLACK);
    if (msg_active) fb_text_row(39, ui_msg, COL_YELLOW, COL_BLACK);
    else fb_text_row(39, "UP/DN SLOT CTR SAVE", COL_GREY, COL_BLACK);
    draw_version_tag();
}

/* LIST: BLE + WiFi merged, strongest first, 13 rows.
 * v2.1: labels shortened to 14 chars so a row ("B <name> -99")
 * is at most 21 chars and never clips on the right. */
#define LIST_ROWS 30 /* V7: 40 text rows, so 30 list rows fit */
static void render_list(void)
{
    /* v2.2: the title is one line only — "BLE&WIFI RADAR".
     * v1.6: title at y=8, clear of the border; header rows fixed. */
    fb_text_centered_at(8, "BLE&WIFI RADAR", COL_GREEN, COL_BLACK);
    char status[40];
    char batstr[10];
    bat_status_str(batstr, sizeof(batstr));
    if (op_mode != 0) snprintf(status, sizeof(status), "SD %s %s %s %s", sd_ready ? "OK" : (sd_checked ? sd_fail_tag : "?"),
             ui_frozen ? "FROZEN" : "LIVE", batstr, mode_short_str(op_mode));
    else snprintf(status, sizeof(status), "SD %s %s %s", sd_ready ? "OK" : (sd_checked ? sd_fail_tag : "?"),
             ui_frozen ? "FROZEN" : "LIVE", batstr);
    if (op_mode != 0) {
        fb_text_row(3, "SPAM MODE - SCAN OFF", COL_YELLOW, COL_BLACK);
        char cline[34];
        snprintf(cline, sizeof(cline), "BLE SPAM SENT %d", (int)spam_ble_sent);
        fb_text_row(4, cline, COL_BLUE, COL_BLACK);
        snprintf(cline, sizeof(cline), "WIFI SPAM SENT %d", (int)spam_wifi_sent);
        fb_text_row(5, cline, COL_YELLOW, COL_BLACK);
        fb_text_row(6, spam_funny ? "FUNNY PAYLOADS ON" : "NORMAL PAYLOADS", COL_GREY, COL_BLACK);
        fb_text_row(7, "MODE VIEW TO STOP", COL_GREY, COL_BLACK);
        draw_version_tag();
        return;
    }
    fb_text_row_at(16, status, COL_YELLOW, COL_BLACK);
    typedef struct { int is_wifi; char label[27]; int rssi; } row_t;
    static row_t rows[MAX_BLE + MAX_WIFI];
    int n = 0;
    if (xSemaphoreTake(dev_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < ble_count && n < MAX_BLE + MAX_WIFI; i++) {
            rows[n].is_wifi = 0;
            if (ble_table[i].name[0] != '\0')
                snprintf(rows[n].label, sizeof(rows[n].label), "%.24s", ble_table[i].name);
            else mac_short(ble_table[i].bda, rows[n].label, sizeof(rows[n].label));
            rows[n].rssi = ble_table[i].rssi;
            n++;
        }
        for (int i = 0; i < wifi_count && n < MAX_BLE + MAX_WIFI; i++) {
            rows[n].is_wifi = 1;
            snprintf(rows[n].label, sizeof(rows[n].label), "%.24s", wifi_table[i].ssid);
            rows[n].rssi = wifi_table[i].rssi;
            n++;
        }
        xSemaphoreGive(dev_lock);
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (rows[j].rssi > rows[i].rssi) { row_t t = rows[i]; rows[i] = rows[j]; rows[j] = t; }
    if (ui_sel >= n && n > 0) ui_sel = n - 1;
    if (ui_sel < 0) ui_sel = 0;
    int start = ui_sel - (ui_sel % LIST_ROWS);
    if (n == 0) {
        fb_text_row(3, "NOTHING YET", COL_WHITE, COL_BLACK);
        for (int r = 4; r < 33; r++) fb_text_row(r, "", COL_WHITE, COL_BLACK);
        draw_version_tag();
        return;
    }
    for (int i = 0; i < LIST_ROWS; i++) {
        int idx = start + i;
        if (idx >= n) { fb_text_row(3 + i, "", COL_WHITE, COL_BLACK); continue; }
        char line[34];
        int rssi = rows[idx].rssi;
        if (rssi < -99) rssi = -99;
        if (rssi > 0) rssi = 0;
        snprintf(line, sizeof(line), "%.1s %.24s %d", rows[idx].is_wifi ? "W" : "B",
                 rows[idx].label, rssi);
        int selected = (idx == ui_sel);
        fb_text_row(3 + i, line,
                    selected ? COL_BLACK : (rows[idx].is_wifi ? COL_YELLOW : COL_BLUE),
                    selected ? COL_WHITE : COL_BLACK);
    }
    draw_version_tag();
}

/* ---------------- CENTER view (v2.2, border inset v2.4) ----------------
 * David centres the view himself. The green border is inset 2 px
 * from the framebuffer edge (x=2..125, y=2..157) and the green
 * cross is the panel centre — both move WITH the offset, together
 * with the words, because they are all drawn through fb_pixel /
 * fb_text_centered_at. The inset is deliberate (v2.4): the bright
 * static line on the right edge of the glass does NOT move with
 * the nudge, so it is the panel's unwritten edge, not our border.
 * Our inset border sits 2 px inside it and DOES move, so David
 * can tell them apart by watching it move.
 * What David looks at is the glass itself:
 *   - inset border clipped on the right  -> nudge LEFT (view moves left)
 *   - empty gap on the left       -> nudge RIGHT
 *   - border clipped at the top   -> nudge UP
 *   - empty gap at the top        -> nudge DOWN
 *   - border fully visible, just inside the glass on all four
 *     sides, words sitting centred on the cross -> it is centred.
 * The words move as he nudges, so he can see the shift happen.
 * Redrawn in place every tick (no fill) like FINDER/LIST. */
static void render_center(void)
{
    /* v1.6: title at y=8, clear of the border; every centred line
     * below is measured the same way (ink width, x=(240-ink)/2)
     * and fixed — the border and cross still move with the nudge,
     * the words no longer drift off-centre with it. */
    fb_text_centered_at(8, "BLE&WIFI RADAR", COL_GREEN, COL_BLACK);
    char off[22];
    snprintf(off, sizeof(off), "X%+d Y%+d", view_off_x, view_off_y);
    fb_text_centered_at(24, off, COL_WHITE, COL_BLACK);
    fb_text_centered_at(40, "UP/DN NUDGE Y", COL_GREY, COL_BLACK);
    fb_text_centered_at(48, "L/R NUDGE X", COL_GREY, COL_BLACK);
    fb_text_centered_at(56, "CTR SAVES", COL_YELLOW, COL_BLACK);
    char modeline[24];
    snprintf(modeline, sizeof(modeline), "MODE: %s", mode_short_str(op_mode));
    fb_text_centered_at(64, modeline, COL_YELLOW, COL_BLACK);
    fb_text_centered_at(296, "BORDER = FRAME", COL_GREY, COL_BLACK);
    fb_text_centered_at(304, "ALL SIDES SHOW", COL_GREY, COL_BLACK);
    /* Cross at the panel centre (120, 160), arms 20 px. */
    fb_cross(RADAR_CX, RADAR_CY, 20, COL_GREEN);
    fb_blip(RADAR_CX, RADAR_CY, 3, COL_WHITE);
    /* v2.4: inset border, NOT the edge border — see fb_border_inset. */
    fb_border_inset(COL_GREEN);
    draw_version_tag();
}

/* ---------------- MODE view (v1.7) ----------------
 * 5th view, reached by LEFT/RIGHT like the others. UP/DOWN move
 * the cursor (0..3 = a mode, 4 = FUNNY toggle), CENTER applies:
 * a mode applies and returns to RADAR; FUNNY toggles in place
 * and stays here. The active mode is marked *ACTIVE. */
static void render_mode(void)
{
    fb_text_centered_at(8, "BLE&WIFI RADAR", COL_GREEN, COL_BLACK);
    char status[40];
    char batstr[10];
    bat_status_str(batstr, sizeof(batstr));
    snprintf(status, sizeof(status), "MODE %s SD %s %s", mode_short_str(op_mode),
             sd_ready ? "OK" : (sd_checked ? sd_fail_tag : "?"), batstr);
    fb_text_row_at(16, status, COL_YELLOW, COL_BLACK);
    static const char *const mode_labels[4] = {
        "RADAR - SCAN ONLY", "SPAM BLE", "SPAM WIFI", "SPAM BOTH",
    };
    for (int i = 0; i < 4; i++) {
        char line[34];
        snprintf(line, sizeof(line), "%s %s%s", (mode_sel == i) ? ">" : " ",
                 mode_labels[i], (op_mode == i) ? " *ACTIVE" : "");
        fb_text_row(3 + i, line, (mode_sel == i) ? COL_BLACK : COL_WHITE,
                    (mode_sel == i) ? COL_WHITE : COL_BLACK);
    }
    char fline[34];
    snprintf(fline, sizeof(fline), "%s FUNNY: %s", (mode_sel == 4) ? ">" : " ",
             spam_funny ? "ON" : "OFF");
    fb_text_row(7, fline, (mode_sel == 4) ? COL_BLACK : COL_YELLOW,
                (mode_sel == 4) ? COL_WHITE : COL_BLACK);
    char cline[34];
    snprintf(cline, sizeof(cline), "BLE SPAM SENT %d", (int)spam_ble_sent);
    fb_text_row(9, cline, COL_BLUE, COL_BLACK);
    snprintf(cline, sizeof(cline), "WIFI SPAM SENT %d", (int)spam_wifi_sent);
    fb_text_row(10, cline, COL_YELLOW, COL_BLACK);
    fb_text_row(11, "SPAM ONLY IN SPAM MODE", COL_GREY, COL_BLACK);
    fb_text_row(12, "NO DEAUTH - SPAM ONLY", COL_GREY, COL_BLACK);
    fb_text_row(39, "UP/DN PICK CTR APPLY", COL_GREY, COL_BLACK);
    fb_border(COL_GREEN);
    draw_version_tag();
}

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        int view = ui_view, frozen = ui_frozen;
        if (xSemaphoreTake(ui_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            view = ui_view; frozen = ui_frozen;
            xSemaphoreGive(ui_lock);
        }
        /* v1.3: every frame is a fresh full render, band by band.
         * No persistent framebuffer, no incremental erase, no
         * fill-on-view-change — each band starts black and the
         * view is drawn into it. The sweep advances 10 degrees
         * per tick (a full sweep takes 9 seconds), independent
         * of the ~5 s BLE scan round. */
        if (view == VIEW_RADAR && !frozen) {
            sweep_deg = (sweep_deg + 10) % 360;
        }
        esp_err_t err = ESP_OK;
        if (view == VIEW_RADAR) err = push_frame(render_frame_radar);
        else if (view == VIEW_FINDER) err = push_frame(render_finder);
        else if (view == VIEW_CENTER) err = push_frame(render_center);
        else if (view == VIEW_MODE) err = push_frame(render_mode);
        else err = push_frame(render_list);
        if (err != ESP_OK) ESP_LOGW(TAG, "ui frame push failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* ---------------- 5-way buttons: profiler v4 direct map ---------------- */

static const char *dir_names[DIR_COUNT] = {"CENTER", "UP", "DOWN", "LEFT", "RIGHT"};
#define BTN_ADC_THRESH_MV 500
#define BTN_ADC_RAW_FALLBACK 1200

static adc_oneshot_unit_handle_t btn_adc = NULL;
static adc_cali_handle_t btn_cali = NULL;
static int btn_adc_ok = 0;

static int btn_adc_pressed(adc_channel_t ch)
{
    int raw = 0;
    if (adc_oneshot_read(btn_adc, ch, &raw) != ESP_OK) return 0;
    if (btn_cali != NULL) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(btn_cali, raw, &mv) == ESP_OK)
            return mv < BTN_ADC_THRESH_MV;
    }
    return raw < BTN_ADC_RAW_FALLBACK;
}

static int btn_dir_active(int d)
{
    switch (d) {
    case DIR_UP:     return btn_adc_pressed(ADC_CHANNEL_0); /* GPIO36 */
    case DIR_DOWN:   return btn_adc_pressed(ADC_CHANNEL_7); /* GPIO35 */
    case DIR_CENTER: return btn_adc_pressed(ADC_CHANNEL_6); /* GPIO34 */
    case DIR_RIGHT:  return btn_adc_pressed(ADC_CHANNEL_3); /* GPIO39 */
    case DIR_LEFT:   return (gps_rx_pin == 13) ? 0 : (gpio_get_level(13) == 0);
    default:         return 0;
    }
}

static int btn_input_init(void)
{
    /* If GPS was found on GPIO13, leave it as the GPS UART RX and
     * give up LEFT (view switching still works with RIGHT). */
    if (gps_rx_pin != 13) {
        gpio_config_t left_cfg = {
            .pin_bit_mask = (1ULL << 13),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&left_cfg);
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &btn_adc) != ESP_OK) {
        ESP_LOGW(TAG, "ADC unit init failed; buttons unavailable");
        return -1;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    const adc_channel_t chans[4] = {ADC_CHANNEL_0, ADC_CHANNEL_3, ADC_CHANNEL_6, ADC_CHANNEL_7};
    for (int i = 0; i < 4; i++) {
        if (adc_oneshot_config_channel(btn_adc, chans[i], &chan_cfg) != ESP_OK) return -1;
    }
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &btn_cali) != ESP_OK) {
        btn_cali = NULL;
        ESP_LOGW(TAG, "ADC cali unavailable; raw threshold fallback");
    }
    btn_adc_ok = 1;
    ESP_LOGI(TAG, "buttons: UP=36 DOWN=35 CENTER=34 RIGHT=39 LEFT=13 (GPIO21/33 ignored)");
    return 0;
}

static void set_msg(const char *msg)
{
    snprintf(ui_msg, sizeof(ui_msg), "%s", msg);
    ui_msg_until = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
}

/* Forward declaration: saving a pin also logs it to SD. */
static void sd_log_pin(const char *slot_name, double lat, double lon);

static void input_task(void *arg)
{
    (void)arg;
    int active_count[DIR_COUNT] = {0};
    int armed[DIR_COUNT] = {1, 1, 1, 1, 1};
    (void)btn_adc_ok;
    for (;;) {
        for (int d = 0; d < DIR_COUNT; d++) {
            int active = btn_dir_active(d);
            int need = (d == DIR_LEFT) ? 5 : 3;
            if (active) active_count[d]++;
            else { active_count[d] = 0; armed[d] = 1; }
            if (active_count[d] >= need && armed[d]) {
                armed[d] = 0;
                active_count[d] = 0;
                ESP_LOGI(TAG, "button %s pressed", dir_names[d]);
                if (xSemaphoreTake(ui_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (d == DIR_CENTER) {
                        if (ui_view == VIEW_MODE) {
                            /* MODE view: CENTER applies. A mode
                             * applies and returns to RADAR (spam
                             * starts there, or scans resume);
                             * FUNNY (cursor 4) toggles in place. */
                            if (mode_sel <= 3) {
                                set_op_mode(mode_sel);
                                set_msg(mode_name_str(op_mode));
                                ui_view = VIEW_RADAR;
                            } else {
                                spam_funny = !spam_funny;
                                mode_save();
                                ESP_LOGI(TAG, "SPAM funny %s", spam_funny ? "ON" : "OFF");
                                set_msg(spam_funny ? "FUNNY ON" : "FUNNY OFF");
                            }
                        } else if (ui_view == VIEW_CENTER) {
                            /* CENTER view only: save the centre
                             * offset to NVS and go back to RADAR.
                             * This is the ONLY way out of CENTER
                             * view — LEFT/RIGHT nudge here, they
                             * do not change view. */
                            offset_save();
                            set_msg("OFFSET SAVED");
                            ui_view = VIEW_RADAR;
                        } else if (ui_view == VIEW_FINDER) {
                            /* Save current GPS into the selected slot. */
                            gps_state_t g;
                            int have = 0;
                            if (xSemaphoreTake(gps_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                                g = gps_state; have = 1;
                                xSemaphoreGive(gps_lock);
                            }
                            if (!gps_found) set_msg("GPS NOT FOUND");
                            else if (!have || !g.fix) set_msg("NO GPS FIX");
                            else {
                                pin_save(ui_finder_slot, g.lat, g.lon);
                                sd_log_pin(ui_finder_slot ? "PARKING" : "SPOT", g.lat, g.lon);
                                set_msg(ui_finder_slot ? "PARKING SAVED" : "SPOT SAVED");
                            }
                        } else {
                            ui_frozen = !ui_frozen;
                        }
                    } else if (d == DIR_UP) {
                        if (ui_view == VIEW_MODE) {
                            mode_sel = (mode_sel + 4) % 5;
                        } else if (ui_view == VIEW_CENTER) {
                            /* Nudge the whole view UP 2 px. */
                            view_off_y = clamp_off(view_off_y - VIEW_OFF_STEP);
                        } else if (ui_view == VIEW_FINDER) ui_finder_slot = 0;
                        else if (ui_view == VIEW_LIST && ui_sel > 0) ui_sel--;
                    } else if (d == DIR_DOWN) {
                        if (ui_view == VIEW_MODE) {
                            mode_sel = (mode_sel + 1) % 5;
                        } else if (ui_view == VIEW_CENTER) {
                            /* Nudge the whole view DOWN 2 px. */
                            view_off_y = clamp_off(view_off_y + VIEW_OFF_STEP);
                        } else if (ui_view == VIEW_FINDER) ui_finder_slot = 1;
                        else if (ui_view == VIEW_LIST) ui_sel++;
                    } else if (d == DIR_LEFT) {
                        if (ui_view == VIEW_CENTER) {
                            /* Nudge the whole view LEFT 2 px.
                             * (If GPS was found on GPIO13, LEFT is
                             * given up — same as before — so this
                             * nudge is unavailable then.) */
                            view_off_x = clamp_off(view_off_x - VIEW_OFF_STEP);
                        } else {
                            ui_view = (ui_view + VIEW_COUNT - 1) % VIEW_COUNT;
                        }
                    } else if (d == DIR_RIGHT) {
                        if (ui_view == VIEW_CENTER) {
                            /* Nudge the whole view RIGHT 2 px. */
                            view_off_x = clamp_off(view_off_x + VIEW_OFF_STEP);
                        } else {
                            ui_view = (ui_view + 1) % VIEW_COUNT;
                        }
                    }
                    xSemaphoreGive(ui_lock);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ---------------- SD logging ---------------- */

static FILE *sd_append_open(void)
{
    if (!sd_ready) return NULL;
    FILE *probe = fopen("/sdcard/marauder_v7_radar.csv", "r");
    int need_header = 1;
    if (probe) {
        fseek(probe, 0, SEEK_END);
        if (ftell(probe) > 0) need_header = 0;
        fclose(probe);
    }
    FILE *f = fopen("/sdcard/marauder_v7_radar.csv", "a");
    if (!f) return NULL;
    if (need_header)
        fprintf(f, "ms,type,name_or_ssid,mac_or_bssid,rssi,lat,lon,fix,sats\n");
    return f;
}

static void sd_log_round(long long ms)
{
    /* v1.5: boundary log line, so the next serial log shows exactly
     * where the SD write starts (v1.4 panicked inside this call). */
    ESP_LOGI(TAG, "SD log start");
    /* v1.5: hold spi3_lock for the whole fopen/fprintf/fclose.
     * SD writes are polling transfers on SPI3 and must not start
     * while display DMA is in flight (v1.4 panic). Taken FIRST,
     * before gps_lock / dev_lock, to match push_frame's order. If
     * the lock cannot be taken in 2 s, skip this round's log
     * rather than block the scanner. */
    if (!spi3_lock || xSemaphoreTake(spi3_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW(TAG, "SD log round: spi3_lock take timeout — round not logged");
        return;
    }
    FILE *f = sd_append_open();
    if (!f) { xSemaphoreGive(spi3_lock); return; }
    gps_state_t g;
    if (xSemaphoreTake(gps_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        g = gps_state;
        xSemaphoreGive(gps_lock);
    } else {
        g.fix = 0; g.sats = 0; g.lat = 0; g.lon = 0;
    }
    if (xSemaphoreTake(dev_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < ble_count; i++) {
            char name[32], mac[18];
            csv_clean(name, ble_table[i].name[0] ? ble_table[i].name : "", sizeof(name));
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                     ble_table[i].bda[0], ble_table[i].bda[1], ble_table[i].bda[2],
                     ble_table[i].bda[3], ble_table[i].bda[4], ble_table[i].bda[5]);
            if (g.fix)
                fprintf(f, "%lld,BLE,%s,%s,%d,%.6f,%.6f,%d,%d\n",
                        ms, name, mac, ble_table[i].rssi, g.lat, g.lon, g.fix, g.sats);
            else
                fprintf(f, "%lld,BLE,%s,%s,%d,,,0,%d\n",
                        ms, name, mac, ble_table[i].rssi, g.sats);
        }
        for (int i = 0; i < wifi_count; i++) {
            char ssid[33], mac[18];
            csv_clean(ssid, wifi_table[i].ssid, sizeof(ssid));
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                     wifi_table[i].bssid[0], wifi_table[i].bssid[1], wifi_table[i].bssid[2],
                     wifi_table[i].bssid[3], wifi_table[i].bssid[4], wifi_table[i].bssid[5]);
            if (g.fix)
                fprintf(f, "%lld,WIFI,%s,%s,%d,%.6f,%.6f,%d,%d\n",
                        ms, ssid, mac, wifi_table[i].rssi, g.lat, g.lon, g.fix, g.sats);
            else
                fprintf(f, "%lld,WIFI,%s,%s,%d,,,0,%d\n",
                        ms, ssid, mac, wifi_table[i].rssi, g.sats);
        }
        xSemaphoreGive(dev_lock);
    }
    fclose(f); /* closed after each round so power loss keeps data */
    xSemaphoreGive(spi3_lock);
}

static void sd_log_pin(const char *slot_name, double lat, double lon)
{
    /* v1.5: same spi3_lock rule as sd_log_round — a pin save is an
     * SD polling transfer on SPI3 and must not start while display
     * DMA is in flight. Taken first, before gps_lock / dev_lock. */
    if (!spi3_lock || xSemaphoreTake(spi3_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW(TAG, "SD log pin: spi3_lock take timeout — pin not logged");
        return;
    }
    FILE *f = sd_append_open();
    if (!f) { xSemaphoreGive(spi3_lock); return; }
    long long ms = esp_timer_get_time() / 1000;
    int sats = 0;
    if (xSemaphoreTake(gps_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        sats = gps_state.sats;
        xSemaphoreGive(gps_lock);
    }
    fprintf(f, "%lld,PIN,%s,,0,%.6f,%.6f,1,%d\n", ms, slot_name, lat, lon, sats);
    fclose(f);
    xSemaphoreGive(spi3_lock);
}

/* ---------------- v1.7 SPAM: BLE advertising + WiFi beacons ----------------
 * Transmits ONLY while op_mode is SPAM (1=BLE, 2=WIFI, 3=BOTH).
 * NO deauth, NO targeted frames: BLE spam is a rotating device
 * name in a non-connectable advertisement (ADV_TYPE_NONCONN_IND),
 * WiFi spam is a broadcast beacon for a rotating fake SSID
 * (DA ff:ff:ff:ff:ff:ff, locally-administered SA/BSSID). Both
 * are the same kind of broadcast stock Marauder's spam uses.
 * Funny payloads are the default; Normal is the plain list.
 * spam_ble_once() is self-contained (start, hold ~800 ms, stop)
 * so no advert is left running when the mode changes. WiFi is
 * one beacon per call, STA mode, WIFI_IF_STA, en_sys_seq=true
 * (per the IDF doc, STA is a valid ifx for esp_wifi_80211_tx).
 * spam_task waits for scan_active==0 before any send, and
 * checks op_mode before every send, so spam never runs at the
 * same time as a scan. All failures are logged, never abort. */
static void spam_ble_once(void)
{
    static int ble_idx = 0;
    const char *name = spam_ble_name(ble_idx++);
    if (sem_adv_data) xSemaphoreTake(sem_adv_data, 0);
    if (sem_adv_start) xSemaphoreTake(sem_adv_start, 0);
    if (sem_adv_stop) xSemaphoreTake(sem_adv_stop, 0);
    esp_err_t err = esp_ble_gap_set_device_name(name);
    if (err != ESP_OK) { ESP_LOGW(TAG, "SPAM BLE set name failed: %s", esp_err_to_name(err)); return; }
    esp_ble_adv_data_t adv = {0};
    adv.set_scan_rsp = false;
    adv.include_name = true;
    adv.include_txpower = false;
    adv.flag = 0x06;
    err = esp_ble_gap_config_adv_data(&adv);
    if (err != ESP_OK) { ESP_LOGW(TAG, "SPAM BLE config adv failed: %s", esp_err_to_name(err)); return; }
    if (!sem_adv_data || xSemaphoreTake(sem_adv_data, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "SPAM BLE adv data ack timeout for '%s'", name);
        return;
    }
    esp_ble_adv_params_t params = {0};
    params.adv_int_min = 0x20;
    params.adv_int_max = 0x20;
    params.adv_type = ADV_TYPE_NONCONN_IND;
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.channel_map = ADV_CHNL_ALL;
    params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    err = esp_ble_gap_start_advertising(&params);
    if (err != ESP_OK) { ESP_LOGW(TAG, "SPAM BLE start adv failed: %s", esp_err_to_name(err)); return; }
    if (!sem_adv_start || xSemaphoreTake(sem_adv_start, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "SPAM BLE start adv ack timeout for '%s'", name);
        (void)esp_ble_gap_stop_advertising();
        return;
    }
    spam_ble_on = 1;
    spam_ble_sent++;
    ESP_LOGI(TAG, "SPAM BLE #%d: '%s' (%s)", (int)spam_ble_sent, name, spam_funny ? "funny" : "normal");
    vTaskDelay(pdMS_TO_TICKS(800));
    spam_ble_on = 0;
    err = esp_ble_gap_stop_advertising();
    if (err != ESP_OK) ESP_LOGW(TAG, "SPAM BLE stop adv failed: %s", esp_err_to_name(err));
    if (sem_adv_stop) xSemaphoreTake(sem_adv_stop, pdMS_TO_TICKS(500));
}

static void spam_wifi_once(void)
{
    static int wifi_idx = 0;
    static const uint8_t chans[3] = {1, 6, 11};
    const char *ssid = spam_wifi_ssid(wifi_idx);
    int ch = chans[wifi_idx % 3];
    wifi_idx++;
    size_t slen = strlen(ssid);
    if (slen > 32) slen = 32;
    uint8_t frame[128];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x80; frame[1] = 0x00; /* beacon, mgmt */
    /* duration 0; DA broadcast */
    memset(&frame[4], 0xFF, 6);
    /* SA / BSSID: locally-administered, unicast, random low bytes */
    uint32_t rnd = esp_random();
    frame[10] = 0x02; frame[11] = 0x11; frame[12] = 0x22;
    frame[13] = (rnd >> 16) & 0xFF; frame[14] = (rnd >> 8) & 0xFF; frame[15] = rnd & 0xFF;
    memcpy(&frame[16], &frame[10], 6);
    /* timestamp 0 (24..31), beacon interval 100 TU (32..33) */
    frame[32] = 0x64; frame[33] = 0x00;
    frame[34] = 0x01; frame[35] = 0x00; /* capability: ESS */
    int pos = 36;
    frame[pos++] = 0x00; frame[pos++] = (uint8_t)slen; /* SSID tag */
    memcpy(&frame[pos], ssid, slen); pos += (int)slen;
    frame[pos++] = 0x01; frame[pos++] = 8; /* supported rates */
    const uint8_t rates[8] = {0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24};
    memcpy(&frame[pos], rates, 8); pos += 8;
    frame[pos++] = 0x03; frame[pos++] = 1; frame[pos++] = (uint8_t)ch; /* DS channel */
    (void)esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, frame, pos, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPAM WIFI tx failed for '%s' ch %d: %s", ssid, ch, esp_err_to_name(err));
        return;
    }
    spam_wifi_sent++;
    ESP_LOGI(TAG, "SPAM WIFI #%d: '%s' ch %d (%s)", (int)spam_wifi_sent, ssid, ch, spam_funny ? "funny" : "normal");
}

static void spam_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (op_mode == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (scan_active) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        if (op_mode == 1 || op_mode == 3) spam_ble_once();
        if (op_mode == 0 || scan_active) continue;
        if (op_mode == 2 || op_mode == 3) spam_wifi_once();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ---------------- scanner task: BLE scan, then WiFi scan, repeat ---------------- */

static void scanner_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* v1.7: in SPAM mode the scanner idles — scans never run
         * at the same time as spam. spam_task waits for
         * scan_active to clear before it sends anything. */
        if (op_mode != 0) {
            scan_active = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        scan_active = 1;
        sd_poll_result();
        int frozen = 0;
        if (xSemaphoreTake(ui_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            frozen = ui_frozen;
            xSemaphoreGive(ui_lock);
        }
        if (frozen) {
            scan_active = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        /* ---- BLE scan round (scan-only, no GATT connect) ---- */
        if (xSemaphoreTake(dev_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            ble_count = 0;
            xSemaphoreGive(dev_lock);
        }
        esp_ble_scan_params_t scan_params = {
            .scan_type = BLE_SCAN_TYPE_ACTIVE,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
            .scan_interval = 0x50,
            .scan_window = 0x50,
            .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
        };
        xSemaphoreTake(sem_scan_done, 0); /* drop any stale done event */
        esp_ble_gap_set_scan_params(&scan_params);
        if (xSemaphoreTake(sem_scan_param, pdMS_TO_TICKS(2000)) != pdTRUE)
            ESP_LOGW(TAG, "BLE scan params ack timeout, continuing");
        if (esp_ble_gap_start_scanning(BLE_SCAN_SECONDS) != ESP_OK)
            ESP_LOGW(TAG, "BLE start scanning failed");
        if (xSemaphoreTake(sem_scan_done, pdMS_TO_TICKS((BLE_SCAN_SECONDS + 3) * 1000)) != pdTRUE) {
            ESP_LOGW(TAG, "BLE scan done timeout, stopping scan");
            esp_ble_gap_stop_scanning();
            xSemaphoreTake(sem_scan_done, pdMS_TO_TICKS(2000));
        }
        ESP_LOGI(TAG, "BLE scan done: %d devices", ble_count);

        /* ---- WiFi scan (passive, 2.4 GHz only on this chip) ---- */
        wifi_scan_config_t scan_cfg = {0};
        esp_wifi_scan_start(&scan_cfg, true);
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        wifi_ap_record_t aps[MAX_WIFI];
        uint16_t got = (n > MAX_WIFI) ? MAX_WIFI : n;
        if (got > 0) esp_wifi_scan_get_ap_records(&got, aps);
        if (xSemaphoreTake(dev_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            wifi_count = got;
            for (int i = 0; i < got; i++) {
                memcpy(wifi_table[i].bssid, aps[i].bssid, 6);
                snprintf(wifi_table[i].ssid, sizeof(wifi_table[i].ssid), "%s",
                         (const char *)aps[i].ssid);
                wifi_table[i].rssi = aps[i].rssi;
            }
            xSemaphoreGive(dev_lock);
        }
        ESP_LOGI(TAG, "WiFi scan done: %d nets", got);

        long long ms = esp_timer_get_time() / 1000;
        sd_log_round(ms);
        /* A full scan round finished on a boot whose hunt is done: the
         * board is alive, so this was not a crash loop — clear the
         * boot counter so the next boot hunts for GPS normally. */
        static int boot_cleared = 0;
        if (!boot_cleared && hunt_done) {
            boot_counter_clear();
            boot_cleared = 1;
        }
        /* v2.1: sweep is advanced 10 deg/tick by ui_task, not here. */
        scan_active = 0;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---------------- app_main ---------------- */

/* Boot screen: one title line + one status line, green border,
 * version tag "1.7" bottom-right. Rendered band by band like
 * everything else in v1.3. */
static const char *boot_status_line = "";

static void render_frame_boot(void)
{
    fb_text_centered_at(152, "BLE&WIFI RADAR", COL_GREEN, COL_BLACK);
    fb_text_centered_at(168, boot_status_line, COL_WHITE, COL_BLACK);
    fb_border(COL_GREEN);
    fb_text_at_raw(222, 312, "1.7", COL_GREY);
}

static void show_boot(const char *status)
{
    boot_status_line = (status && status[0]) ? status : "";
    esp_err_t err = push_frame(render_frame_boot);
    ESP_LOGI(TAG, "show_boot -> %s", esp_err_to_name(err));
}

void app_main(void)
{
    ESP_LOGI(TAG, "RADAR2 1.7 starting — BLE+WiFi radar, band-buffered, double-buffered, battery gauge + SD fix + SPI3 mutex + Radar/Spam switch + Funny spam");
    ESP_LOGI(TAG, "largest free block: %u bytes, total free: %u bytes",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    spi3_lock = xSemaphoreCreateMutex(); /* v1.5: display + SD on SPI3 */
    dev_lock = xSemaphoreCreateMutex();
    ui_lock = xSemaphoreCreateMutex();
    gps_lock = xSemaphoreCreateMutex();
    sem_scan_param = xSemaphoreCreateBinary();
    sem_scan_done = xSemaphoreCreateBinary();
    sem_adv_data = xSemaphoreCreateBinary();
    sem_adv_start = xSemaphoreCreateBinary();
    sem_adv_stop = xSemaphoreCreateBinary();
    sem_sd_mount = xSemaphoreCreateBinary();
    band_done_sem[0] = xSemaphoreCreateBinary();
    band_done_sem[1] = xSemaphoreCreateBinary();
    if (band_done_sem[0]) xSemaphoreGive(band_done_sem[0]);
    if (band_done_sem[1]) xSemaphoreGive(band_done_sem[1]);

    /* v1.3: NO full framebuffer. v1.2's heap_caps_malloc(153,600)
     * failed — largest free block is 113,840 bytes. Two 3,840-byte
     * band buffers instead (7,680 bytes total). If even these
     * fail, log it and park — NO abort. */
    for (int i = 0; i < 2; i++) {
        band_buf[i] = (uint16_t *)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!band_buf[i]) {
            ESP_LOGE(TAG, "band buffer %d malloc failed (%d bytes) — parking, NOT aborting", i, BAND_BYTES);
            park_forever("band buffer malloc failed");
        }
    }
    ESP_LOGI(TAG, "band buffers ok (2 x %d bytes), largest free block now: %u bytes",
             BAND_BYTES, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    /* SD_CS preset HIGH before display init, as Marauder does. */
    gpio_config_t sd_cs_cfg = {
        .pin_bit_mask = (1ULL << PIN_SD_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sd_cs_cfg);
    gpio_set_level(PIN_SD_CS, 1);
    ESP_LOGI(TAG, "SD_CS GPIO4 preset HIGH before display init");

    /* Display SPI: VSPI (SPI3_HOST) 27 MHz mode 0, same as v1.2. */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    ESP_LOGI(TAG, "spi_bus_initialize SPI3_HOST -> %s", esp_err_to_name(err));
    if (err != ESP_OK) park_forever("spi_bus_initialize failed");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_TFT_CS,
        .dc_gpio_num = PIN_TFT_DC,
        .spi_mode = 0,
        .pclk_hz = 27 * 1000 * 1000, /* v1.2: VSPI 27 MHz, as Marauder/TFT_eSPI */
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_cfg, &lcd_io);
    ESP_LOGI(TAG, "esp_lcd_new_panel_io_spi (CS 17, DC 26, 27 MHz) -> %s", esp_err_to_name(err));
    if (err != ESP_OK) park_forever("esp_lcd_new_panel_io_spi failed");

    /* Colour-done callback: signals when a band buffer's DMA is
     * finished, so push_frame() never draws into a buffer DMA is
     * still reading. If registration fails, double buffering
     * alone (plus tx_param's wait for in-flight transfers) still
     * guards the buffers. */
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = color_done_cb,
    };
    err = esp_lcd_panel_io_register_event_callbacks(lcd_io, &cbs, NULL);
    ESP_LOGI(TAG, "register on_color_trans_done -> %s", esp_err_to_name(err));
    if (err != ESP_OK)
        ESP_LOGW(TAG, "colour-done callback not registered — double buffering alone still guards the buffers");

    err = lcd_init_sequence();
    ESP_LOGI(TAG, "lcd_init_sequence -> %s", esp_err_to_name(err));
    if (err != ESP_OK) park_forever("lcd_init_sequence failed");

    /* Backlight AFTER display init: Marauder-style LEDC PWM,
     * 5 kHz, 8-bit, duty 255 = full on, GPIO32. Uses
     * ledc_set_duty() + ledc_update_duty() — NOT
     * ledc_set_duty_and_update(), which needs the LEDC fade
     * service and ESP_FAILs (proven by V7TEST 1.0). Continue
     * to drawing even if they fail: ledc_channel_config()
     * above already set duty 255, so the backlight is on
     * either way. */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&ledc_timer);
    ESP_LOGI(TAG, "ledc_timer_config 5 kHz 8-bit -> %s", esp_err_to_name(err));
    if (err != ESP_OK) park_forever("ledc_timer_config failed");

    ledc_channel_config_t ledc_channel = {
        .gpio_num = PIN_TFT_BL,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 255,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ledc_channel);
    ESP_LOGI(TAG, "ledc_channel_config GPIO32 duty 255 -> %s", esp_err_to_name(err));
    if (err != ESP_OK) park_forever("ledc_channel_config failed");

    err = ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 255);
    ESP_LOGI(TAG, "ledc_set_duty 255 -> %s", esp_err_to_name(err));
    err = ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "ledc_update_duty -> %s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_set_duty/update failed (%s) — continuing, channel_config already set duty 255", esp_err_to_name(err));
    }

    /* NVS first, before the first boot screen: pins_load() also
     * loads the saved centre offset, so the boot title is drawn
     * with David's offset from the very first screen. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    pins_load();
    ESP_LOGI(TAG, "saved pins: SPOT %s PARKING %s, centre offset X=%d Y=%d",
             pin_spot.valid ? "yes" : "no", pin_parking.valid ? "yes" : "no",
             view_off_x, view_off_y);

    /* Count this boot, then show the count + reset reason as
     * the boot screen's small status line, e.g. "BOOT #3 POWERON". */
    boot_counter_bump();
    char boot_line[22];
    snprintf(boot_line, sizeof(boot_line), "BOOT #%d %s",
             boot_count, reset_reason_str(esp_reset_reason()));
    show_boot(boot_line);
    vTaskDelay(pdMS_TO_TICKS(3000)); /* display test 3 s, so David can see the border/title */

    /* SD mount in its own task — the radar never waits on it. */
    xTaskCreate(sd_mount_task, "sd_mount", 6144, NULL, 5, NULL);

    /* GPS: V7 fixed UART2 TX 21 / RX 22 — no hunt, no probing.
     * gps_fixed_init() configures the UART and starts gps_task;
     * the radar is already up by then, and a GPS fix still needs
     * open sky. */
    sd_poll_result();

    /* Buttons: profiler v4 direct map (no setup, no NVS). */
    int buttons_ok = (btn_input_init() == 0);
    if (!buttons_ok) ESP_LOGW(TAG, "button input unavailable; radar runs, views fixed");

    /* Bluetooth: BLE-only, max TX power, scan-only (no GATT).
     * Logged step by step, never ESP_ERROR_CHECK — if BT fails
     * the radar UI still runs, just without BLE dots. */
    int bt_ok = 0;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err == ESP_OK) err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err == ESP_OK) err = esp_bluedroid_init();
    if (err == ESP_OK) err = esp_bluedroid_enable();
    if (err == ESP_OK) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
        err = esp_ble_gap_register_callback(gap_cb);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BT init failed (%s) — BLE scan off, radar UI still runs", esp_err_to_name(err));
    } else {
        bt_ok = 1;
        ESP_LOGI(TAG, "BT BLE-only init ok");
    }
    (void)bt_ok;

    /* WiFi: station mode, passive scans only (never joins).
     * Logged, never ESP_ERROR_CHECK. */
    esp_netif_init();
    esp_err_t ev_err = esp_event_loop_create_default();
    if (ev_err != ESP_OK && ev_err != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "event loop create failed: %s", esp_err_to_name(ev_err));
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed (%s) — WiFi scan off, radar UI still runs", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "WiFi STA init ok");
    }

    ESP_LOGI(TAG, "largest free block after BT/WiFi start: %u bytes, total free: %u bytes",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    /* Battery gauge task: MAX17048 on I2C SDA 33 / SCL 16.
     * It inits I2C itself; if the gauge is absent it logs once
     * and deletes itself. Radar never waits on it. */
    xTaskCreate(bat_task, "bat", 4096, NULL, 5, NULL);

    xTaskCreate(ui_task, "ui", 6144, NULL, 5, NULL);
    if (buttons_ok) xTaskCreate(input_task, "input", 4096, NULL, 5, NULL);
    xTaskCreate(scanner_task, "scanner", 8192, NULL, 5, NULL);
    xTaskCreate(spam_task, "spam", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "SPAM: MODE view switches RADAR / SPAM BLE / SPAM WIFI / SPAM BOTH; boot is always RADAR; funny %s",
             spam_funny ? "ON" : "OFF");
    /* GPS fixed init — no hunt task on the V7. */
    gps_fixed_init();
}
