#include "configuration.h"

#if defined(ARCH_ESP32) && defined(U115_BRIDGE_ESP32)

#include "U115LockingArduinoHal.h"

#ifndef U115_VPIN_BASE
#define U115_VPIN_BASE 100
#endif
#define U115_PIN_CS (U115_VPIN_BASE + 0)
#define U115_PIN_RESET (U115_VPIN_BASE + 2)
#define U115_PIN_BUSY (U115_VPIN_BASE + 4)
#define U115_PIN_DIO1 (U115_VPIN_BASE + 6)

#define SYNC_OUT 0xA5
#define SYNC_IN 0x5A
#define MAX_PAYLOAD 300
#define REPLY_TIMEOUT_MS 500

// UART2: UART1 is the GPS port of this board and UART0 the console.
static HardwareSerial u115Uart(2);

U115LockingArduinoHal *u115Hal = nullptr;

U115LockingArduinoHal::U115LockingArduinoHal(SPIClass &spi, SPISettings spiSettings) : LockingArduinoHal(spi, spiSettings)
{
    mutex = xSemaphoreCreateRecursiveMutex();
    u115Hal = this;
}

bool U115LockingArduinoHal::isU115Pin(uint32_t pin)
{
    return pin >= U115_VPIN_BASE && pin <= U115_VPIN_BASE + 15;
}

void U115LockingArduinoHal::init()
{
    if (!found)
        found = openBridge();
}

// The Grove signals' direction is not marked on the board: try both ways round.
bool U115LockingArduinoHal::openBridge()
{
#ifdef U115_GROVE_POWER_PIN
    ::pinMode(U115_GROVE_POWER_PIN, OUTPUT);
    ::digitalWrite(U115_GROVE_POWER_PIN, HIGH);
    ::delay(300); // the bridge firmware starts in ~100 ms
#endif
    const int pins[2][2] = {{U115_GROVE_PIN_A, U115_GROVE_PIN_B}, {U115_GROVE_PIN_B, U115_GROVE_PIN_A}}; // {rx, tx}
    u115Uart.setRxBufferSize(1024);
    for (int attempt = 0; attempt < 2; attempt++) {
        const int rx = pins[attempt][0], tx = pins[attempt][1];
        u115Uart.end();
        u115Uart.begin(115200, SERIAL_8N1, rx, tx);
        ::delay(20);
        while (u115Uart.available())
            u115Uart.read();
        st = 0;
        for (int tries = 0; tries < 3; tries++) {
            if (ping()) {
                LOG_INFO("U115: bridge found on Grove (RX GPIO%d, TX GPIO%d)", rx, tx);
                return true;
            }
        }
    }
    u115Uart.end();
    LOG_ERROR("U115: no bridge answering on the Grove port (G1/G2)");
    return false;
}

bool U115LockingArduinoHal::ping()
{
    uint8_t pong[7];
    int n = request(0x01, nullptr, 0, pong, sizeof(pong));
    if (n == 7 && memcmp(pong, "U115BR", 6) == 0) {
        LOG_INFO("U115: bridge firmware v%u", pong[6]);
        return true;
    }
    return false;
}

// Parses bridge frames until a reply is complete or waitMs passes (0 = only what is buffered).
bool U115LockingArduinoHal::pump(uint32_t waitMs)
{
    const uint32_t start = ::millis();
    for (;;) {
        while (u115Uart.available()) {
            const uint8_t b = (uint8_t)u115Uart.read();
            switch (st) {
            case 0:
                if (b == SYNC_IN)
                    st = 1;
                break;
            case 1:
                cmd = chk = b;
                st = 2;
                break;
            case 2:
                len = b;
                chk ^= b;
                st = 3;
                break;
            case 3:
                len |= (uint16_t)b << 8;
                chk ^= b;
                pos = 0;
                st = len > MAX_PAYLOAD ? 0 : (len ? 4 : 5);
                break;
            case 4:
                data[pos++] = b;
                chk ^= b;
                if (pos >= len)
                    st = 5;
                break;
            case 5:
                st = 0;
                if (b != chk) {
                    LOG_WARN("U115: bad frame checksum (cmd 0x%02X)", cmd);
                    break;
                }
                if (cmd == 0xE1) {
                    dio1Pending = true;
                } else if (cmd == 0xE0) {
                    LOG_WARN("U115: bridge (re)started, firmware v%u", len ? data[0] : 0);
                    restarted = true;
                } else {
                    replyCmd = cmd;
                    replyLen = len;
                    memcpy(replyBuf, data, len);
                    replyReady = true;
                    return true;
                }
                break;
            }
        }
        if (waitMs == 0 || ::millis() - start >= waitMs)
            return false;
        // A reply normally comes within a millisecond or two; past that, give the other
        // tasks the processor instead of spinning (an unplugged U115 never answers).
        if (::millis() - start >= 3)
            vTaskDelay(1);
        else
            ::delayMicroseconds(50);
    }
}

