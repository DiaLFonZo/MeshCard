#if defined(MESHCARD_UI)
#include "LocalUiAPI.h"

#include "MeshService.h"
#include "NodeDB.h"
#include "PowerStatus.h"
#include "TypeConversions.h"
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
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

// Every memory request that fails: how big it was, which task asked, and how much was free.
// Kept across a restart, so the restart note says what ran out right before a crash.
static constexpr uint32_t kFailMagic = 0x4d434146; // "MCAF"
RTC_NOINIT_ATTR static uint32_t failMagic;
RTC_NOINIT_ATTR static uint32_t failCount;
RTC_NOINIT_ATTR static uint32_t failSize;
RTC_NOINIT_ATTR static uint32_t failFree;
RTC_NOINIT_ATTR static uint32_t failLargest;
RTC_NOINIT_ATTR static char failTask[12];
static volatile bool failToPrint = false;

static void onAllocFail(size_t size, uint32_t caps, const char *fn)
{
    (void)caps;
    (void)fn;
    if (failMagic != kFailMagic)
        failCount = 0;
    failCount++;
    failSize = size;
    failFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    failLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const char *t = pcTaskGetName(nullptr);
    strncpy(failTask, t ? t : "?", sizeof(failTask) - 1);
    failTask[sizeof(failTask) - 1] = '\0';
    failMagic = kFailMagic;
    failToPrint = true;
}

static void writeRestartNote()
{
    const esp_reset_reason_t why = esp_reset_reason();
    char line[360];
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
    if (failMagic == kFailMagic && failCount) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n')
            n--;
        snprintf(line + n, sizeof(line) - n, " | %lu memory request(s) failed, last %lu bytes by task %s (free %lu, largest %lu)\n",
                 (unsigned long)failCount, (unsigned long)failSize, failTask, (unsigned long)failFree,
                 (unsigned long)failLargest);
    }
    failMagic = 0;
    failCount = 0;
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
// The Zero's four alarm sounds, sample for sample (same parts as make_sound() in meshcard/src/common/alert.cpp),
// made while playing so they need no buffer.
namespace
{
struct TonePart {
    float f0, f1, secs, amp; // a sweep from f0 to f1 Hz; amp 0 = silence
    bool decay;
};
constexpr int kRingRate = 22050;
const TonePart kChirp[] = {{1200, 1200, 0.12f, 0.85f, false}, {0, 0, 0.05f, 0, false}, {1800, 1800, 0.14f, 0.85f, false}};
const TonePart kBeeps[] = {{2000, 2000, 0.10f, 0.9f, false}, {0, 0, 0.08f, 0, false}, {2000, 2000, 0.10f, 0.9f, false},
                           {0, 0, 0.08f, 0, false},          {2000, 2000, 0.10f, 0.9f, false}, {0, 0, 0.08f, 0, false}};
const TonePart kBell[] = {{880, 880, 0.9f, 0.9f, true}};
const TonePart kSiren[] = {{800, 2200, 0.45f, 0.95f, false}, {800, 2200, 0.45f, 0.95f, false}};
} // namespace

static void playRing(int sound, int volume)
{
    if (!audioThread)
        return;
    if (audioThread->isPlaying()) // something else is on the speaker: let it finish
        return;
    const TonePart *parts = kChirp;
    size_t count = sizeof(kChirp) / sizeof(kChirp[0]);
    if (sound == 1) {
        parts = kBeeps;
        count = sizeof(kBeeps) / sizeof(kBeeps[0]);
    } else if (sound == 2) {
        parts = kBell;
        count = 1;
    } else if (sound == 3) {
        parts = kSiren;
        count = 2;
    }
    size_t part = 0;
    int i = 0;
    float phase = 0;
    audioThread->setGain(0.5f * volume / 100.0f); // 50 % = the firmware's own level
    audioThread->playSamples(kRingRate, [&](int16_t &out) {
        while (part < count && i >= (int)(parts[part].secs * kRingRate)) {
            ++part;
            i = 0;
            phase = 0;
        }
        if (part >= count)
            return false;
        const TonePart &p = parts[part];
        const int n = (int)(p.secs * kRingRate);
        const int fade = kRingRate / 200; // 5 ms edges, no clicks
        const float t = (float)i / n;
        float a = p.amp;
        if (a > 0) {
            phase += 2 * (float)M_PI * (p.f0 + (p.f1 - p.f0) * t) / kRingRate;
            if (p.decay)
                a *= expf(-4.0f * t);
            if (i < fade)
                a *= (float)i / fade;
            if (n - i < fade)
                a *= (float)(n - i) / fade;
        }
        float v = sinf(phase);
        if (p.decay)
            v = 0.7f * v + 0.3f * sinf(2 * phase);
        out = a > 0 ? (int16_t)(v * a * 32767) : 0;
        ++i;
        return true;
    });
    audioThread->resetGain();
}
#endif

