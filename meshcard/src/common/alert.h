// SPDX-License-Identifier: GPL-3.0-only
// MeshCard message alarm: settings shared by the Settings page and the
// background alarm service (meshcard-alertd), and the alarm sounds.
#pragma once

#include <string>

namespace mz {

struct AlertConf {
    bool on     = true;
    bool all    = false;  // false: direct messages only
    int volume  = 80;     // percent, speaker volume while ringing
    int sound   = 0;      // index into alert_sound_names()
    bool repeat = true;   // ring every few seconds until a key is pressed
};

constexpr int kAlertSoundCount = 4;
const char* alert_sound_name(int index);

// ~/.config/meshcard/alert.conf
std::string alert_conf_path();
AlertConf alert_conf_load();
bool alert_conf_save(const AlertConf& c);

// Rings once in the background (does not wait): sets the speaker volume,
// plays the sound, puts the volume back.
void alert_ring(const AlertConf& c);

}  // namespace mz
