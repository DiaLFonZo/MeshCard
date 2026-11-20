#define USE_ST7789
#define HAS_SPI_TFT 1
#define ST7789_NSS 37
#define ST7789_RS 34  // DC
#define ST7789_SDA 35 // MOSI
#define ST7789_SCK 36
#define ST7789_RESET 33
#define ST7789_MISO -1
#define ST7789_BUSY -1
// #define VTFT_CTRL 38
#define VTFT_LEDA 38
#define TFT_BACKLIGHT_ON HIGH
// #define ST7789_BL (32+6)
#define ST7789_SPI_HOST SPI2_HOST
// #define TFT_BL (32+6)
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 135
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define HAS_PHYSICAL_KEYBOARD 1

// Backlight is controlled to power rail on this board, this also powers the neopixel
// #define PIN_POWER_EN 38

#define BUTTON_PIN 0

#define I2C_SDA 8
#define I2C_SCL 9

#define I2C_SDA1 2
#define I2C_SCL1 1

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 40
#define LORA_MISO 39
#define LORA_MOSI 14
#define LORA_CS 5 // NSS

#define USE_SX1262
#define LORA_DIO0 -1
#define LORA_RESET 3
#define LORA_RST 3
#define LORA_DIO1 4
#define LORA_DIO2 6
#define LORA_DIO3 RADIOLIB_NC

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 15
#define GPS_TX_PIN 13
#define HAS_GPS 1
#define GPS_BAUDRATE 115200

// audio codec ES8311
#define HAS_I2S
#define DAC_I2S_BCK 41
#define DAC_I2S_WS 43
#define DAC_I2S_DOUT 42
#define DAC_I2S_DIN 46
#define DAC_I2S_MCLK 45 // dummy

// TCA8418 keyboard
#define I2C_NO_RESCAN
#define KB_INT 11

#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 21
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

#define BATTERY_PIN 10 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC_CHANNEL_9
#define ADC_MULTIPLIER 2 * 1.02 // 100k + 100k, and add 2% to kick the voltage over the max voltage to show charging.

// BMI270 6-axis IMU on internal I2C bus
#define HAS_BMI270

#ifdef U115_BRIDGE_ESP32
// MeshCard U115 build: the radio is an M5Stack U115 (Ra-07H) on the Grove port, running the
// u115_bridge firmware (serial on G1/G2, 115200 8N1), not the Cap LoRa-1262 on the EXT header.
#undef I2C_SDA1 // G1/G2 are the bridge's serial line, not a second I2C bus
#undef I2C_SCL1
#define U115_GROVE_PIN_A 1
#define U115_GROVE_PIN_B 2
#define U115_VPIN_BASE 100
#undef SX126X_CS
#undef SX126X_DIO1
#undef SX126X_BUSY
#undef SX126X_RESET
#define SX126X_CS (U115_VPIN_BASE + 0)
#define SX126X_RESET (U115_VPIN_BASE + 2)
#define SX126X_BUSY (U115_VPIN_BASE + 4)
#define SX126X_DIO1 (U115_VPIN_BASE + 6)
// Ra-07H runs on a crystal, no TCXO
#undef SX126X_DIO3_TCXO_VOLTAGE
#undef TCXO_OPTIONAL
// DIO1 is not wired to the ESP32: poll the radio, steered by the bridge's DIO1 events
#define LORA_DIO1_SOFTWARE_POLL 1
#endif
