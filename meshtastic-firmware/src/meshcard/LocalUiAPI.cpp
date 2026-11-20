#if defined(MESHCARD_UI)
#include "LocalUiAPI.h"

#include "MeshService.h"
#include "PowerStatus.h"
#include "configuration.h"
#include "main.h"
#include <cstring>
#ifdef HAS_I2S
#include "AudioThread.h"
#endif

#include "SPILock.h"
#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <esp_attr.h>
#include <esp32-hal.h>
#include <esp_system.h>
#include <string>
#include <sys/stat.h>

LocalUiAPI *localUiApi = nullptr;

// ------------------------------------------------------------------ restart notes
// Every start writes one line to /littlefs/mc/restarts.log: why the chip restarted (crash,
// freeze watchdog, power dip, a restart the firmware asked for...) and what it was doing just
// before. That "just before" lives in memory that survives a restart (not a power-off).
static constexpr uint32_t kNoteMagic = 0x4d435254; // "MCRT"
RTC_NOINIT_ATTR static uint32_t noteMagic;
RTC_NOINIT_ATTR static uint32_t noteUptimeS;
RTC_NOINIT_ATTR static uint32_t noteFreeHeap;
RTC_NOINIT_ATTR static uint32_t noteMinHeap;
RTC_NOINIT_ATTR static char noteStep[16];

static void noteStepSet(const char *step)
{
    strncpy(noteStep, step, sizeof(noteStep) - 1);
    noteStep[sizeof(noteStep) - 1] = '\0';
}

static void noteAlive()
{
    noteUptimeS = millis() / 1000;
    noteFreeHeap = ESP.getFreeHeap();
    noteMinHeap = ESP.getMinFreeHeap();
    noteMagic = kNoteMagic;
}

static const char *resetReasonName(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:
        return "power on";
    case ESP_RST_EXT:
        return "reset pin";
    case ESP_RST_SW:
        return "firmware restart";
    case ESP_RST_PANIC:
        return "CRASH";
    case ESP_RST_INT_WDT:
        return "FREEZE (interrupt watchdog)";
    case ESP_RST_TASK_WDT:
        return "FREEZE (task watchdog)";
    case ESP_RST_WDT:
        return "FREEZE (watchdog)";
    case ESP_RST_DEEPSLEEP:
        return "wake from sleep";
    case ESP_RST_BROWNOUT:
        return "POWER DIP (brownout)";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "other";
    }
}

// Where a crash happened: Arduino's crash hook keeps the code addresses of the crash in memory
// that survives the restart. They go in the restart note and are turned back into file:line with
// addr2line on the firmware's .elf.
static constexpr uint32_t kPanicMagic = 0x4d435043; // "MCPC"
static constexpr int kPanicDepth = 8;
RTC_NOINIT_ATTR static uint32_t panicMagic;
RTC_NOINIT_ATTR static uint32_t panicPc[kPanicDepth];

static void IRAM_ATTR keepCrashPlace(arduino_panic_info_t *info, void *)
{
    for (int i = 0; i < kPanicDepth; i++)
        panicPc[i] = (info && (unsigned)i < info->backtrace_len) ? info->backtrace[i] : 0;
    if (info && info->backtrace_len == 0)
        panicPc[0] = (uint32_t)info->pc;
    panicMagic = kPanicMagic;
}

static void writeRestartNote()
{
    const esp_reset_reason_t why = esp_reset_reason();
    char line[300];
    if (noteMagic == kNoteMagic)
        snprintf(line, sizeof(line), "start: %s (%d) | before it: up %lus, step %s, free %lu, lowest %lu\n",
                 resetReasonName(why), (int)why, (unsigned long)noteUptimeS, noteStep, (unsigned long)noteFreeHeap,
                 (unsigned long)noteMinHeap);
    else
        snprintf(line, sizeof(line), "start: %s (%d) | before it: nothing kept (power was off)\n", resetReasonName(why),
                 (int)why);
    if (why == ESP_RST_PANIC && panicMagic == kPanicMagic) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n')
            n--;
        n += snprintf(line + n, sizeof(line) - n, " | crash at");
        for (int i = 0; i < kPanicDepth && panicPc[i] && n < sizeof(line) - 12; i++)
            n += snprintf(line + n, sizeof(line) - n, " 0x%08lx", (unsigned long)panicPc[i]);
        snprintf(line + n, sizeof(line) - n, "\n");
    }
    panicMagic = 0;
    noteMagic = 0;
    noteStepSet("boot");
    Serial.printf("MeshCard: %s", line);

    // Keep the log small: past 4 KB the old half moves to restarts.log.1.
    mkdir("/littlefs/mc", 0775); // already there after the first message; harmless if so
    const char *path = "/littlefs/mc/restarts.log";
    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fclose(f);
        if (size > 4096) {
            remove("/littlefs/mc/restarts.log.1");
            rename(path, "/littlefs/mc/restarts.log.1");
        }
    }
    f = fopen(path, "a");
    if (f) {
        fputs(line, f);
        fclose(f);
    }
}

