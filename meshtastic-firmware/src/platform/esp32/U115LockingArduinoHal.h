#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(U115_BRIDGE_ESP32)

#include "mesh/RadioLibInterface.h"
#include <SPI.h>

/**
 * RadioLib access to the SX1262 inside an M5Stack U115 (Ai-Thinker Ra-07H / ASR6501) on a
 * serial port, through the u115_bridge firmware on the U115. The same bridge drives the
 * Cardputer Zero build (src/platform/portduino/libpinedio_u115.c); this is its ESP32 side.
 *
 * The radio pins are virtual (U115_VPIN_BASE + 0 CS, +2 RESET, +4 BUSY, +6 DIO1): the bridge
 * drives NSS around each transfer, pulses reset on request and reports BUSY/DIO1.
 *
 * Bridge frames: SYNC | CMD | LEN_L | LEN_H | PAYLOAD | CHK (XOR of CMD, LEN, payload)
 *   host->bridge SYNC 0xA5, bridge->host SYNC 0x5A, reply CMD = request | 0x80
 *   0x01 PING -> "U115BR" + version, 0x02 XFER (MOSI -> MISO), 0x03 RESET radio,
 *   0x04 PINS -> [BUSY, DIO1], 0xFF error [cmd, code], 0xE1 event DIO1 rising, 0xE0 event bridge started
 *
 * DIO1 has no wire to the ESP32, so the variant sets LORA_DIO1_SOFTWARE_POLL; u115TakeIrq()
 * lets the poll skip reading the radio's IRQ register while the bridge has reported nothing.
 */
class U115LockingArduinoHal : public LockingArduinoHal
{
  public:
    U115LockingArduinoHal(SPIClass &spi, SPISettings spiSettings);

    void init() override;
    void term() override {}
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    uint32_t pinToInterrupt(uint32_t pin) override;
    void spiBegin() override {}
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override;
    void spiEndTransaction() override;
    void spiEnd() override {}

    /** True once since the bridge last reported a DIO1 rising edge (clears it) */
    bool takeIrq();
    bool bridgeFound() const { return found; }
    /** True once after the bridge reported it (re)started (U115 unplugged and back): the radio lost its settings */
    bool takeRestarted()
    {
        const bool was = restarted;
        restarted = false;
        return was;
    }

  private:
    static bool isU115Pin(uint32_t pin);
    bool openBridge();
    bool ping();
    int request(uint8_t cmd, const uint8_t *payload, uint16_t len, uint8_t *out, uint16_t outMax);
    // Reads what the bridge has sent; returns true when a reply frame completed.
    bool pump(uint32_t waitMs);

    bool found = false;
    bool resetLow = false;
    volatile bool dio1Pending = false;
    volatile bool restarted = false;
    SemaphoreHandle_t mutex = nullptr;

    // frame parser
    uint8_t st = 0, cmd = 0, chk = 0;
    uint16_t len = 0, pos = 0;
    uint8_t data[300];
    bool replyReady = false;
    uint8_t replyCmd = 0;
    uint16_t replyLen = 0;
    uint8_t replyBuf[300];
};

/** The bridge HAL in use, or nullptr (for the software IRQ poll) */
extern U115LockingArduinoHal *u115Hal;

#endif // ARCH_ESP32 && U115_BRIDGE_ESP32
