#include "audio_assets.h"
#include "assets/glados_wakeup.h"
#include "assets/glados_imprimiendo.h"
#include "assets/glados_pause.h"
#include "assets/glados_cancelar.h"
#include "assets/glados_error.h"
#include "assets/glados_completado.h"
#include "assets/glados_stanby.h"
static const audio_asset_t assets[AUDIO_CLIP_COUNT]={
    {glados_wakeup_wav,sizeof(glados_wakeup_wav)},
    {glados_imprimiendo_wav,sizeof(glados_imprimiendo_wav)},
    {glados_pause_wav,sizeof(glados_pause_wav)},
    {glados_cancelar_wav,sizeof(glados_cancelar_wav)},
    {glados_error_wav,sizeof(glados_error_wav)},
    {glados_completado_wav,sizeof(glados_completado_wav)},
    {glados_stanby_wav,sizeof(glados_stanby_wav)},
};
const audio_asset_t *audio_asset_get(audio_clip_t clip)
{ return (unsigned)clip<AUDIO_CLIP_COUNT?&assets[clip]:NULL; }