// The USB log only starts a few seconds after boot, so the history is printed again later.
static void printRestartHistory()
{
    FILE *f = fopen("/littlefs/mc/restarts.log", "r");
    if (!f)
        return;
    char buf[160];
    Serial.println("MeshCard: restart history:");
    while (fgets(buf, sizeof(buf), f))
        Serial.printf("  %s", buf);
    fclose(f);
}

// ------------------------------------------------------------------ SD card copy
// Every message is also added to /meshcard/messages.txt on the SD card, kept forever.
// The card is opened only while writing (it needs ~10 KB of memory, the ADV has little)
// and only from the firmware loop, the same place the rest of the SPI bus is used.
static constexpr uint8_t kSdCs = 12; // Cardputer microSD: CS 12, on the 40/39/14 bus
static concurrency::Lock *sdLock = nullptr;
static std::string sdPending;
static uint32_t sdRetryAt = 0;

extern "C" void mc_sd_append(const char *text, size_t len)
{
    if (!sdLock)
        return;
    {
        concurrency::LockGuard g(sdLock);
        if (sdPending.size() + len > 4096)
            return; // no card for a long time: the built-in copy still has it
        sdPending.append(text, len);
    }
    if (localUiApi)
        localUiApi->setIntervalFromNow(0);
}

static void flushSd()
{
    std::string out;
    {
        concurrency::LockGuard g(sdLock);
        if (sdPending.empty())
            return;
        if (sdRetryAt && static_cast<int32_t>(millis() - sdRetryAt) < 0)
            return; // no card last time: look again later
        // Opening the card needs ~10 KB in one piece: wait while the screens are using it.
        if (ESP.getMaxAllocHeap() < 20 * 1024) {
            sdRetryAt = millis() + 5000;
            return;
        }
        out = sdPending; // copy, not swap: sdPending keeps its reserved room
        sdPending.clear();
    }
    bool ok = false;
    {
        concurrency::LockGuard g(spiLock);
        if (!SPI.bus())
            SPI.begin(40, 39, 14);
        if (SD.begin(kSdCs, SPI, 4000000U, "/sd", 1)) {
            SD.mkdir("/meshcard");
            File f = SD.open("/meshcard/messages.txt", FILE_APPEND);
            if (f) {
                ok = f.write(reinterpret_cast<const uint8_t *>(out.data()), out.size()) == out.size();
                f.close();
            }
            SD.end();
        }
    }
    Serial.printf("MeshCard: SD copy %s (%u bytes), free %u\n", ok ? "saved" : "no card", (unsigned)out.size(),
                  (unsigned)ESP.getFreeHeap());
    if (ok) {
        sdRetryAt = 0;
        return;
    }
    sdRetryAt = millis() + 60000;
    concurrency::LockGuard g(sdLock);
    if (out.size() + sdPending.size() <= 4096)
        sdPending.insert(0, out); // keep it for the next try
}

// ------------------------------------------------------------------ alarm sound
// The screen task asks; the firmware loop plays it (the speaker belongs to it).
static volatile int ringSound = -1;
static volatile int ringVolume = 60;

extern "C" void mc_platform_ring(int sound, int volume)
{
    ringVolume = volume;
    ringSound = sound;
    if (localUiApi)
        localUiApi->setIntervalFromNow(0);
}