int U115LockingArduinoHal::request(uint8_t c, const uint8_t *payload, uint16_t n, uint8_t *out, uint16_t outMax)
{
    if (n > MAX_PAYLOAD)
        return -1;
    uint8_t head[4] = {SYNC_OUT, c, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8)};
    uint8_t sum = c ^ head[2] ^ head[3];
    for (uint16_t i = 0; i < n; i++)
        sum ^= payload[i];

    // U115 not answering (unplugged): after 5 misses in a row, only try again every 2 s so
    // the radio code cannot keep the processor waiting 500 ms per command.
    static uint8_t misses = 0;
    static uint32_t quietUntil = 0;
    if (misses >= 5 && static_cast<int32_t>(::millis() - quietUntil) < 0)
        return -1;

    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    replyReady = false;
    u115Uart.write(head, 4);
    if (n)
        u115Uart.write(payload, n);
    u115Uart.write(&sum, 1);

    int ret = -1;
    const uint32_t start = ::millis();
    while (!replyReady && ::millis() - start < REPLY_TIMEOUT_MS)
        pump(REPLY_TIMEOUT_MS - (::millis() - start));
    if (!replyReady) {
        if (found && misses < 5)
            LOG_WARN("U115: no reply to command 0x%02X", c);
        if (misses < 255)
            misses++;
        if (misses >= 5)
            quietUntil = ::millis() + 2000;
    } else if (replyCmd == 0xFF) {
        LOG_WARN("U115: bridge error %u on command 0x%02X", replyLen > 1 ? replyBuf[1] : 0, c);
    } else if (replyCmd != (c | 0x80)) {
        LOG_WARN("U115: unexpected reply 0x%02X to 0x%02X", replyCmd, c);
    } else {
        const uint16_t k = replyLen < outMax ? replyLen : outMax;
        if (out && k)
            memcpy(out, replyBuf, k);
        ret = replyLen;
    }
    if (replyReady)
        misses = 0; // it answered (even an error reply means it is there)
    replyReady = false;
    xSemaphoreGiveRecursive(mutex);
    return ret;
}

bool U115LockingArduinoHal::takeIrq()
{
    if (!found)
        return false;
    if (xSemaphoreTakeRecursive(mutex, 0) == pdTRUE) {
        pump(0);
        xSemaphoreGiveRecursive(mutex);
    }
    const bool was = dio1Pending;
    dio1Pending = false;
    return was;
}

void U115LockingArduinoHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (isU115Pin(pin))
        return; // the bridge owns the radio pins
    LockingArduinoHal::pinMode(pin, mode);
}

void U115LockingArduinoHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (!isU115Pin(pin)) {
        LockingArduinoHal::digitalWrite(pin, value);
        return;
    }
    if (pin == U115_PIN_RESET) {
        // RadioLib pulses reset low then high; the bridge does the whole pulse
        if (value == GpioLevelLow) {
            resetLow = true;
        } else if (resetLow) {
            resetLow = false;
            request(0x03, nullptr, 0, nullptr, 0);
        }
    }
    // CS: the bridge drives NSS around each transfer
}

uint32_t U115LockingArduinoHal::digitalRead(uint32_t pin)
{
    if (!isU115Pin(pin))
        return LockingArduinoHal::digitalRead(pin);
    if (pin != U115_PIN_BUSY && pin != U115_PIN_DIO1)
        return 0;
    uint8_t pins[2] = {0, 0};
    if (request(0x04, nullptr, 0, pins, sizeof(pins)) != 2)
        return pin == U115_PIN_BUSY ? 1 : 0; // no answer: report busy, so RadioLib times out rather than talks
    return pin == U115_PIN_BUSY ? pins[0] : pins[1];
}

void U115LockingArduinoHal::attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode)
{
    if (interruptNum == RADIOLIB_NC)
        return;
    LockingArduinoHal::attachInterrupt(interruptNum, cb, mode);
}

void U115LockingArduinoHal::detachInterrupt(uint32_t interruptNum)
{
    if (interruptNum == RADIOLIB_NC)
        return;
    LockingArduinoHal::detachInterrupt(interruptNum);
}

uint32_t U115LockingArduinoHal::pinToInterrupt(uint32_t pin)
{
    if (isU115Pin(pin))
        return RADIOLIB_NC;
    return LockingArduinoHal::pinToInterrupt(pin);
}

// No SPI bus here: only one radio command at a time on the serial link.
void U115LockingArduinoHal::spiBeginTransaction()
{
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}

void U115LockingArduinoHal::spiEndTransaction()
{
    xSemaphoreGiveRecursive(mutex);
}

void U115LockingArduinoHal::spiTransfer(uint8_t *out, size_t n, uint8_t *in)
{
    if (n == 0)
        return;
    if (!found || n > MAX_PAYLOAD) {
        if (in)
            memset(in, 0xFF, n);
        return;
    }
    uint8_t buf[MAX_PAYLOAD];
    if (request(0x02, out, (uint16_t)n, buf, sizeof(buf)) != (int)n) {
        if (in)
            memset(in, 0xFF, n);
        return;
    }
    if (in)
        memcpy(in, buf, n);
}

#endif // ARCH_ESP32 && U115_BRIDGE_ESP32
