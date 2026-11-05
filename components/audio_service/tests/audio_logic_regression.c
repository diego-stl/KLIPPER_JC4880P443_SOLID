#include "../audio_logic.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint8_t volume=77;
    assert(audio_parse_command(NULL,&volume)==AUDIO_NOT_OURS);
    assert(audio_parse_command("audiobook",&volume)==AUDIO_NOT_OURS);
    assert(audio_parse_command("audio",&volume)==AUDIO_HELP);
    assert(audio_parse_command("audio status",&volume)==AUDIO_STATUS);
    assert(audio_parse_command("audio on",&volume)==AUDIO_ON);
    assert(audio_parse_command("audio off",&volume)==AUDIO_OFF);
    assert(audio_parse_command("audio stop",&volume)==AUDIO_STOP);
    assert(audio_parse_command("audio test",&volume)==AUDIO_TEST);
    assert(audio_parse_command("audio sounds",&volume)==AUDIO_SOUNDS);
    for(unsigned i=0;i<AUDIO_CLIP_COUNT;++i){
        audio_clip_t parsed=AUDIO_CLIP_COUNT;assert(audio_clip_parse(audio_clip_name_value((audio_clip_t)i),&parsed) && parsed==(audio_clip_t)i);
        char command[64];snprintf(command,sizeof(command),"audio play %s",audio_clip_name_value((audio_clip_t)i));
        assert(audio_parse_command(command,&volume)==AUDIO_PLAY);
    }
    assert(!audio_clip_parse("invalid",(audio_clip_t*)&(audio_clip_t){AUDIO_CLIP_COUNT}));
    assert(audio_parse_command("audio play wakeup trailing",&volume)==AUDIO_INVALID);
    assert(audio_parse_command("audio play ",&volume)==AUDIO_INVALID);
    assert(audio_parse_command("audio volume 0",&volume)==AUDIO_VOLUME && volume==0);
    assert(audio_parse_command("audio volume 50",&volume)==AUDIO_VOLUME && volume==50);
    assert(audio_parse_command("audio volume 100",&volume)==AUDIO_VOLUME && volume==100);
    const char *bad[]={"audio volume -1","audio volume +50","audio volume 101",
        "audio volume 9999999999999999","audio volume 50x","audio volume 50 test",
        "audio volume ","audio test now","audio volume 5.5"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){volume=77;assert(audio_parse_command(bad[i],&volume)==AUDIO_INVALID && volume==77);}
    // The actual worker's eligibility predicate rejects all old work after mute,
    // including a rapid off/on and requests already dequeued before queue reset.
    for(uint32_t gen=0;gen<1000;++gen){
        assert(audio_request_allowed(true,true,50,gen,gen));
        assert(!audio_request_allowed(true,false,50,gen+1,gen));
        assert(!audio_request_allowed(true,true,50,gen+1,gen));
        assert(!audio_request_allowed(true,true,0,gen,gen));
        assert(!audio_request_allowed(false,true,50,gen,gen));
    }
    assert(!audio_request_allowed(true,true,50,0,UINT32_MAX));
    audio_settings_t settings={1,0,0,0};bool enabled=true;
    assert(audio_decode_settings(&settings,sizeof(settings),&enabled,&volume)&&!enabled&&volume==0);
    settings=(audio_settings_t){1,1,50,0};assert(audio_decode_settings(&settings,sizeof(settings),&enabled,&volume)&&enabled&&volume==50);
    settings.volume=101;enabled=false;volume=77;
    assert(!audio_decode_settings(&settings,sizeof(settings),&enabled,&volume)&&!enabled&&volume==77);
    settings=(audio_settings_t){2,1,50,0};assert(!audio_decode_settings(&settings,sizeof(settings),&enabled,&volume));
    settings=(audio_settings_t){1,2,50,0};assert(!audio_decode_settings(&settings,sizeof(settings),&enabled,&volume));
    settings=(audio_settings_t){1,1,50,0};assert(!audio_decode_settings(&settings,sizeof(settings)-1,&enabled,&volume));
    assert(!audio_decode_settings(NULL,sizeof(settings),&enabled,&volume));
    puts("PASS: actual audio parser, generation/mute eligibility and versioned settings validation");
    return 0;
}
