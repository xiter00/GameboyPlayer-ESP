// ESP32-S3 (N16R8) GB/GBC Emulator
// TFT ST7789 240x135 + I2S MAX98357A + joystick tactile
// ROM dibaca dari LittleFS -> /game.gb

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <driver/i2s.h>
#include <esp_partition.h>

// print peta partisi asli dari firmware yang lagi jalan -- biar gak
// nebak-nebak offset buat flash littlefs.bin manual
void print_partition_table() {
  Serial.println("=== PARTITION TABLE ASLI ===");
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                     ESP_PARTITION_SUBTYPE_ANY,
                                                     NULL);
  while (it != NULL) {
    const esp_partition_t *p = esp_partition_get(it);
    Serial.printf("  %-12s type=%d sub=%d offset=0x%06X size=0x%06X (%u bytes)\n",
                  p->label, p->type, p->subtype, p->address, p->size, p->size);
    it = esp_partition_next(it);
  }
  Serial.println("============================");
}

#include "peanut_gb.h" // DMG-only, gak ada mode warna CGB

// ---------- pin config ----------
// pin TFT ada di platformio.ini

#define PIN_I2S_BCLK    4
#define PIN_I2S_LRC     5
#define PIN_I2S_DOUT    6
#define PIN_I2S_SD      7

#define PIN_BTN_UP      1
#define PIN_BTN_DOWN    2
#define PIN_BTN_LEFT    3
#define PIN_BTN_RIGHT   8
#define PIN_BTN_A       17
#define PIN_BTN_B       18
#define PIN_BTN_START   21
#define PIN_BTN_SELECT  38

#define ROM_FILENAME    "/game.gb"

// ---------- display / letterbox ----------
#define GB_WIDTH        160
#define GB_HEIGHT       144
#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   135

constexpr int DIFF_X   = SCREEN_WIDTH  - GB_WIDTH;
constexpr int DIFF_Y   = SCREEN_HEIGHT - GB_HEIGHT;
constexpr int OFFSET_X = DIFF_X / 2;
constexpr int CROP_Y_TOTAL = (DIFF_Y < 0) ? -DIFF_Y : 0;
constexpr int CROP_Y_TOP   = CROP_Y_TOTAL / 2;
constexpr int CROP_Y_BOTTOM= CROP_Y_TOTAL - CROP_Y_TOP;
constexpr int OFFSET_Y = (DIFF_Y > 0) ? (DIFF_Y / 2) : 0;

TFT_eSPI tft = TFT_eSPI();

struct gb_s gb;
uint8_t *rom = nullptr;
uint8_t cart_ram[32 * 1024];

int16_t audio_buf[2][735];

// ---------- audio ----------
void audio_init() {
  pinMode(PIN_I2S_SD, OUTPUT);
  digitalWrite(PIN_I2S_SD, HIGH);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 32768,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = PIN_I2S_BCLK,
    .ws_io_num = PIN_I2S_LRC,
    .data_out_num = PIN_I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void audio_push_frame() {
  size_t bytes_written;
  i2s_write(I2S_NUM_0, audio_buf, sizeof(audio_buf), &bytes_written, portMAX_DELAY);
}

// ---------- joystick ----------
void joystick_init() {
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
}

void joystick_update(struct gb_s *gb) {
  gb->direct.joypad_bits.up     = digitalRead(PIN_BTN_UP);
  gb->direct.joypad_bits.down   = digitalRead(PIN_BTN_DOWN);
  gb->direct.joypad_bits.left   = digitalRead(PIN_BTN_LEFT);
  gb->direct.joypad_bits.right  = digitalRead(PIN_BTN_RIGHT);
  gb->direct.joypad_bits.a      = digitalRead(PIN_BTN_A);
  gb->direct.joypad_bits.b      = digitalRead(PIN_BTN_B);
  gb->direct.joypad_bits.start  = digitalRead(PIN_BTN_START);
  gb->direct.joypad_bits.select = digitalRead(PIN_BTN_SELECT);
}

// ---------- peanut-gb callbacks ----------
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr) {
  return rom[addr];
}

uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
  return cart_ram[addr];
}

void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
  cart_ram[addr] = val;
}

void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t val) {
  Serial.printf("GB ERROR %d val %d\n", gb_err, val);
}

