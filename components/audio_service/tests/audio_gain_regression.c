#include "../audio_wav.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
int main(void)
{
    int16_t known[]={12345,INT16_MIN,-32767,-10,-9,-3,-1,0,1,3,9,10,32767,-12345};
    audio_apply_voice_gain(known+1,12);
    const int16_t expected[]={12345,-9830,-9830,-3,-2,0,0,0,0,0,2,3,9830,-12345};
    for(unsigned i=0;i<14;++i)assert(known[i]==expected[i]);
    int previous=-9831;
    for(int value=INT16_MIN;value<=INT16_MAX;++value){
        int16_t output=(int16_t)value;audio_apply_voice_gain(&output,1);
        assert(output==(value*3)/10 && output>=-9830 && output<=9830);
        assert(output>=previous);previous=output;
        int residual=value*3-output*10;assert(residual>-10 && residual<10);
        if(value>0)assert(output>=0);
        if(value<0)assert(output<=0);
    }
    int16_t unchanged=100;audio_apply_voice_gain(&unchanged,0);assert(unchanged==100);
    audio_apply_voice_gain(NULL,128);
    puts("PASS: 30% voice gain, all 65536 int16 values, bounds, monotonicity, <1LSB rounding and buffer guards");
    return 0;
}