#ifdef HAS_I2S
static void playRing(int sound, int volume)
{
    // Same four sounds as the Zero, as tunes the speaker can play.
    static const char *const kTunes[] = {
        "chirp:d=16,o=6,b=200:d,p,a",                          // Chirp: two rising notes
        "beeps:d=16,o=6,b=180:b,p,b,p,b,8p",                   // Beeps: three short high beeps
        "bell:d=2,o=5,b=120:a",                                // Bell: one ding
        "siren:d=32,o=5,b=200:g,b,d6,f#6,a6,c#7,g,b,d6,f#6,a6,c#7,8p", // Siren: two rising sweeps
    };
    if (!audioThread)
        return;
    if (sound < 0 || sound > 3)
        sound = 0;
    if (audioThread->isPlaying()) // something else is on the speaker: let it finish
        return;
    if (ESP.getMaxAllocHeap() < 12 * 1024) // the player needs a few KB: skip rather than crash
        return;
    audioThread->stop();
    audioThread->setGain(0.5f * volume / 100.0f); // 50 % = the firmware's own level
    audioThread->beginRttl(kTunes[sound], strlen(kTunes[sound]));
    const uint32_t start = millis();
    while (audioThread->isPlaying() && millis() - start < 3000)
        delay(5);
    audioThread->stop();
    audioThread->resetGain();
}
#endif

// Lives in the MeshCard screen code (meshcard/src/ui/esp32_main.cpp).
void meshcard_ui_start();

// ------------------------------------------------------------------ LocalUiLink

size_t LocalUiLink::Ring::put(const uint8_t *buf, size_t len)
{
    size_t n = 0;
    while (n < len && count < kSize) {
        data[head] = buf[n++];
        head = (head + 1) % kSize;
        ++count;
    }
    return n;
}

size_t LocalUiLink::Ring::get(uint8_t *buf, size_t len)
{
    size_t n = 0;
    while (n < len && count > 0) {
        buf[n++] = data[tail];
        tail = (tail + 1) % kSize;
        --count;
    }
    return n;
}

int LocalUiLink::available()
{
    concurrency::LockGuard g(&lock);
    return static_cast<int>(toFw.count);
}

int LocalUiLink::read()
{
    concurrency::LockGuard g(&lock);
    uint8_t c;
    return toFw.get(&c, 1) ? c : -1;
}

int LocalUiLink::peek()
{
    concurrency::LockGuard g(&lock);
    return toFw.count ? toFw.data[toFw.tail] : -1;
}

size_t LocalUiLink::write(const uint8_t *buf, size_t len)
{
    concurrency::LockGuard g(&lock);
    return toUi.put(buf, len);
}

size_t LocalUiLink::toUiSpace()
{
    concurrency::LockGuard g(&lock);
    return kSize - toUi.count;
}

size_t LocalUiLink::uiRead(uint8_t *buf, size_t len)
{
    concurrency::LockGuard g(&lock);
    return toUi.get(buf, len);
}

size_t LocalUiLink::uiWrite(const uint8_t *buf, size_t len)
{
    concurrency::LockGuard g(&lock);
    return toFw.put(buf, len);
}

// The screen task's end of the link (declared in meshcard/src/ui/mesh.cpp).
size_t mc_link_read(uint8_t *buf, size_t len)
{
    return localUiApi ? localUiApi->link.uiRead(buf, len) : 0;
}

size_t mc_link_write(const uint8_t *buf, size_t len)
{
    if (!localUiApi)
        return 0;
    const size_t n = localUiApi->link.uiWrite(buf, len);
    if (n)
        localUiApi->setIntervalFromNow(0); // let the firmware pick it up right away
    return n;
}

// ------------------------------------------------------------------ LocalUiAPI

LocalUiAPI::LocalUiAPI() : StreamAPI(&link), concurrency::OSThread("LocalUi")
{
    api_type = TYPE_PACKET;
    localUi = true;
    meshcardTap = &LocalUiAPI::tap;
}

