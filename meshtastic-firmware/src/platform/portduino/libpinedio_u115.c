// libpinedio API over the U115 radio bridge (M5Stack Unit LoRaWAN915, Ai-Thinker
// Ra-07H / ASR6501 running u115_bridge firmware), replacing the CH341 backends.
//
// The SX1262 sits inside the U115; its bridge firmware runs each SPI transaction
// we send over a serial port and returns the radio's answer. meshtasticd keeps
// using Ch341Hal unchanged: config "Lora: spidev: ch341" with the CH341 pin
// numbers below.
//
// Serial port: environment variable U115_PORT (default COM6 on Windows,
// /dev/serial0 on Linux), 115200 8N1.
//
// Bridge frames: SYNC | CMD | LEN_L | LEN_H | PAYLOAD | CHK (XOR of CMD, LEN, payload)
//   host->bridge SYNC 0xA5, bridge->host SYNC 0x5A, reply CMD = request | 0x80
//   0x01 PING, 0x02 XFER (MOSI -> MISO), 0x03 RESET radio, 0x04 PINS -> [BUSY, DIO1]
//   0xFF error [cmd, code], 0xE1 event: DIO1 rising, 0xE0 event: bridge started

#if defined(ARCH_PORTDUINO) && defined(PINEDIO_U115)

#include "libpinedio-usb.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

// Pin numbers as for a CH341 stick, so the usual config works: CS 0, Reset 2, Busy 4, IRQ 6
#define U115_PIN_CS 0
#define U115_PIN_RESET 2
#define U115_PIN_BUSY 4
#define U115_PIN_DIO1 6

#define SYNC_OUT 0xA5
#define SYNC_IN 0x5A
#define MAX_PAYLOAD 300
#define REPLY_TIMEOUT_MS 500

// ---------------------------------------------------------------- serial port
#ifdef _WIN32
static HANDLE port = INVALID_HANDLE_VALUE;

