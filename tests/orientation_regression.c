#include "display_orientation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { N=800*480 };
int main(void)
{
    assert(orientation_current()==90);
    assert(!orientation_select(180)&&orientation_current()==90);
    assert(orientation_select(270)&&orientation_current()==270);
    uint16_t u=123,v=456;
    assert(!orientation_map_touch(90,480,0,&u,&v)&&u==123&&v==456);
    assert(!orientation_map_touch(90,0,800,&u,&v));
    assert(!orientation_map_touch(0,0,0,&u,&v));
    uint16_t *src=malloc((N+2)*sizeof(uint16_t)),*dst=malloc((N+2)*sizeof(uint16_t));
    assert(src&&dst);src[0]=dst[0]=0xa55a;src[N+1]=dst[N+1]=0x5aa5;
    assert(!orientation_rotate_rgb565(180,src+1,dst+1));
    assert(!orientation_rotate_rgb565(90,src+1,src+1));
    const unsigned rotations[]={90,270};
    for(unsigned r=0;r<2;++r){
        unsigned rotation=rotations[r];
        unsigned char *visited=calloc(N,1);assert(visited);
        for(unsigned y=0;y<800;++y)for(unsigned x=0;x<480;++x){
            assert(orientation_map_touch(rotation,x,y,&u,&v));assert(u<800&&v<480);
            assert(!visited[v*800+u]++);
            unsigned xx=rotation==90?479-v:v, yy=rotation==90?u:799-u;
            assert(xx==x&&yy==y);
        }
        for(unsigned i=0;i<N;++i)assert(visited[i]==1);
        free(visited);
        for(unsigned pattern=0;pattern<4;++pattern){
            for(unsigned y=0;y<480;++y)for(unsigned x=0;x<800;++x)
                src[1+y*800+x]=pattern==0?x:pattern==1?y:pattern==2?((x/7+y/11)&1?0xffff:0):((x*131)^y*47);
            memset(dst+1,0xcc,N*sizeof(uint16_t));
            assert(orientation_rotate_rgb565(rotation,src+1,dst+1));
            for(unsigned y=0;y<800;++y)for(unsigned x=0;x<480;++x){
                unsigned su=rotation==90?y:799-y,sv=rotation==90?479-x:x;
                uint16_t expected=pattern==0?su:pattern==1?sv:pattern==2?((su/7+sv/11)&1?0xffff:0):((su*131)^sv*47);
                assert(dst[1+y*480+x]==expected);
            }
            assert(src[0]==0xa55a&&src[N+1]==0x5aa5&&dst[0]==0xa55a&&dst[N+1]==0x5aa5);
        }
    }
    assert(orientation_map_touch(90,479,0,&u,&v)&&u==0&&v==0);
    assert(orientation_map_touch(270,0,799,&u,&v)&&u==0&&v==0);
    orientation_gate_t gate={0};
    orientation_gate_begin(&gate);orientation_gate_frame(&gate,true);
    orientation_gate_sample(&gate,true,true,true);assert(gate.blocked);
    orientation_gate_sample(&gate,true,false,false);assert(gate.blocked);
    orientation_gate_sample(&gate,false,true,false);assert(gate.blocked);
    orientation_gate_sample(&gate,true,true,false);assert(!gate.blocked);
    orientation_gate_begin(&gate);orientation_gate_sample(&gate,true,true,false);
    orientation_gate_frame(&gate,false);assert(gate.blocked);
    orientation_gate_frame(&gate,true);assert(gate.blocked);
    orientation_gate_sample(&gate,true,true,false);assert(!gate.blocked);
    orientation_gate_begin(&gate);
    orientation_gate_sample(&gate,true,true,false);
    orientation_gate_sample(&gate,true,true,true);
    orientation_gate_frame(&gate,true);assert(gate.blocked);
    orientation_gate_sample(&gate,true,false,false);assert(gate.blocked);
    orientation_gate_sample(&gate,true,true,false);assert(!gate.blocked);
    orientation_gate_begin(&gate);orientation_gate_frame(&gate,true);orientation_gate_begin(&gate);
    orientation_gate_sample(&gate,true,true,false);assert(gate.blocked);
    free(src);free(dst);
    puts("PASS: real C transforms, 768000 raw mappings, 8 RGB565 frames, guards, rotation gate");
    return 0;
}
