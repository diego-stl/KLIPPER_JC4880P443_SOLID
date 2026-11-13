#include "display_orientation.h"
#include "display_settings_mailbox.h"
#include "gui_metrics_math.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PIXELS=800*480 };
static void damage_frames(void)
{
    uint16_t *image=calloc(PIXELS,2),*lcd[2]={calloc(PIXELS,2),calloc(PIXELS,2)},*reference=calloc(PIXELS,2);
    assert(image&&lcd[0]&&lcd[1]&&reference);
    const orientation_area_t regions[]={{0,0,0,0},{799,479,799,479},{39,80,430,125},
        {470,140,770,180},{22,50,360,456},{311,17,327,33},{0,479,799,479}};
    for(unsigned initial=90;initial<=270;initial+=180){
        memset(image,0,PIXELS*2);memset(lcd[0],0,PIXELS*2);memset(lcd[1],0,PIXELS*2);memset(reference,0,PIXELS*2);
        orientation_damage_t damage={0};unsigned back=0;
        for(unsigned frame=0;frame<40;++frame){
            unsigned rotation=frame<20?initial:(initial==90?270:90);
            orientation_area_t area=(frame==0||frame==20)?(orientation_area_t){0,0,799,479}:regions[frame%7];
            for(int y=area.y1;y<=area.y2;++y)for(int x=area.x1;x<=area.x2;++x)image[y*800+x]=(uint16_t)((x*131+y*47)^frame*331);
            assert(orientation_damage_add(&damage,&area));
            if(frame%3==0){orientation_area_t corner={780,2,799,21};
                for(int y=2;y<=21;++y)for(int x=780;x<800;++x)image[y*800+x]=frame;
                assert(orientation_damage_add(&damage,&corner));}
            orientation_area_t copy;assert(orientation_damage_next(&damage,&copy));
            assert(orientation_rotate_rgb565_area(rotation,image,lcd[back],&copy));
            assert(!memcmp(lcd[back^1],reference,PIXELS*2)); // Never writes the active scanout.
            assert(orientation_rotate_rgb565(rotation,image,reference));
            assert(!memcmp(lcd[back],reference,PIXELS*2)); // Includes erased and disjoint old regions.
            orientation_damage_commit(&damage);assert(!orientation_damage_next(&damage,&copy));back^=1;
        }
    }
    const orientation_area_t invalid[]={{-1,0,1,1},{0,0,800,1},{0,0,1,480},{4,3,2,1}};
    orientation_damage_t damage={0};
    for(unsigned i=0;i<4;++i){assert(!orientation_damage_add(&damage,&invalid[i]));assert(!orientation_rotate_rgb565_area(90,image,lcd[0],&invalid[i]));}
    free(image);free(lcd[0]);free(lcd[1]);free(reference);
}
static void display_mailbox(void)
{
    display_settings_mailbox_t box={.brightness=80,.sleep_minutes=10};display_settings_request_t request;
    display_settings_submit(&box,80,10);assert(!display_settings_begin(&box,&request));
    display_settings_submit(&box,40,10);display_settings_submit(&box,55,5);
    assert(box.status.revision==2&&box.status.pending&&box.status.writes==0);
    assert(display_settings_begin(&box,&request)&&request.brightness==55&&request.sleep_minutes==5);
    assert(!display_settings_begin(&box,&request));
    display_settings_submit(&box,70,1);display_settings_complete(&box,&request,ESP_OK,25000);
    assert(box.status.pending&&box.status.saved_revision==2&&box.status.revision==3);
    assert(display_settings_begin(&box,&request)&&request.brightness==70);
    display_settings_complete(&box,&request,ESP_FAIL,31000);
    assert(!box.status.pending&&box.status.result==ESP_FAIL&&box.status.saved_revision==2);
    assert(!display_settings_begin(&box,&request)); // Failure does not spin/rewrite NVS.
    display_settings_submit(&box,70,1);assert(display_settings_begin(&box,&request));
    display_settings_complete(&box,&request,ESP_OK,12000);
    assert(!box.status.pending&&box.status.saved_revision==4&&box.status.writes==3&&box.status.max_save_us==31000);
    display_settings_submit(&box,70,1);assert(!box.status.pending);
}
int main(void)
{
    damage_frames();display_mailbox();
    assert(gui_cpu_busy_permille(10,1010,1000)==0);
    assert(gui_cpu_busy_permille(10,10,1000)==1000);
    assert(gui_cpu_busy_permille(0,250,1000)==750);
    assert(gui_cpu_busy_permille(0xfffffff0U,0x54U,1000)==900);
    assert(gui_cpu_busy_permille(0,1200,1000)==0);
    assert(gui_cpu_busy_permille(0,0,0)==0);
    puts("PASS: 80 alternating dirty frames, rotation changes, active-buffer integrity, async saves, CPU counters");return 0;
}