int32_t LocalUiAPI::runOnce()
{
    noteAlive();
    // TEMP (2026-11-19): find what drops free memory to ~7 KB during the first 40 s.
    static uint32_t lastLowPrint = 0;
    static uint32_t lastLow = 0, lastFree = 0;
    if (millis() < 45000 && millis() - lastLowPrint >= 250) {
        lastLowPrint = millis();
        const uint32_t low = ESP.getMinFreeHeap();
        const uint32_t freeNow = ESP.getFreeHeap();
        if (low != lastLow || freeNow + 1500 < lastFree || freeNow > lastFree + 1500) {
            lastLow = low;
            lastFree = freeNow;
            Serial.printf("MC_LOW t=%lu lowest %lu free %lu\n", (unsigned long)millis(), (unsigned long)low,
                          (unsigned long)ESP.getFreeHeap());
        }
    }
    static bool historyShown = false;
    if (!historyShown && millis() > 30000) {
        historyShown = true;
        printRestartHistory();
    }
    const int sound = ringSound;
    if (sound >= 0) {
        ringSound = -1;
#ifdef HAS_I2S
        noteStepSet("alarm sound");
        playRing(sound, ringVolume);
#endif
    }
    noteStepSet("sd card");
    flushSd();
    noteStepSet("screen link");
    const int32_t delay = runOncePart();
    noteStepSet("idle");
    return delay > 20 ? 20 : delay; // the screen expects answers quickly
}

// A full frame must fit, or the packet PhoneAPI just handed over would be lost. The screen
// task empties the link every few ms, so wait for it a little rather than drop.
bool LocalUiAPI::canWriteFrame(size_t frameLen)
{
    for (int i = 0; i < 100; ++i) {
        if (link.toUiSpace() >= frameLen)
            return true;
        delay(3);
    }
    return false;
}

void LocalUiAPI::tap(const meshtastic_MeshPacket *p)
{
    LocalUiAPI *self = localUiApi;
    if (!self || !p)
        return;
    meshtastic_MeshPacket *copy = packetPool.allocCopy(*p);
    if (!copy)
        return; // pool is short: the screen misses this one rather than the radio
    meshtastic_MeshPacket *dropped = nullptr;
    {
        concurrency::LockGuard g(&self->qLock);
        if (self->qCount == kQueueLen) { // full: drop the oldest
            dropped = self->queue[self->qHead];
            self->qHead = (self->qHead + 1) % kQueueLen;
            --self->qCount;
        }
        self->queue[(self->qHead + self->qCount) % kQueueLen] = copy;
        ++self->qCount;
    }
    if (dropped)
        packetPool.release(dropped);
    self->setIntervalFromNow(0);
}

meshtastic_MeshPacket *LocalUiAPI::takeLocalPacket()
{
    concurrency::LockGuard g(&qLock);
    if (qCount == 0)
        return nullptr;
    meshtastic_MeshPacket *p = queue[qHead];
    qHead = (qHead + 1) % kQueueLen;
    --qCount;
    return p;
}

void meshcardSetup()
{
    if (localUiApi)
        return;
    writeRestartNote();
    set_arduino_panic_handler(keepCrashPlace, nullptr);
    sdPending.reserve(4096); // one piece now, so adding a message never needs a new one
    sdLock = new concurrency::Lock();
    localUiApi = new LocalUiAPI();
    meshcard_ui_start();
}

// The screens wait for Bluetooth to be set up first: it needs one big piece of memory
// at start, and the screens can live with what is left (no PSRAM on the ADV).
extern "C" bool mc_platform_ready()
{
    if (millis() > 20000)
        return true; // never wait forever
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
    static uint32_t bleSeen = 0;
    if (config.bluetooth.enabled) {
        if (!nimbleBluetooth)
            return false;
        if (!bleSeen)
            bleSeen = millis();
        return millis() - bleSeen > 1500; // let its setup finish
    }
#endif
    return millis() > 3000;
}

// MeshCard top bar battery (overrides the weak default in meshcard/src/ui/battery.cpp).
extern "C" bool mc_platform_battery(int *percent, bool *charging)
{
    if (!powerStatus || !powerStatus->getHasBattery())
        return false;
    *percent = powerStatus->getBatteryChargePercent();
    // The ADV has no "power in" pin; the firmware only calls it plugged once the
    // battery reads full. A computer on the USB port is also seen right away.
    // (A voltage-jump guess was rejected: radio sends make the voltage jump too.)
    bool usbHost = false;
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
    usbHost = HWCDC::isPlugged();
#endif
    *charging = powerStatus->getIsCharging() || powerStatus->getHasUSB() || usbHost;
    return true;
}

#endif
