#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct { const uint8_t *pcm; size_t bytes; uint32_t sample_rate; } audio_wav_t;
// Board adaptation only: original assets and exact expansion remain unchanged.
enum { AUDIO_VOICE_GAIN_PERCENT=30 };
static inline void audio_apply_voice_gain(int16_t *samples,size_t count)
{
    if(!samples)return;
    for(size_t i=0;i<count;++i)samples[i]=(int16_t)(((int32_t)samples[i]*3)/10);
}
static inline uint16_t audio_le16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static inline uint32_t audio_le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
// Complete RIFF/WAVE only. Unknown padded chunks are skipped, never played.
static inline bool audio_wav_parse(const uint8_t *wav,size_t length,audio_wav_t *out)
{
    if(!wav || !out || length<12 || memcmp(wav,"RIFF",4) || memcmp(wav+8,"WAVE",4) ||
       (uint64_t)audio_le32(wav+4)+8!=length)return false;
    bool format=false,data=false;audio_wav_t parsed={0};size_t offset=12;
    while(offset<length){
        if(length-offset<8)return false;
        const uint8_t *chunk=wav+offset;uint32_t bytes=audio_le32(chunk+4);offset+=8;
        if((size_t)bytes>length-offset)return false;
        if(!memcmp(chunk,"fmt ",4)){
            if(format || bytes<16)return false;
            const uint8_t *f=wav+offset;
            if(audio_le16(f)!=1 || audio_le16(f+2)!=1 || audio_le32(f+4)!=22050 ||
               audio_le32(f+8)!=44100 || audio_le16(f+12)!=2 || audio_le16(f+14)!=16)return false;
            format=true;parsed.sample_rate=22050;
        }else if(!memcmp(chunk,"data",4)){
            if(data || !bytes || (bytes&1))return false;
            data=true;parsed.pcm=wav+offset;parsed.bytes=bytes;
        }
        offset+=bytes;
        if(bytes&1){if(offset==length)return false;++offset;}
    }
    if(!format || !data)return false;
    *out=parsed;return true;
}
// Byte-wise LE decoding avoids unaligned reads from embedded const resources.
static inline size_t audio_expand_mono_2x(const uint8_t *pcm,size_t samples,int16_t *out,size_t capacity)
{
    if(!pcm || !out || samples>capacity/4)return 0;
    for(size_t i=0;i<samples;++i){uint16_t raw=audio_le16(pcm+i*2);
        int16_t value=(int16_t)((int32_t)raw-((raw&0x8000)?65536:0));
        out[i*4]=out[i*4+1]=out[i*4+2]=out[i*4+3]=value;
    }
    return samples*4;
}
