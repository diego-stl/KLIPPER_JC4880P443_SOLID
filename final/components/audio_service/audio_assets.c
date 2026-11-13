#include "audio_assets.h"
#include "assets/glados_wakeup.h"

#define PORTAL_EFFECTS(X) \
    X(PRINTING, wpn_portalgun_activation_01) \
    X(PAUSE, laser_node_power_off) \
    X(CANCEL, hackbuzzer) \
    X(ERROR, turret_disabled_2) \
    X(COMPLETE, aperture_logo_bells_01_01) \
    X(STANDBY, wpn_portal_fizzler_shimmy_01) \
    X(TEMPERATURE, portal_enter_01) \
    X(TUNING, portal_fizzle2) \
    X(MOVEMENT, interior_arm_platform_close_01) \
    X(FILAMENT, retract) \
    X(NETWORK, button_synth_positive_01) \
    X(SYSTEM, button_synth_negative_02)

#define DECLARE_WAV(clip, name) \
    extern const uint8_t _binary_##name##_wav_start[]; \
    extern const uint32_t name##_wav_length;
PORTAL_EFFECTS(DECLARE_WAV)
#undef DECLARE_WAV

static const struct { const uint8_t *wav; const uint32_t *length; } effects[AUDIO_CLIP_COUNT] = {
#define WAV_ENTRY(clip, name) [AUDIO_CLIP_##clip] = {_binary_##name##_wav_start, &name##_wav_length},
    PORTAL_EFFECTS(WAV_ENTRY)
#undef WAV_ENTRY
};
#undef PORTAL_EFFECTS

audio_asset_t audio_asset_get(audio_clip_t clip)
{
    if (clip == AUDIO_CLIP_WAKEUP)
        return (audio_asset_t){glados_wakeup_wav, sizeof(glados_wakeup_wav)};
    if ((unsigned)clip >= AUDIO_CLIP_COUNT || !effects[clip].length)
        return (audio_asset_t){0};
    return (audio_asset_t){effects[clip].wav, *effects[clip].length};
}