static bool port_open(const char *name)
{
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\%s", name);
    port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (port == INVALID_HANDLE_VALUE)
        return false;
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(port, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = dcb.fOutX = dcb.fInX = FALSE;
    SetCommState(port, &dcb);
    COMMTIMEOUTS t = {0};
    t.ReadIntervalTimeout = MAXDWORD; // return what is there, waiting up to the total below
    t.ReadTotalTimeoutMultiplier = MAXDWORD;
    t.ReadTotalTimeoutConstant = 50;
    SetCommTimeouts(port, &t);
    PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}
static int port_read(uint8_t *buf, int len)
{
    DWORD n = 0;
    if (!ReadFile(port, buf, len, &n, NULL))
        return -1;
    return (int)n;
}
static bool port_write(const uint8_t *buf, int len)
{
    DWORD n = 0;
    return WriteFile(port, buf, len, &n, NULL) && (int)n == len;
}
static void port_close(void)
{
    if (port != INVALID_HANDLE_VALUE)
        CloseHandle(port);
    port = INVALID_HANDLE_VALUE;
}
#define DEFAULT_PORT "COM6"
#else
static int port = -1;

static bool port_open(const char *name)
{
    port = open(name, O_RDWR | O_NOCTTY);
    if (port < 0)
        return false;
    struct termios tio;
    tcgetattr(port, &tio);
    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CRTSCTS | CSTOPB);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1; // 100 ms read timeout
    tcsetattr(port, TCSANOW, &tio);
    tcflush(port, TCIOFLUSH);
    return true;
}
static int port_read(uint8_t *buf, int len)
{
    int n = (int)read(port, buf, len);
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
        return 0;
    return n;
}
static bool port_write(const uint8_t *buf, int len)
{
    while (len > 0) {
        int n = (int)write(port, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        buf += n;
        len -= n;
    }
    return true;
}
static void port_close(void)
{
    if (port >= 0)
        close(port);
    port = -1;
}
#define DEFAULT_PORT "/dev/serial0"
#endif

// ---------------------------------------------------------------- shared state
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;   // guards everything below
static pthread_mutex_t req_lock = PTHREAD_MUTEX_INITIALIZER; // one request at a time
static pthread_cond_t reply_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t event_cond = PTHREAD_COND_INITIALIZER;

static pthread_t reader_thread, event_thread;
static bool threads_running = false;
static volatile bool stopping = false;

static bool reply_ready = false;
static uint8_t reply_cmd;
static uint16_t reply_len;
static uint8_t reply_buf[MAX_PAYLOAD];

static unsigned dio1_events = 0; // rising edges reported by the bridge, not yet dispatched
static struct pinedio_inst *the_inst = NULL;

static bool cs_low = false;
static bool reset_low = false;

static void deadline_after_ms(struct timespec *ts, int ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

// Reads every frame from the bridge: replies go to the waiting request, DIO1
// events to the event thread (callbacks re-enter this library, so they must not
// run here, where the replies they wait for are read).
static void *reader_fn(void *arg)
{
    (void)arg;
    enum { S_SYNC, S_CMD, S_LL, S_LH, S_DATA, S_CHK } st = S_SYNC;
    uint8_t cmd = 0, chk = 0, data[MAX_PAYLOAD];
    uint16_t len = 0, pos = 0;
    uint8_t buf[256];

    while (!stopping) {
        int n = port_read(buf, sizeof(buf));
        if (n < 0) {
            fprintf(stderr, "U115: serial read failed\n");
            pthread_mutex_lock(&lock);
            if (the_inst)
                the_inst->in_error = true;
            pthread_mutex_unlock(&lock);
            break;
        }
        for (int i = 0; i < n; i++) {
            uint8_t b = buf[i];
            switch (st) {
            case S_SYNC:
                if (b == SYNC_IN)
                    st = S_CMD;
                break;
            case S_CMD:
                cmd = chk = b;
                st = S_LL;
                break;
            case S_LL:
                len = b;
                chk ^= b;
                st = S_LH;
                break;
            case S_LH:
                len |= (uint16_t)b << 8;
                chk ^= b;
                pos = 0;
                st = len > MAX_PAYLOAD ? S_SYNC : (len ? S_DATA : S_CHK);
                break;
            case S_DATA:
                data[pos++] = b;
                chk ^= b;
                if (pos >= len)
                    st = S_CHK;
                break;
            case S_CHK:
                st = S_SYNC;
                if (b != chk) {
                    fprintf(stderr, "U115: bad frame checksum (cmd 0x%02X)\n", cmd);
                    break;
                }
                pthread_mutex_lock(&lock);
                if (cmd == 0xE1) {
                    dio1_events++;
                    pthread_cond_signal(&event_cond);
                } else if (cmd == 0xE0) {
                    fprintf(stderr, "U115: bridge (re)started, firmware v%u\n", len ? data[0] : 0);
                } else {
                    reply_cmd = cmd;
                    reply_len = len;
                    memcpy(reply_buf, data, len);
                    reply_ready = true;
                    pthread_cond_signal(&reply_cond);
                }
                pthread_mutex_unlock(&lock);
                break;
            }
        }
    }
    return NULL;
}

static void *event_fn(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&lock);
    while (!stopping) {
        if (dio1_events == 0) {
            pthread_cond_wait(&event_cond, &lock);
            continue;
        }
        dio1_events--;
        void (*cb)(void) = NULL;
        if (the_inst && (the_inst->interrupts[U115_PIN_DIO1].mode & PINEDIO_INT_MODE_RISING))
            cb = the_inst->interrupts[U115_PIN_DIO1].callback;
        pthread_mutex_unlock(&lock);
        if (cb)
            cb();
        pthread_mutex_lock(&lock);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

// Sends one command and waits for its reply. Returns the reply payload length, or < 0.
static int32_t request(uint8_t cmd, const uint8_t *payload, uint16_t len, uint8_t *out, uint16_t out_max)
{
    uint8_t frame[MAX_PAYLOAD + 5];
    if (len > MAX_PAYLOAD)
        return -1;
    frame[0] = SYNC_OUT;
    frame[1] = cmd;
    frame[2] = (uint8_t)(len & 0xFF);
    frame[3] = (uint8_t)(len >> 8);
    uint8_t chk = cmd ^ frame[2] ^ frame[3];
    for (uint16_t i = 0; i < len; i++) {
        frame[4 + i] = payload[i];
        chk ^= payload[i];
    }
    frame[4 + len] = chk;

    pthread_mutex_lock(&req_lock);
    pthread_mutex_lock(&lock);
    reply_ready = false;
    pthread_mutex_unlock(&lock);

    int32_t ret = -1;
    if (!port_write(frame, len + 5)) {
        fprintf(stderr, "U115: serial write failed\n");
    } else {
        struct timespec ts;
        deadline_after_ms(&ts, REPLY_TIMEOUT_MS);
        pthread_mutex_lock(&lock);
        while (!reply_ready) {
            if (pthread_cond_timedwait(&reply_cond, &lock, &ts) != 0)
                break;
        }
        if (!reply_ready) {
            fprintf(stderr, "U115: no reply to command 0x%02X\n", cmd);
        } else if (reply_cmd == 0xFF) {
            fprintf(stderr, "U115: bridge error %u on command 0x%02X\n", reply_len > 1 ? reply_buf[1] : 0, cmd);
        } else if (reply_cmd != (cmd | 0x80)) {
            fprintf(stderr, "U115: unexpected reply 0x%02X to 0x%02X\n", reply_cmd, cmd);
        } else {
            uint16_t n = reply_len < out_max ? reply_len : out_max;
            if (out && n)
                memcpy(out, reply_buf, n);
            ret = reply_len;
        }
        reply_ready = false;
        pthread_mutex_unlock(&lock);
    }
    pthread_mutex_unlock(&req_lock);
    return ret;
}

// ---------------------------------------------------------------- libpinedio API
int32_t pinedio_set_option(struct pinedio_inst *inst, enum pinedio_option option, uint32_t value)
{
    if (option < PINEDIO_OPTION_MAX)
        inst->options[option] = value;
    return 0;
}

int32_t pinedio_init(struct pinedio_inst *inst, void *driver)
{
    (void)driver;
    inst->in_error = false;
    for (int i = 0; i < PINEDIO_INT_PIN_MAX; i++) {
        inst->interrupts[i].callback = NULL;
        inst->interrupts[i].mode = 0;
    }

    const char *name = getenv("U115_PORT");
    if (!name || !*name)
        name = DEFAULT_PORT;
    if (!port_open(name)) {
        fprintf(stderr, "U115: cannot open serial port %s (set U115_PORT)\n", name);
        return -1;
    }

    stopping = false;
    the_inst = inst;
    if (pthread_create(&reader_thread, NULL, reader_fn, NULL) != 0 ||
        pthread_create(&event_thread, NULL, event_fn, NULL) != 0) {
        fprintf(stderr, "U115: cannot start threads\n");
        port_close();
        return -2;
    }
    threads_running = true;

    uint8_t pong[7];
    int32_t n = -1;
    for (int tries = 0; tries < 3 && n != 7; tries++)
        n = request(0x01, NULL, 0, pong, sizeof(pong));
    if (n != 7 || memcmp(pong, "U115BR", 6) != 0) {
        fprintf(stderr, "U115: no bridge answering on %s\n", name);
        pinedio_deinit(inst);
        return -3;
    }
    fprintf(stderr, "U115: bridge v%u on %s\n", pong[6], name);
    strncpy(inst->serial_number, "U115", sizeof(inst->serial_number) - 1);
    snprintf(inst->product_string, sizeof(inst->product_string), "M5Stack U115 bridge v%u", pong[6]);
    return 0;
}

int32_t pinedio_set_pin_mode(struct pinedio_inst *inst, uint32_t pin, uint32_t mode)
{
    (void)inst;
    (void)pin;
    (void)mode;
    return 0; // the bridge owns the radio pins
}

int32_t pinedio_digital_write(struct pinedio_inst *inst, uint32_t pin, bool active)
{
    if (pin == U115_PIN_CS) {
        cs_low = !active; // the bridge drives NSS around each transfer
        return 0;
    }
    if (pin == U115_PIN_RESET) {
        // RadioLib pulses reset low then high; the bridge does the whole pulse
        if (!active) {
            reset_low = true;
        } else if (reset_low) {
            reset_low = false;
            if (request(0x03, NULL, 0, NULL, 0) < 0) {
                inst->in_error = true;
                return -1;
            }
        }
        return 0;
    }
    return 0;
}

int32_t pinedio_set_cs(struct pinedio_inst *inst, bool active)
{
    return pinedio_digital_write(inst, U115_PIN_CS, active);
}

int32_t pinedio_digital_read(struct pinedio_inst *inst, uint32_t pin)
{
    if (pin != U115_PIN_BUSY && pin != U115_PIN_DIO1)
        return 0;
    uint8_t pins[2] = {0, 0};
    if (request(0x04, NULL, 0, pins, sizeof(pins)) != 2) {
        inst->in_error = true;
        return -1;
    }
    return pin == U115_PIN_BUSY ? pins[0] : pins[1];
}

int32_t pinedio_get_irq_state(struct pinedio_inst *inst, uint32_t pin)
{
    return pinedio_digital_read(inst, pin);
}

int32_t pinedio_transceive(struct pinedio_inst *inst, uint8_t *write_buf, uint8_t *read_buf, uint32_t count)
{
    if (count == 0)
        return 0;
    if (count > MAX_PAYLOAD)
        return -1;
    uint8_t in[MAX_PAYLOAD];
    if (request(0x02, write_buf, (uint16_t)count, in, sizeof(in)) != (int32_t)count) {
        inst->in_error = true;
        return -1;
    }
    if (read_buf)
        memcpy(read_buf, in, count);
    return 0;
}

int32_t pinedio_write_read(struct pinedio_inst *inst, uint8_t *writearr, uint32_t writecnt, uint8_t *readarr, uint32_t readcnt)
{
    uint32_t total = writecnt + readcnt;
    if (total > MAX_PAYLOAD)
        return -1;
    uint8_t buf[MAX_PAYLOAD];
    memcpy(buf, writearr, writecnt);
    memset(buf + writecnt, 0, readcnt);
    int32_t ret = pinedio_transceive(inst, buf, buf, total);
    if (ret == 0)
        memcpy(readarr, buf + writecnt, readcnt);
    return ret;
}

int32_t pinedio_attach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin, enum pinedio_int_mode int_mode,
                                 void (*callback)(void))
{
    if (int_pin >= PINEDIO_INT_PIN_MAX)
        return -1;
    pthread_mutex_lock(&lock);
    inst->interrupts[int_pin].previous_state = 255;
    inst->interrupts[int_pin].mode = int_mode;
    inst->interrupts[int_pin].callback = callback;
    pthread_mutex_unlock(&lock);
    return 0;
}

int32_t pinedio_deattach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin)
{
    if (int_pin >= PINEDIO_INT_PIN_MAX)
        return -1;
    pthread_mutex_lock(&lock);
    inst->interrupts[int_pin].callback = NULL;
    pthread_mutex_unlock(&lock);
    return 0;
}

void pinedio_deinit(struct pinedio_inst *inst)
{
    pthread_mutex_lock(&lock);
    stopping = true;
    the_inst = NULL;
    pthread_cond_broadcast(&event_cond);
    pthread_mutex_unlock(&lock);
    if (threads_running) {
        pthread_join(reader_thread, NULL); // its reads time out within ~100 ms
        pthread_join(event_thread, NULL);
        threads_running = false;
    }
    port_close();
    inst->in_error = false;
}

#endif // ARCH_PORTDUINO && PINEDIO_U115
