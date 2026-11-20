#pragma once
#if defined(MESHCARD_UI)

// MeshCard on the Cardputer ADV: the MeshCard screen app runs in this firmware, in its own
// task (core 0), while the firmware runs on core 1. There is no PSRAM, so the screens are
// not a "phone" with a copy of everything; they use the firmware's own data:
//  - nodes: a small table the firmware thread fills from its node list (mc_node_*);
//  - settings: read straight from the firmware's own config (mc_read_settings);
//  - packets in: only the ones the screens act on (texts, acks, traceroutes, own stats) are
//    queued for them, plus one short line per heard packet for the radio log (mc_take_*);
//  - packets out: handed to the mesh service on the firmware thread (mc_send_packet).
// No client API, no config sync, no framing: that link and its sync burst were the
// biggest memory users.

#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

class LocalUiAPI : public concurrency::OSThread
{
  public:
    LocalUiAPI();

  protected:
    int32_t runOnce() override;

  private:
    static void tap(const meshtastic_MeshPacket *p);
};

extern LocalUiAPI *localUiApi;

/// Starts the MeshCard screen task and its link to the mesh service (call once from setup()).
void meshcardSetup();

#endif
