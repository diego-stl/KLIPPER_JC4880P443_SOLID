#include "audio_service.h"
#include "sdkconfig.h"
#include "audio_logic.h"
#include "audio_assets.h"
#include "audio_wav.h"
#include "board_touch.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE
#error "Audio must use the existing new I2C master bus; disable CODEC_I2C_BACKWARD_COMPATIBLE"
#endif

enum { PA_PIN=11, RATE=44100, BLOCK_FRAMES=128, DMA_COUNT=6, QUEUE_SIZE=8 };
static const char *TAG="audio_service";
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static audio_service_status_t s_status={.enabled=true,.volume=50,.last_clip=AUDIO_CLIP_COUNT};
static bool s_started,s_gpio_ready;
static QueueHandle_t s_queue;
static uint32_t s_settings_revision,s_saved_revision;
static esp_err_t s_hardware_error;
static i2s_chan_handle_t s_tx;
static esp_codec_dev_handle_t s_codec;
static const audio_codec_ctrl_if_t *s_ctrl;
static const audio_codec_data_if_t *s_data;
static const audio_codec_gpio_if_t *s_gpio;
static const audio_codec_if_t *s_codec_if;
typedef struct {
    uint32_t generation, printer_generation;
    audio_clip_t clip;
    bool printer;
} audio_request_t;
static uint32_t s_printer_generation;
static bool s_playing_printer;
static audio_wav_t s_waves[AUDIO_CLIP_COUNT];

// PA transitions and generation changes share this short critical section.
// The codec never owns GPIO11; no I2C/I2S/NVS operation runs with s_lock held.
static void pa_off_locked(void)
{
    if(s_gpio_ready)gpio_set_level(PA_PIN,0);
}
static void cancel_locked(void)
{
    ++s_status.generation;
    pa_off_locked();
    if(s_queue)xQueueReset(s_queue);
    s_status.queued=0;
}
static void remember_error(esp_err_t error)
{
    portENTER_CRITICAL(&s_lock);s_status.last_error=error;portEXIT_CRITICAL(&s_lock);
}
void audio_service_get_status(audio_service_status_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&s_lock);*out=s_status;
    out->queued=s_queue?(uint8_t)uxQueueMessagesWaiting(s_queue):0;
    out->settings_pending=s_settings_revision!=s_saved_revision;
    out->settings_revision=s_settings_revision;out->saved_revision=s_saved_revision;
    portEXIT_CRITICAL(&s_lock);
}
static bool request_allowed_locked(const audio_request_t *request)
{
    return audio_request_allowed(s_status.ready,s_status.enabled,s_status.volume,s_status.generation,request->generation)
        && (!request->printer || request->printer_generation==s_printer_generation);
}
static bool allowed(const audio_request_t *request)
{
    portENTER_CRITICAL(&s_lock);
    bool ok=request_allowed_locked(request);
    portEXIT_CRITICAL(&s_lock);return ok;
}
static esp_err_t save_settings(const audio_settings_t *settings)
{
    nvs_handle_t nvs;esp_err_t error=nvs_open("jc_audio",NVS_READWRITE,&nvs);
    if(error!=ESP_OK)return error;
    error=nvs_set_blob(nvs,"settings_v1",settings,sizeof(*settings));
    if(error==ESP_OK)error=nvs_commit(nvs);
    nvs_close(nvs);return error;
}
static void persist_pending(void)
{
    static int64_t retry_after;
    if(esp_timer_get_time()<retry_after)return;
    portENTER_CRITICAL(&s_lock);
    uint32_t revision=s_settings_revision;
    bool dirty=revision!=s_saved_revision;
    audio_settings_t settings={.version=1,.enabled=s_status.enabled,.volume=s_status.volume};
    portEXIT_CRITICAL(&s_lock);
    if(!dirty)return;
    esp_err_t error=save_settings(&settings);
    portENTER_CRITICAL(&s_lock);
    if(error==ESP_OK)s_saved_revision=revision;
    // Preserve the reason hardware is unavailable even after a successful save.
    s_status.last_error=s_hardware_error!=ESP_OK?s_hardware_error:error;
    portEXIT_CRITICAL(&s_lock);
    if(error==ESP_OK)ESP_LOGI(TAG,"AUDIO_SETTINGS_SAVED rev=%"PRIu32" enabled=%u volume=%u",revision,settings.enabled,settings.volume);
    else {ESP_LOGE(TAG,"AUDIO_SETTINGS_ERROR %s; cambio RAM conservado",esp_err_to_name(error));retry_after=esp_timer_get_time()+1000000;}
}
esp_err_t audio_service_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    if(!s_started){portEXIT_CRITICAL(&s_lock);return ESP_ERR_INVALID_STATE;}
    cancel_locked();uint32_t generation=s_status.generation;portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG,"AUDIO_STOP gen=%"PRIu32" PA=0 queue=0",generation);return ESP_OK;
}
void audio_service_stop_printer(void)
{
    portENTER_CRITICAL(&s_lock);
    ++s_printer_generation;
    if(s_playing_printer)pa_off_locked();
    if(s_queue){
        unsigned pending=uxQueueMessagesWaiting(s_queue);
        for(unsigned i=0;i<pending;++i){
            audio_request_t request;
            if(xQueueReceive(s_queue,&request,0)==pdTRUE && !request.printer)
                xQueueSend(s_queue,&request,0);
        }
    }
    portEXIT_CRITICAL(&s_lock);
}
esp_err_t audio_service_set_enabled(bool enabled)
{
    // Off takes effect before any potentially slow Flash operation.
    portENTER_CRITICAL(&s_lock);
    if(!s_started){portEXIT_CRITICAL(&s_lock);return ESP_ERR_INVALID_STATE;}
    if(!enabled)cancel_locked();
    s_status.enabled=enabled;
    ++s_settings_revision;
    uint32_t generation=s_status.generation;
    portEXIT_CRITICAL(&s_lock);
    if(!enabled)ESP_LOGI(TAG,"AUDIO_MUTED gen=%"PRIu32" PA=0 queue=0",generation);
    return ESP_OK;
}
esp_err_t audio_service_set_volume(uint8_t volume)
{
    if(volume>100)return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    if(!s_started){portEXIT_CRITICAL(&s_lock);return ESP_ERR_INVALID_STATE;}
    if(volume==0)cancel_locked();
    s_status.volume=volume;++s_settings_revision;portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}