// Lives in the MeshCard screen code (meshcard/src/ui/esp32_main.cpp).
void meshcard_ui_start();

// ------------------------------------------------------------------ LocalUiAPI

namespace
{
void refreshUiNodes(); // below, with the screens' tables
void sendOutbox();
} // namespace
// The screens' slow jobs (saving messages to the built-in storage), run on this thread so
// the screen task never waits on the flash. Lives in meshcard/src/ui/mesh.cpp.
void mc_ui_background_work();

LocalUiAPI::LocalUiAPI() : concurrency::OSThread("MeshCard")
{
    meshcardTap = &LocalUiAPI::tap;
}

int32_t LocalUiAPI::runOnce()
{
    noteAlive();
    // TEMP (2026-11-20, memory hunt): print every new low of free memory, and every failed request.
    static uint32_t lastLow = 0xffffffff;
    {
        const uint32_t low = ESP.getMinFreeHeap();
        if (low + 512 < lastLow) {
            lastLow = low;
            Serial.printf("MC_LOW t=%lu lowest %lu free %lu largest %lu\n", (unsigned long)millis(), (unsigned long)low,
                          (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
        }
        if (failToPrint) {
            failToPrint = false;
            Serial.printf("MC_FAIL %lu so far, last %lu bytes by task %s (free %lu, largest %lu)\n", (unsigned long)failCount,
                          (unsigned long)failSize, failTask, (unsigned long)failFree, (unsigned long)failLargest);
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
    noteStepSet("sending");
    sendOutbox();
    noteStepSet("sd card");
    flushSd();
    noteStepSet("node table");
    refreshUiNodes();
    noteStepSet("saving msgs");
    mc_ui_background_work();
    noteStepSet("idle");
    return 50; // a packet in or a send out wakes it at once
}

// ------------------------------------------------------------------ data for the screens
// Everything below is small and bounded: nothing here grows with the packets that come in.
// The screen task (core 0) only ever reads these tables under uiLock; it never touches the
// firmware's own lists, which change under it.

static concurrency::Lock *uiLock = nullptr;

// Nodes: one short entry per node (~100 bytes; the firmware's full record is 300+), filled
// on the firmware thread from its node list. Memory for 16 more is added as nodes are heard.
namespace
{
constexpr size_t kChunk = 16;
constexpr size_t kChunks = (MAX_NUM_NODES + kChunk - 1) / kChunk;
enum : uint16_t {
    kHasUser = 1 << 0,
    kHasHops = 1 << 1,
    kViaMqtt = 1 << 2,
    kFavorite = 1 << 3,
    kIgnored = 1 << 4,
    kHasPos = 1 << 5,
    kHasAlt = 1 << 6,
    kHasMetrics = 1 << 7,
    kLicensed = 1 << 8,
    kHasUnmsg = 1 << 9,
    kUnmsg = 1 << 10,
    kHasBattery = 1 << 11,
    kHasVoltage = 1 << 12,
    kHasChUtil = 1 << 13,
    kHasAirUtil = 1 << 14,
    kHasUptime = 1 << 15,
};
struct UiNode {
    uint32_t num, lastHeard, posTime, uptime;
    float snr, voltage, chUtil, airUtil;
    int32_t lat, lon, alt;
    uint16_t hwModel, flags;
    uint8_t role, channel, hops, battery, sats, keyLen;
    char shortName[sizeof(meshtastic_User::short_name)];
    char longName[sizeof(meshtastic_User::long_name)];
};
UiNode *uiChunks[kChunks] = {};
size_t uiCapacity = 0;
size_t uiNodeCount = 0;
UiNode &uiNode(size_t i)
{
    return uiChunks[i / kChunk][i % kChunk];
}
volatile uint32_t uiNodesRev = 1;
volatile uint32_t heardSeq = 0; // bumped for every packet, so the table is looked at again

// When this device has no real time yet, the firmware stores last_heard = 0 for every node, and the screens then
// count nobody as online. So each packet's sender is stamped here with the uptime it was heard at, and a node
// whose last_heard is 0 shows "heard N s ago" on the screens' own clock (time(), which counts from boot then).
constexpr size_t kHeardAt = 64;
struct HeardAt {
    uint32_t num, ms;
};
HeardAt heardAt[kHeardAt] = {};

void noteHeardAt(uint32_t num)
{
    if (!num || (nodeDB && num == nodeDB->getNodeNum()))
        return;
    const uint32_t now = millis();
    size_t slot = 0;
    for (size_t i = 0; i < kHeardAt; ++i) {
        if (heardAt[i].num == num) {
            slot = i;
            break;
        }
        if (!heardAt[i].num || now - heardAt[i].ms > now - heardAt[slot].ms)
            slot = i; // a free slot, or else the oldest
    }
    heardAt[slot].num = num;
    heardAt[slot].ms = now;
}

uint32_t localLastHeard(uint32_t num)
{
    for (size_t i = 0; i < kHeardAt; ++i)
        if (heardAt[i].num == num) {
            const uint32_t now = static_cast<uint32_t>(time(nullptr));
            const uint32_t ago = (millis() - heardAt[i].ms) / 1000;
            return now > ago ? now - ago : 1;
        }
    return 0;
}

void toUiNode(const meshtastic_NodeInfo &n, UiNode &u)
{
    memset(&u, 0, sizeof(u));
    u.num = n.num;
    u.lastHeard = n.last_heard ? n.last_heard : localLastHeard(n.num);
    u.snr = n.snr;
    u.channel = n.channel;
    u.hops = n.hops_away;
    uint16_t f = 0;
    if (n.has_hops_away)
        f |= kHasHops;
    if (n.via_mqtt)
        f |= kViaMqtt;
    if (n.is_favorite)
        f |= kFavorite;
    if (n.is_ignored)
        f |= kIgnored;
    if (n.has_user) {
        f |= kHasUser;
        strncpy(u.shortName, n.user.short_name, sizeof(u.shortName) - 1);
        strncpy(u.longName, n.user.long_name, sizeof(u.longName) - 1);
        u.hwModel = n.user.hw_model;
        u.role = n.user.role;
        u.keyLen = n.user.public_key.size;
        if (n.user.is_licensed)
            f |= kLicensed;
        if (n.user.has_is_unmessagable)
            f |= kHasUnmsg;
        if (n.user.is_unmessagable)
            f |= kUnmsg;
    }
    if (n.has_position && n.position.has_latitude_i && n.position.has_longitude_i) {
        f |= kHasPos;
        u.lat = n.position.latitude_i;
        u.lon = n.position.longitude_i;
        u.posTime = n.position.time;
        u.sats = n.position.sats_in_view;
        if (n.position.has_altitude) {
            f |= kHasAlt;
            u.alt = n.position.altitude;
        }
    }
    if (n.has_device_metrics) {
        const meshtastic_DeviceMetrics &d = n.device_metrics;
        f |= kHasMetrics;
        if (d.has_battery_level) {
            f |= kHasBattery;
            u.battery = d.battery_level > 255 ? 255 : d.battery_level;
        }
        if (d.has_voltage) {
            f |= kHasVoltage;
            u.voltage = d.voltage;
        }
        if (d.has_channel_utilization) {
            f |= kHasChUtil;
            u.chUtil = d.channel_utilization;
        }
        if (d.has_air_util_tx) {
            f |= kHasAirUtil;
            u.airUtil = d.air_util_tx;
        }
        if (d.has_uptime_seconds) {
            f |= kHasUptime;
            u.uptime = d.uptime_seconds;
        }
    }
    u.flags = f;
}

void fromUiNode(const UiNode &u, meshtastic_NodeInfo &n)
{
    n = meshtastic_NodeInfo_init_zero;
    n.num = u.num;
    n.last_heard = u.lastHeard;
    n.snr = u.snr;
    n.channel = u.channel;
    n.has_hops_away = u.flags & kHasHops;
    n.hops_away = u.hops;
    n.via_mqtt = u.flags & kViaMqtt;
    n.is_favorite = u.flags & kFavorite;
    n.is_ignored = u.flags & kIgnored;
    if (u.flags & kHasUser) {
        n.has_user = true;
        snprintf(n.user.id, sizeof(n.user.id), "!%08lx", (unsigned long)u.num);
        memcpy(n.user.short_name, u.shortName, sizeof(u.shortName));
        memcpy(n.user.long_name, u.longName, sizeof(u.longName));
        n.user.hw_model = static_cast<meshtastic_HardwareModel>(u.hwModel);
        n.user.role = static_cast<meshtastic_Config_DeviceConfig_Role>(u.role);
        n.user.public_key.size = u.keyLen; // the screens only ask whether there is one
        n.user.is_licensed = u.flags & kLicensed;
        n.user.has_is_unmessagable = u.flags & kHasUnmsg;
        n.user.is_unmessagable = u.flags & kUnmsg;
    }
    if (u.flags & kHasPos) {
        n.has_position = true;
        n.position.has_latitude_i = n.position.has_longitude_i = true;
        n.position.latitude_i = u.lat;
        n.position.longitude_i = u.lon;
        n.position.time = u.posTime;
        n.position.sats_in_view = u.sats;
        n.position.has_altitude = u.flags & kHasAlt;
        n.position.altitude = u.alt;
    }
    if (u.flags & kHasMetrics) {
        meshtastic_DeviceMetrics &d = n.device_metrics;
        n.has_device_metrics = true;
        d.has_battery_level = u.flags & kHasBattery;
        d.battery_level = u.battery;
        d.has_voltage = u.flags & kHasVoltage;
        d.voltage = u.voltage;
        d.has_channel_utilization = u.flags & kHasChUtil;
        d.channel_utilization = u.chUtil;
        d.has_air_util_tx = u.flags & kHasAirUtil;
        d.air_util_tx = u.airUtil;
        d.has_uptime_seconds = u.flags & kHasUptime;
        d.uptime_seconds = u.uptime;
    }
}

// Firmware thread: bring the table up to date, at most once a second while packets come in.
void refreshUiNodes()
{
    static uint32_t lastSeq = 0, lastAt = 0;
    static size_t lastCount = 0;
    if (!nodeDB || !uiLock)
        return;
    const size_t count = nodeDB->getNumMeshNodes();
    const uint32_t now = millis();
    const bool changed = heardSeq != lastSeq || count != lastCount;
    if (lastAt && !(changed && now - lastAt >= 1000) && now - lastAt < 15000)
        return;
    lastSeq = heardSeq;
    lastCount = count;
    lastAt = now ? now : 1;

    bool differs = false;
    size_t k = 0;
    UiNode u;
    for (size_t i = 0; i < count && k < kChunks * kChunk; ++i) {
        const meshtastic_NodeInfoLite *lite = nodeDB->getMeshNodeByIndex(i);
        if (!lite || !lite->num)
            continue;
        if (k >= uiCapacity) {
            // Room for 16 more. If memory is that short, the list shows the nodes it has.
            UiNode *chunk = static_cast<UiNode *>(calloc(kChunk, sizeof(UiNode)));
            if (!chunk)
                break;
            concurrency::LockGuard g(uiLock);
            uiChunks[uiCapacity / kChunk] = chunk;
            uiCapacity += kChunk;
        }
        const meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfo(lite);
        toUiNode(info, u);
        concurrency::LockGuard g(uiLock);
        if (memcmp(&uiNode(k), &u, sizeof(u)) != 0) {
            memcpy(&uiNode(k), &u, sizeof(u)); // bytes, padding too: compared next time
            differs = true;
        }
        ++k;
    }
    concurrency::LockGuard g(uiLock);
    if (k != uiNodeCount)
        differs = true;
    uiNodeCount = k;
    if (differs)
        uiNodesRev = uiNodesRev + 1;
}

// Packets the screens act on: a few full copies, in place (no memory asked for).
constexpr int kInbox = 4; // the screen task empties it every few ms
meshtastic_MeshPacket inbox[kInbox];
int inHead = 0, inCount = 0;

// Packets the screens send, handed to the mesh service on the firmware thread.
constexpr int kOutbox = 2;
meshtastic_MeshPacket outbox[kOutbox];
int outCount = 0;

void sendOutbox()
{
    static meshtastic_MeshPacket p; // firmware thread only; 350 bytes off its stack
    while (uiLock && service) {
        {
            concurrency::LockGuard g(uiLock);
            if (!outCount)
                return;
            p = outbox[0];
            for (int i = 1; i < outCount; ++i)
                outbox[i - 1] = outbox[i];
            --outCount;
        }
        service->handleToRadio(p);
    }
}

// One short line per heard packet, for the radio log page.
struct Heard {
    uint32_t rxTime, from, to;
    float snr;
    int16_t rssi, port; // port -1: could not be read (another channel's key)
    uint8_t hopStart, hopLimit;
    uint8_t textLen;
    char text[40];
};
constexpr int kHeard = 16;
Heard heard[kHeard];
int heardHead = 0, heardCount = 0;

bool wantedByScreens(const meshtastic_MeshPacket &p)
{
    if (p.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    switch (p.decoded.portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
    case meshtastic_PortNum_RANGE_TEST_APP: // "seq N" from other senders, shown in the channel
    case meshtastic_PortNum_ROUTING_APP:
    case meshtastic_PortNum_TRACEROUTE_APP:
        return true;
    case meshtastic_PortNum_TELEMETRY_APP: // only our own radio's numbers (the radio page)
        return nodeDB && p.from == nodeDB->getNodeNum();
    default:
        return false;
    }
}
} // namespace

void LocalUiAPI::tap(const meshtastic_MeshPacket *p)
{
    LocalUiAPI *self = localUiApi;
    if (!self || !p || !uiLock)
        return;
    {
        concurrency::LockGuard g(uiLock);
        Heard &h = heard[(heardHead + heardCount) % kHeard];
        if (heardCount == kHeard)
            heardHead = (heardHead + 1) % kHeard;
        else
            ++heardCount;
        memset(&h, 0, sizeof(h));
        h.rxTime = p->rx_time;
        h.from = p->from;
        h.to = p->to;
        h.snr = p->rx_snr;
        h.rssi = p->rx_rssi;
        h.hopStart = p->hop_start;
        h.hopLimit = p->hop_limit;
        h.port = p->which_payload_variant == meshtastic_MeshPacket_decoded_tag ? p->decoded.portnum : -1;
        if (h.port == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            h.textLen = p->decoded.payload.size < sizeof(h.text) ? p->decoded.payload.size : sizeof(h.text);
            memcpy(h.text, p->decoded.payload.bytes, h.textLen);
        }
        noteHeardAt(p->from);
        heardSeq = heardSeq + 1;

        if (wantedByScreens(*p)) {
            int slot;
            if (inCount < kInbox) {
                slot = (inHead + inCount++) % kInbox;
            } else {
                // Full (the screens are busy): lose an ack or stats line before a message.
                int drop = 0;
                for (int i = 0; i < kInbox; ++i) {
                    const int s = (inHead + i) % kInbox;
                    if (inbox[s].decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
                        drop = i;
                        break;
                    }
                }
                for (int i = drop; i > 0; --i) // close the gap, keeping the order
                    inbox[(inHead + i) % kInbox] = inbox[(inHead + i - 1) % kInbox];
                inHead = (inHead + 1) % kInbox;
                slot = (inHead + kInbox - 1) % kInbox;
            }
            inbox[slot] = *p;
        }
    }
    self->setIntervalFromNow(0);
}

// ---- the screen task's side (declared in meshcard/src/ui/mesh.cpp)

// Queues one packet to send; waits a little when two are already waiting. The mesh service
// treats it like a phone's packet (it sets the sender and id, and admin to us is applied).
bool mc_send_packet(const meshtastic_MeshPacket *p)
{
    if (!uiLock || !localUiApi || !p)
        return false;
    for (int tries = 0; tries < 30; ++tries) {
        {
            concurrency::LockGuard g(uiLock);
            if (outCount < kOutbox) {
                outbox[outCount++] = *p;
                break;
            }
        }
        if (tries == 29)
            return false;
        vTaskDelay(pdMS_TO_TICKS(10)); // the screen task waits, never the firmware
    }
    localUiApi->setIntervalFromNow(0);
    return true;
}

uint32_t mc_my_node_num()
{
    return nodeDB ? nodeDB->getNodeNum() : 0;
}

uint32_t mc_nodes_rev()
{
    return uiNodesRev;
}

size_t mc_node_count()
{
    if (!uiLock)
        return 0;
    concurrency::LockGuard g(uiLock);
    return uiNodeCount;
}

bool mc_node_at(size_t i, meshtastic_NodeInfo *out)
{
    if (!uiLock)
        return false;
    UiNode u;
    {
        concurrency::LockGuard g(uiLock);
        if (i >= uiNodeCount)
            return false;
        u = uiNode(i);
    }
    fromUiNode(u, *out);
    return true;
}

bool mc_node_find(uint32_t num, meshtastic_NodeInfo *out)
{
    if (!uiLock || !num)
        return false;
    UiNode u;
    {
        concurrency::LockGuard g(uiLock);
        size_t i = 0;
        while (i < uiNodeCount && uiNode(i).num != num)
            ++i;
        if (i == uiNodeCount)
            return false;
        u = uiNode(i);
    }
    fromUiNode(u, *out);
    return true;
}

// Brings the screen's copies of the fixed-size settings up to date (no pointers inside, so
// reading them while the firmware saves a change can at worst show the old value until the
// next read). Returns which parts changed: 1 config, 2 modules, 4 channels, 8 own node info.
static bool syncBytes(void *dst, const void *src, size_t n)
{
    if (memcmp(dst, src, n) == 0)
        return false;
    memcpy(dst, src, n);
    return true;
}

int mc_read_settings(meshtastic_LocalConfig *cfg, meshtastic_LocalModuleConfig *mod, meshtastic_Channel *channels,
                     int count, meshtastic_MyNodeInfo *my)
{
    int changed = 0;
    if (cfg && syncBytes(cfg, &config, sizeof(*cfg)))
        changed |= 1;
    if (mod && syncBytes(mod, &moduleConfig, sizeof(*mod)))
        changed |= 2;
    for (int i = 0; channels && i < count; ++i) {
        meshtastic_Channel unused;
        const meshtastic_Channel *src = &unused;
        if (i < (int)(sizeof(channelFile.channels) / sizeof(channelFile.channels[0])) && i < channelFile.channels_count) {
            src = &channelFile.channels[i];
        } else {
            memset(&unused, 0, sizeof(unused));
            unused.index = i;
        }
        if (syncBytes(&channels[i], src, sizeof(channels[i])))
            changed |= 4;
    }
    if (my && syncBytes(my, &myNodeInfo, sizeof(*my)))
        changed |= 8;
    return changed;
}

void mc_read_metadata(meshtastic_DeviceMetadata *md)
{
    *md = getDeviceMetadata();
}

bool mc_take_packet(meshtastic_MeshPacket *out)
{
    if (!uiLock)
        return false;
    concurrency::LockGuard g(uiLock);
    if (!inCount)
        return false;
    *out = inbox[inHead];
    inHead = (inHead + 1) % kInbox;
    --inCount;
    return true;
}

// The radio log line comes back as a packet header (and the start of a text).
bool mc_take_heard(meshtastic_MeshPacket *out)
{
    if (!uiLock)
        return false;
    Heard h;
    {
        concurrency::LockGuard g(uiLock);
        if (!heardCount)
            return false;
        h = heard[heardHead];
        heardHead = (heardHead + 1) % kHeard;
        --heardCount;
    }
    memset(out, 0, sizeof(*out));
    out->rx_time = h.rxTime;
    out->from = h.from;
    out->to = h.to;
    out->rx_snr = h.snr;
    out->rx_rssi = h.rssi;
    out->hop_start = h.hopStart;
    out->hop_limit = h.hopLimit;
    if (h.port >= 0) {
        out->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        out->decoded.portnum = static_cast<meshtastic_PortNum>(h.port);
        memcpy(out->decoded.payload.bytes, h.text, h.textLen);
        out->decoded.payload.size = h.textLen;
    } else {
        out->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    }
    return true;
}

void meshcardSetup()
{
    if (localUiApi)
        return;
    writeRestartNote();
    set_arduino_panic_handler(keepCrashPlace, nullptr);
    heap_caps_register_failed_alloc_callback(onAllocFail);
    sdPending.reserve(4096); // one piece now, so adding a message never needs a new one
    sdLock = new concurrency::Lock();
    uiLock = new concurrency::Lock();
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
    // "Full" in the firmware is above 4.20 V, but this ADV reads 4.16 V at most while the
    // charger holds it full, so it never counted as plugged (screen went off, 2026-11-20).
    // A steady level, not a jump: on from 4.15 V, off again under 4.12 V.
    static bool full = false;
    const int mv = powerStatus->getBatteryVoltageMv();
    if (mv >= 4150)
        full = true;
    else if (mv < 4120)
        full = false;
    *charging = powerStatus->getIsCharging() || powerStatus->getHasUSB() || usbHost || full;
    return true;
}

#endif
