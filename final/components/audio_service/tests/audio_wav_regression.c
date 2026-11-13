#include "../audio_wav.h"
#include "../include/audio_clips.h"
#include "../assets/glados_wakeup.h"
#include <assert.h>
#include <stdio.h>
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void small_wave(uint8_t *b)
{
    memset(b,0,60);memcpy(b,"RIFF",4);put32(b+4,52);memcpy(b+8,"WAVEfmt ",8);put32(b+16,16);
    b[20]=1;b[22]=1;put32(b+24,22050);put32(b+28,44100);b[32]=2;b[34]=16;
    memcpy(b+36,"data",4);put32(b+40,4);b[46]=0;b[47]=0x80;
    memcpy(b+48,"JUNK",4);put32(b+52,3);b[56]=0xff;b[57]=0x7f;b[58]=0xff;
}
int main(void)
{
    const char *const filenames[AUDIO_CLIP_COUNT]={
        [AUDIO_CLIP_PRINTING]="wpn_portalgun_activation_01",
        [AUDIO_CLIP_PAUSE]="laser_node_power_off",
        [AUDIO_CLIP_CANCEL]="hackbuzzer",
        [AUDIO_CLIP_ERROR]="turret_disabled_2",
        [AUDIO_CLIP_COMPLETE]="aperture_logo_bells_01_01",
        [AUDIO_CLIP_STANDBY]="wpn_portal_fizzler_shimmy_01",
        [AUDIO_CLIP_TEMPERATURE]="portal_enter_01",
        [AUDIO_CLIP_TUNING]="portal_fizzle2",
        [AUDIO_CLIP_MOVEMENT]="interior_arm_platform_close_01",
        [AUDIO_CLIP_FILAMENT]="retract",
        [AUDIO_CLIP_NETWORK]="button_synth_positive_01",
        [AUDIO_CLIP_SYSTEM]="button_synth_negative_02",
    };
    const size_t expected_pcm[AUDIO_CLIP_COUNT]={
        96768,86760,86428,44880,67328,220500,42386,15044,57674,58396,16382,64906,56984,
    };
    static uint8_t file_bytes[230000];
    size_t total_wav=0,total_pcm=0,total_expanded=0;
    for(unsigned i=0;i<AUDIO_CLIP_COUNT;++i){
        const uint8_t *wav=glados_wakeup_wav;
        size_t length=sizeof(glados_wakeup_wav);
        if(i!=AUDIO_CLIP_WAKEUP){
            char path[160];
            assert(filenames[i]);
            snprintf(path,sizeof(path),"components/audio_service/assets/%s.wav",filenames[i]);
            FILE *file=fopen(path,"rb");
            assert(file);
            length=44+expected_pcm[i];
            assert(length<=sizeof(file_bytes));
            assert(fread(file_bytes,1,length,file)==length && fgetc(file)==EOF);
            fclose(file);
            wav=file_bytes;
        }
        audio_wav_t wave;
        assert(audio_wav_parse(wav,length,&wave));
        assert(wave.pcm==wav+44 && wave.bytes==expected_pcm[i] && wave.sample_rate==22050);
        if(i==AUDIO_CLIP_WAKEUP){
            // Original LIST/id3 tail is present but excluded from playback.
            assert(length-44-wave.bytes==106 && !memcmp(wave.pcm+wave.bytes,"LIST",4));
        }else assert(length==44+wave.bytes);
        total_wav+=length;total_pcm+=wave.bytes;
        for(size_t offset=0;offset<wave.bytes/2;){
            size_t count=wave.bytes/2-offset;if(count>128)count=128;
            int16_t output[514];output[0]=12345;output[513]=-12345;
            size_t written=audio_expand_mono_2x(wave.pcm+offset*2,count,output+1,512);
            assert(written==count*4 && output[0]==12345 && output[513]==-12345);
            for(size_t j=0;j<count;++j){uint16_t raw=audio_le16(wave.pcm+(offset+j)*2);
                int16_t expected=(int16_t)((int32_t)raw-((raw&0x8000)?65536:0));
                for(unsigned k=0;k<4;++k)assert(output[1+j*4+k]==expected);
            }
            total_expanded+=written;offset+=count;
        }
        assert(!audio_wav_parse(wav,length-1,&wave));
    }
    assert(total_expanded==total_pcm*2);
    uint8_t b[60],bad[60];audio_wav_t wave;small_wave(b);assert(audio_wav_parse(b,60,&wave)&&wave.bytes==4);
    // Every truncation, even one inside a skipped unknown chunk, is rejected.
    for(size_t n=0;n<60;++n)assert(!audio_wav_parse(b,n,&wave));
    const unsigned corrupt[]={0,8,20,22,24,28,32,34};
    for(unsigned i=0;i<sizeof(corrupt)/sizeof(*corrupt);++i){memcpy(bad,b,60);bad[corrupt[i]]^=1;assert(!audio_wav_parse(bad,60,&wave));}
    memcpy(bad,b,60);put32(bad+40,UINT32_MAX);assert(!audio_wav_parse(bad,60,&wave));
    memcpy(bad,b,60);put32(bad+40,3);assert(!audio_wav_parse(bad,60,&wave));
    memcpy(bad,b,60);put32(bad+40,0);assert(!audio_wav_parse(bad,60,&wave));
    memcpy(bad,b,60);memcpy(bad+48,"data",4);put32(bad+52,4);assert(!audio_wav_parse(bad,60,&wave));
    memcpy(bad,b,60);memcpy(bad+48,"fmt ",4);assert(!audio_wav_parse(bad,60,&wave));
    memcpy(bad,b,60);put32(bad+4,51);assert(!audio_wav_parse(bad,59,&wave)); // missing odd-chunk pad
    // Chunk order is not assumed: data may precede fmt.
    memcpy(bad,b,12);memcpy(bad+12,b+36,12);memcpy(bad+24,b+12,24);memcpy(bad+48,b+48,12);
    assert(audio_wav_parse(bad,60,&wave)&&wave.pcm==bad+20&&wave.bytes==4);
    int16_t out[8];memset(out,0x55,sizeof(out));assert(audio_expand_mono_2x(b+44,2,out,7)==0 && out[0]==0x5555);
    assert(audio_expand_mono_2x(b+44,2,out,8)==8 && out[0]==0 && out[4]==-32768);
    assert(!audio_wav_parse(NULL,60,&wave));assert(!audio_wav_parse(b,60,NULL));
    printf("PASS: all %u WAVs; %zu container bytes, %zu PCM bytes, %zu expanded samples; robust RIFF bounds/padding/format and signed conversion\n",
           (unsigned)AUDIO_CLIP_COUNT,total_wav,total_pcm,total_expanded);
    return 0;
}