static esp_err_t enqueue_audio(audio_clip_t clip,bool printer)
{
    portENTER_CRITICAL(&s_lock);
    audio_request_t request={s_status.generation,s_printer_generation,clip,printer};
    if(!s_queue || !request_allowed_locked(&request)){
        ++s_status.requests_dropped;portEXIT_CRITICAL(&s_lock);return ESP_ERR_INVALID_STATE;
    }
    bool sent=xQueueSend(s_queue,&request,0)==pdTRUE;
    if(!sent)++s_status.requests_dropped;
    portEXIT_CRITICAL(&s_lock);return sent?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t audio_service_test_tone(void){return enqueue_audio(AUDIO_CLIP_COUNT,false);}
esp_err_t audio_service_play(audio_clip_t clip)
{
    if((unsigned)clip>=AUDIO_CLIP_COUNT)return ESP_ERR_INVALID_ARG;
    return enqueue_audio(clip,false);
}
esp_err_t audio_service_play_printer(audio_clip_t clip)
{
    if((unsigned)clip>=AUDIO_CLIP_COUNT)return ESP_ERR_INVALID_ARG;
    return enqueue_audio(clip,true);
}
const char *audio_service_clip_name(audio_clip_t clip){return audio_clip_name_value(clip);}
static esp_err_t validate_assets(void)
{
    size_t wav_bytes=0,pcm_bytes=0;
    for(unsigned i=0;i<AUDIO_CLIP_COUNT;++i){
        audio_asset_t asset=audio_asset_get((audio_clip_t)i);
        if(!audio_wav_parse(asset.wav,asset.length,&s_waves[i])){
            ESP_LOGE(TAG,"AUDIO_ASSET_INVALID clip=%s",audio_service_clip_name((audio_clip_t)i));
            return ESP_ERR_INVALID_RESPONSE;
        }
        wav_bytes+=asset.length;pcm_bytes+=s_waves[i].bytes;
    }
    ESP_LOGI(TAG,"AUDIO_ASSETS_VALID count=%u wav_bytes=%zu pcm_bytes=%zu",AUDIO_CLIP_COUNT,wav_bytes,pcm_bytes);
    return ESP_OK;
}
static esp_err_t codec_result(int result)
{
    return result==ESP_CODEC_DEV_OK?ESP_OK:ESP_FAIL;
}
static esp_err_t initialize_hardware(void)
{
    if(!board_i2c_bus())return ESP_ERR_INVALID_STATE;
    esp_err_t probe=i2c_master_probe(board_i2c_bus(),0x18,100);
    if(probe!=ESP_OK)return probe;
    ESP_LOGI(TAG,"AUDIO_PROBE addr=0x18 ACK; PA=0");
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    channel.auto_clear=true;channel.dma_desc_num=DMA_COUNT;channel.dma_frame_num=BLOCK_FRAMES;
    esp_err_t error=i2s_new_channel(&channel,&s_tx,NULL);
    if(error!=ESP_OK)return error;
    i2s_std_config_t standard={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=13,.bclk=12,.ws=10,.dout=9,.din=I2S_GPIO_UNUSED},
    };
    standard.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;
    error=i2s_channel_init_std_mode(s_tx,&standard);if(error!=ESP_OK)return error;
    error=i2s_channel_enable(s_tx);if(error!=ESP_OK)return error;
    audio_codec_i2c_cfg_t control={.port=I2C_NUM_0,.addr=ES8311_CODEC_DEFAULT_ADDR,.bus_handle=board_i2c_bus()};
    // esp_codec_dev 1.3.4 uses 100kHz by default for this I2C control interface.
    s_ctrl=audio_codec_new_i2c_ctrl(&control);if(!s_ctrl)return ESP_ERR_NO_MEM;
    uint8_t id1=0,id2=0;
    int read1=s_ctrl->read_reg(s_ctrl,0xfd,1,&id1,1);
    int read2=s_ctrl->read_reg(s_ctrl,0xfe,1,&id2,1);
    ESP_LOGI(TAG,"AUDIO_CODEC_IDS addr=0x18 FD=0x%02x FE=0x%02x read_ok=%d PA=0",
             id1,id2,read1==ESP_CODEC_DEV_OK && read2==ESP_CODEC_DEV_OK);
    if(read1!=ESP_CODEC_DEV_OK || read2!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    if(id1!=0x83 || id2!=0x11)return ESP_ERR_INVALID_RESPONSE;
    audio_codec_i2s_cfg_t data={.port=I2S_NUM_0,.tx_handle=s_tx,.rx_handle=NULL};
    s_data=audio_codec_new_i2s_data(&data);if(!s_data)return ESP_ERR_NO_MEM;
    s_gpio=audio_codec_new_gpio();if(!s_gpio)return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t codec={.ctrl_if=s_ctrl,.gpio_if=s_gpio,
        .codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC,.master_mode=false,.use_mclk=true,
        .pa_pin=-1,.pa_reverted=false,.mclk_div=256};
    s_codec_if=es8311_codec_new(&codec);if(!s_codec_if)return ESP_ERR_NO_MEM;
    esp_codec_dev_cfg_t device={.dev_type=ESP_CODEC_DEV_TYPE_OUT,.codec_if=s_codec_if,.data_if=s_data};
    s_codec=esp_codec_dev_new(&device);if(!s_codec)return ESP_ERR_NO_MEM;
    esp_codec_dev_sample_info_t samples={.bits_per_sample=16,.channel=2,.channel_mask=3,.sample_rate=RATE,.mclk_multiple=256};
    error=codec_result(esp_codec_dev_open(s_codec,&samples));if(error!=ESP_OK)return error;
    audio_service_status_t status;audio_service_get_status(&status);
    return codec_result(esp_codec_dev_set_out_vol(s_codec,status.volume));
}
static void cleanup_hardware(void)
{
    if(s_codec){esp_codec_dev_close(s_codec);esp_codec_dev_delete(s_codec);s_codec=NULL;}
    if(s_codec_if){audio_codec_delete_codec_if(s_codec_if);s_codec_if=NULL;}
    if(s_data){audio_codec_delete_data_if(s_data);s_data=NULL;}
    if(s_ctrl){audio_codec_delete_ctrl_if(s_ctrl);s_ctrl=NULL;}
    if(s_gpio){audio_codec_delete_gpio_if(s_gpio);s_gpio=NULL;}
    if(s_tx){i2s_channel_disable(s_tx);i2s_del_channel(s_tx);s_tx=NULL;}
}
static esp_err_t clear_dma(void)
{
    esp_err_t error=i2s_channel_disable(s_tx);if(error!=ESP_OK)return error;
    int16_t zero[BLOCK_FRAMES*2]={0};
    for(unsigned i=0;i<DMA_COUNT;++i){size_t written=0;
        error=i2s_channel_preload_data(s_tx,zero,sizeof(zero),&written);
        if(error!=ESP_OK || written!=sizeof(zero))return error==ESP_OK?ESP_FAIL:error;
    }
    return i2s_channel_enable(s_tx);
}
static esp_err_t begin_playback(const audio_request_t *request,uint8_t *volume)
{
    if(!allowed(request))return ESP_ERR_INVALID_STATE;
    esp_err_t error=clear_dma();if(error!=ESP_OK)return error;
    audio_service_status_t status;audio_service_get_status(&status);
    error=codec_result(esp_codec_dev_set_out_vol(s_codec,status.volume));if(error!=ESP_OK)return error;
    portENTER_CRITICAL(&s_lock);
    bool valid=request_allowed_locked(request);
    if(valid){gpio_set_level(PA_PIN,1);s_status.playing=true;
        s_playing_printer=request->printer;
        if(request->clip==AUDIO_CLIP_COUNT)++s_status.tones_started;
        else {++s_status.clips_started;s_status.last_clip=request->clip;}
    }
    portEXIT_CRITICAL(&s_lock);
    if(!valid)return ESP_ERR_INVALID_STATE;
    *volume=status.volume;return ESP_OK;
}
static esp_err_t drain_tail(const audio_request_t *request)
{
    int64_t until=esp_timer_get_time()+20000;
    while(esp_timer_get_time()<until){if(!allowed(request))return ESP_ERR_INVALID_STATE;vTaskDelay(1);}
    return ESP_OK;
}
static esp_err_t play_tone(const audio_request_t *request)
{
    uint8_t applied_volume;
    esp_err_t error=begin_playback(request,&applied_volume);if(error!=ESP_OK)return error;
    ESP_LOGI(TAG,"AUDIO_TONE_START gen=%"PRIu32" volume=%u hz=1000 duration_ms=500",
             request->generation,applied_volume);
    const uint32_t frames=RATE/2;int16_t buffer[BLOCK_FRAMES*2];
    audio_service_status_t status;
    for(uint32_t offset=0;offset<frames;){
        if(!allowed(request))return ESP_ERR_INVALID_STATE;
        audio_service_get_status(&status);
        if(status.volume!=applied_volume){
            error=codec_result(esp_codec_dev_set_out_vol(s_codec,status.volume));
            if(error!=ESP_OK)return error;
            applied_volume=status.volume;
        }
        unsigned count=frames-offset;if(count>BLOCK_FRAMES)count=BLOCK_FRAMES;
        for(unsigned i=0;i<count;++i){
            // Preserve the original diagnostic's 30% PCM amplitude; codec 50%.
            int16_t value=(int16_t)(9830.0f*sinf(6.28318530718f*1000.0f*(offset+i)/RATE));
            buffer[i*2]=value;buffer[i*2+1]=value;
        }
        size_t written=0;error=i2s_channel_write(s_tx,buffer,count*4,&written,20);
        if(error!=ESP_OK || written!=count*4)return error==ESP_OK?ESP_FAIL:error;
        offset+=count;
    }
    // Let the bounded DMA tail drain while retaining cancellation checks.
    return drain_tail(request);
}
static esp_err_t play_clip(const audio_request_t *request)
{
    audio_clip_t clip=request->clip;
    if((unsigned)clip>=AUDIO_CLIP_COUNT)return ESP_ERR_INVALID_ARG;
    const audio_wav_t *wave=&s_waves[clip];uint8_t applied_volume;
    esp_err_t error=begin_playback(request,&applied_volume);if(error!=ESP_OK)return error;
    ESP_LOGI(TAG,"AUDIO_CLIP_START name=%s gen=%"PRIu32" volume=%u gain_pct=%u pcm_bytes=%zu duration_ms=%"PRIu32,
             audio_service_clip_name(clip),request->generation,applied_volume,AUDIO_VOICE_GAIN_PERCENT,wave->bytes,(uint32_t)((uint64_t)wave->bytes*1000/44100));
    int16_t buffer[BLOCK_FRAMES*4];size_t offset=0,total=wave->bytes/2;
    while(offset<total){
        if(!allowed(request))return ESP_ERR_INVALID_STATE;
        audio_service_status_t status;audio_service_get_status(&status);
        if(status.volume!=applied_volume){
            error=codec_result(esp_codec_dev_set_out_vol(s_codec,status.volume));
            if(error!=ESP_OK)return error;
            applied_volume=status.volume;
        }
        size_t count=total-offset;if(count>BLOCK_FRAMES)count=BLOCK_FRAMES;
        size_t samples=audio_expand_mono_2x(wave->pcm+offset*2,count,buffer,sizeof(buffer)/sizeof(buffer[0]));
        if(samples!=count*4)return ESP_ERR_INVALID_SIZE;
        audio_apply_voice_gain(buffer,samples);
        size_t written=0;error=i2s_channel_write(s_tx,buffer,samples*sizeof(int16_t),&written,20);
        if(error!=ESP_OK || written!=samples*sizeof(int16_t))return error==ESP_OK?ESP_FAIL:error;
        offset+=count;
    }
    return drain_tail(request);
}
static void audio_worker(void *argument)
{
    (void)argument;
    esp_err_t error=validate_assets();
    if(error==ESP_OK)error=initialize_hardware();
    portENTER_CRITICAL(&s_lock);
    s_hardware_error=error;s_status.ready=error==ESP_OK;s_status.last_error=error;pa_off_locked();
    portEXIT_CRITICAL(&s_lock);
    if(error!=ESP_OK){cleanup_hardware();ESP_LOGE(TAG,"Audio no disponible: %s; panel continua",esp_err_to_name(error));}
    else ESP_LOGI(TAG,"AUDIO_READY addr=0x18 rate=44100 bits=16 channels=2 MCLK=256 PA=11 active_high=1 boot_sound=0 voice_gain_pct=30");
    for(;;){audio_request_t request;
        persist_pending();
        // Wait outside the lock, then dequeue under the same lock used by cancellation.
        if(xQueuePeek(s_queue,&request,pdMS_TO_TICKS(20))!=pdTRUE)continue;
        portENTER_CRITICAL(&s_lock);
        bool received=xQueueReceive(s_queue,&request,0)==pdTRUE;
        portEXIT_CRITICAL(&s_lock);
        if(!received)continue;
        bool tone=request.clip==AUDIO_CLIP_COUNT;
        error=tone?play_tone(&request):play_clip(&request);
        bool cancelled=error==ESP_ERR_INVALID_STATE && !allowed(&request);
        portENTER_CRITICAL(&s_lock);pa_off_locked();s_status.playing=false;
        s_playing_printer=false;
        if(tone){if(error==ESP_OK)++s_status.tones_completed;else ++s_status.tones_aborted;}
        else {if(error==ESP_OK)++s_status.clips_completed;else ++s_status.clips_aborted;}
        if(error!=ESP_OK && !cancelled){s_hardware_error=error;s_status.last_error=error;s_status.ready=false;cancel_locked();}
        bool failed=!s_status.ready;portEXIT_CRITICAL(&s_lock);
        if(tone)ESP_LOGI(TAG,"%s gen=%"PRIu32" error=%s PA=0",
                        error==ESP_OK?"AUDIO_TONE_DONE":"AUDIO_TONE_ABORT",request.generation,esp_err_to_name(error));
        else ESP_LOGI(TAG,"%s name=%s gen=%"PRIu32" error=%s PA=0",
                      error==ESP_OK?"AUDIO_CLIP_DONE":"AUDIO_CLIP_ABORT",audio_service_clip_name(request.clip),request.generation,esp_err_to_name(error));
        if(failed)cleanup_hardware();
    }
}
esp_err_t audio_service_start(void)
{
    if(s_started)return ESP_ERR_INVALID_STATE;
    // Preload inactive level before output enable. PA remains manual throughout.
    esp_err_t error=gpio_set_level(PA_PIN,0);if(error!=ESP_OK){remember_error(error);return error;}
    gpio_config_t pa={.pin_bit_mask=1ULL<<PA_PIN,.mode=GPIO_MODE_OUTPUT};
    error=gpio_config(&pa);if(error!=ESP_OK){remember_error(error);return error;}
    portENTER_CRITICAL(&s_lock);s_gpio_ready=true;pa_off_locked();portEXIT_CRITICAL(&s_lock);
    error=nvs_flash_init();if(error!=ESP_OK){remember_error(error);return error;}
    nvs_handle_t nvs;
    if(nvs_open("jc_audio",NVS_READONLY,&nvs)==ESP_OK){
        audio_settings_t settings={0};size_t size=sizeof(settings);
        error=nvs_get_blob(nvs,"settings_v1",&settings,&size);nvs_close(nvs);
        bool enabled;uint8_t volume;
        if(error==ESP_OK && audio_decode_settings(&settings,size,&enabled,&volume)){
            portENTER_CRITICAL(&s_lock);s_status.enabled=enabled;s_status.volume=volume;portEXIT_CRITICAL(&s_lock);
        }
    }
    QueueHandle_t queue=xQueueCreate(QUEUE_SIZE,sizeof(audio_request_t));
    if(!queue){
        remember_error(ESP_ERR_NO_MEM);return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_lock);s_queue=queue;s_started=true;portEXIT_CRITICAL(&s_lock);
    if(xTaskCreate(audio_worker,"audio_worker",6144,NULL,1,NULL)!=pdPASS){
        portENTER_CRITICAL(&s_lock);s_started=false;s_queue=NULL;portEXIT_CRITICAL(&s_lock);
        vQueueDelete(queue);
        remember_error(ESP_ERR_NO_MEM);return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
bool audio_service_console_command(const char *command)
{
    uint8_t volume=0;audio_command_t action=audio_parse_command(command,&volume);
    if(action==AUDIO_NOT_OURS)return false;
    esp_err_t error=ESP_OK;
    switch(action){
    case AUDIO_ON:error=audio_service_set_enabled(true);break;
    case AUDIO_OFF:error=audio_service_set_enabled(false);break;
    case AUDIO_VOLUME:error=audio_service_set_volume(volume);break;
    case AUDIO_TEST:error=audio_service_test_tone();break;
    case AUDIO_STOP:error=audio_service_stop();break;
    case AUDIO_PLAY:{audio_clip_t clip;
        error=audio_clip_parse(command+11,&clip)?audio_service_play(clip):ESP_ERR_INVALID_ARG;
        printf("AUDIO_PLAY %s clip=%s\n",esp_err_to_name(error),command+11);break;}
    case AUDIO_SOUNDS:
        for(unsigned i=0;i<AUDIO_CLIP_COUNT;++i){
            audio_asset_t asset=audio_asset_get((audio_clip_t)i);audio_wav_t wave;
            bool valid=audio_wav_parse(asset.wav,asset.length,&wave);
            printf("AUDIO_SOUND clip=%s valid=%d wav_bytes=%zu pcm_bytes=%zu duration_ms=%"PRIu32"\n",
                   audio_service_clip_name((audio_clip_t)i),valid,asset.length,valid?wave.bytes:0,
                   valid?(uint32_t)((uint64_t)wave.bytes*1000/44100):0);
        }
        break;
    case AUDIO_STATUS:{audio_service_status_t status;audio_service_get_status(&status);
        printf("AUDIO_STATUS ready=%d enabled=%d playing=%d volume=%u queued=%u error=%s started=%"PRIu32" completed=%"PRIu32" aborted=%"PRIu32" dropped=%"PRIu32" gen=%"PRIu32" pending=%d settings_rev=%"PRIu32" saved_rev=%"PRIu32" clips_started=%"PRIu32" clips_completed=%"PRIu32" clips_aborted=%"PRIu32" last_clip=%s\n",
            status.ready,status.enabled,status.playing,status.volume,status.queued,esp_err_to_name(status.last_error),
            status.tones_started,status.tones_completed,status.tones_aborted,status.requests_dropped,status.generation,
            status.settings_pending,status.settings_revision,status.saved_revision,
            status.clips_started,status.clips_completed,status.clips_aborted,audio_service_clip_name(status.last_clip));break;}
    case AUDIO_HELP:case AUDIO_INVALID:printf("audio help | status | on | off | volume 0..100 | test | stop | sounds | play NAME\n");break;
    default:break;
    }
    if(action==AUDIO_TEST)printf("AUDIO_TEST %s (tono solicitado explicitamente; no sonido automatico)\n",esp_err_to_name(error));
    else if(action==AUDIO_ON||action==AUDIO_OFF||action==AUDIO_VOLUME||action==AUDIO_STOP)printf("AUDIO_COMMAND %s\n",esp_err_to_name(error));
    return true;
}