void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[160],
                    const uint_fast8_t line) {
  if (line < CROP_Y_TOP || line >= (GB_HEIGHT - CROP_Y_BOTTOM)) {
    return;
  }

  int16_t screenY = line - CROP_Y_TOP + OFFSET_Y;

  for (uint8_t x = 0; x < GB_WIDTH; x++) {
    uint16_t color;
    uint8_t shade = pixels[x] & 0x03;
    switch (shade) {
      case 0: color = TFT_WHITE; break;
      case 1: color = TFT_LIGHTGREY; break;
      case 2: color = TFT_DARKGREY; break;
      default: color = TFT_BLACK; break;
    }

    tft.drawPixel(OFFSET_X + x, screenY, color);
  }
}

// ---------- load rom ----------
bool load_rom() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount gagal!");
    return false;
  }

  File romFile = LittleFS.open(ROM_FILENAME, "r");
  if (!romFile) {
    Serial.println("ROM file gak ketemu!");
    return false;
  }

  size_t romSize = romFile.size();
  rom = (uint8_t *)ps_malloc(romSize);
  if (!rom) {
    Serial.println("Gagal alokasi PSRAM!");
    romFile.close();
    return false;
  }

  romFile.read(rom, romSize);
  romFile.close();

  Serial.printf("ROM loaded: %d bytes\n", romSize);
  return true;
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[1] serial ok");

  print_partition_table();

  Serial.printf("[1a] chip rev: %d, cores: %d\n", ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("[1b] free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("[1c] psram size: %u bytes (0 = PSRAM gagal init)\n", ESP.getPsramSize());
  Serial.printf("[1d] free psram: %u bytes\n", ESP.getFreePsram());

  Serial.println("[1e] mulai tft.init() sekarang...");
  Serial.flush();
  delay(100);

  tft.init();
  Serial.println("[2] tft init ok");
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  Serial.println("[3] tft ready");

  audio_init();
  Serial.println("[4] audio init ok");
  joystick_init();
  Serial.println("[5] joystick init ok");

  if (!load_rom()) {
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_RED);
    tft.println("ROM load failed!");
    while (1) delay(1000);
  }

  // dump header ROM + itung ulang checksum manual (Pan Docs algorithm)
  // biar tau pasti byte mana yang gak cocok, bukan tebak-tebak
  {
    char title[17] = {0};
    memcpy(title, &rom[0x134], 16);
    uint8_t cart_type   = rom[0x147];
    uint8_t rom_size    = rom[0x148];
    uint8_t ram_size    = rom[0x149];
    uint8_t cgb_flag    = rom[0x143];
    uint8_t stored_sum  = rom[0x14D];

    uint8_t calc_sum = 0;
    for (uint16_t addr = 0x134; addr <= 0x14C; addr++) {
      calc_sum = calc_sum - rom[addr] - 1;
    }

    Serial.println("=== HEADER ROM ===");
    Serial.printf("  title      : %s\n", title);
    Serial.printf("  cgb_flag   : 0x%02X\n", cgb_flag);
    Serial.printf("  cart_type  : 0x%02X\n", cart_type);
    Serial.printf("  rom_size   : 0x%02X\n", rom_size);
    Serial.printf("  ram_size   : 0x%02X\n", ram_size);
    Serial.printf("  checksum stored di file : 0x%02X\n", stored_sum);
    Serial.printf("  checksum hasil hitung   : 0x%02X %s\n", calc_sum,
                  (calc_sum == stored_sum) ? "(COCOK)" : "(BEDA -- ROM rusak/gak lengkap)");
    Serial.println("==================");
  }

  enum gb_init_error_e ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read,
                                      &gb_cart_ram_write, &gb_error, NULL);
  if (ret != GB_INIT_NO_ERROR) {
    Serial.printf("GB init gagal, kode: %d -> ", ret);
    if (ret == GB_INIT_CARTRIDGE_UNSUPPORTED) {
      Serial.println("GB_INIT_CARTRIDGE_UNSUPPORTED (mapper/cart type ROM ini gak didukung Peanut-GB)");
    } else if (ret == GB_INIT_INVALID_CHECKSUM) {
      Serial.println("GB_INIT_INVALID_CHECKSUM (checksum header gak cocok, liat dump di atas)");
    } else {
      Serial.println("(kode gak dikenal di error handler ini)");
    }
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_RED);
    tft.printf("GB init error: %d", ret);
    while (1) delay(1000);
  }

  gb_init_lcd(&gb, &lcd_draw_line);

  Serial.println("Emulator siap.");
}

// ---------- loop ----------
void loop() {
  joystick_update(&gb);
  gb_run_frame(&gb);

  // audio_push_frame();
}
