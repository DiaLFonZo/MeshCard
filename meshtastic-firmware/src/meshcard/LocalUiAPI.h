#pragma once
#if defined(MESHCARD_UI)

// MeshCard on the Cardputer ADV: the MeshCard screen app runs in this firmware, in its own
// task, and talks to the radio through the normal client API (framed ToRadio / FromRadio,
// the same bytes a phone sends over serial). LocalUiAPI is that client's end inside the
// firmware; the screen app's end is mc_link_read / mc_link_write.

#include "StreamAPI.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"

/// Byte loopback between the firmware (the StreamAPI side) and the screen task.
class LocalUiLink : public Stream
{
  public:
    static constexpr size_t kSize = 2048; // a few full frames (max ~520 bytes each); RAM is tight

    // Firmware side (Stream)
    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *buf, size_t len) override;
    void flush() override {}

    /// Free room on the firmware -> screen side.
    size_t toUiSpace();

    // Screen side
    size_t uiRead(uint8_t *buf, size_t len);
    size_t uiWrite(const uint8_t *buf, size_t len);

  private:
    struct Ring {
        uint8_t data[kSize];
        size_t head = 0, tail = 0, count = 0;
        size_t put(const uint8_t *buf, size_t len);
        size_t get(uint8_t *buf, size_t len);
    };
    Ring toUi, toFw;
    concurrency::Lock lock;
};

class LocalUiAPI : public StreamAPI, public concurrency::OSThread
{
  public:
    LocalUiAPI();

    LocalUiLink link;

  protected:
    int32_t runOnce() override;
    bool checkIsConnected() override { return true; }
    bool canWriteFrame(size_t frameLen) override;
    meshtastic_MeshPacket *takeLocalPacket() override;

  private:
    static void tap(const meshtastic_MeshPacket *p);

    static constexpr int kQueueLen = 8; // packets waiting for the screen (copies from packetPool)
    meshtastic_MeshPacket *queue[kQueueLen] = {};
    int qHead = 0, qCount = 0;
    concurrency::Lock qLock;
};

extern LocalUiAPI *localUiApi;

/// Creates the local client and starts the MeshCard screen task (call once from setup()).
void meshcardSetup();

#endif
