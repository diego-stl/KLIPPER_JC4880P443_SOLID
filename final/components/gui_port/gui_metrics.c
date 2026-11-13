#include "gui_port.h"
#include "gui_metrics.h"
#include "gui_metrics_math.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint64_t total_us; uint32_t calls,max_us; } timing_t;
typedef struct { size_t total,free,minimum,largest; } memory_t;
typedef struct {
    timing_t timing[GUI_METRIC_COUNT];
    uint32_t frames,poll_gap_us;
    uint64_t rotated_pixels;
} work_t;
typedef struct {
    work_t work;
    memory_t internal,psram;
    uint64_t uptime_ms;
    uint32_t fps_milli,stack_free;
    unsigned cpu[2];
    bool ready,cpu_valid;
} snapshot_t;
static work_t s_work;
static snapshot_t s_snapshot;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_periodic,s_reset_requested;
static int64_t s_last_touch;

void gui_metrics_record(gui_metric_t metric,uint32_t elapsed_us)
{
    timing_t *t=&s_work.timing[metric];t->total_us+=elapsed_us;t->calls++;
    if(elapsed_us>t->max_us)t->max_us=elapsed_us;
}
void gui_metrics_frame(uint32_t pixels) {s_work.frames++;s_work.rotated_pixels+=pixels;}
void gui_metrics_touch(int64_t started,uint32_t elapsed)
{
    if(s_last_touch){uint64_t gap=started-s_last_touch;if(gap>s_work.poll_gap_us)s_work.poll_gap_us=gap>UINT32_MAX?UINT32_MAX:(uint32_t)gap;}
    s_last_touch=started;gui_metrics_record(GUI_METRIC_TOUCH,elapsed);
}
static memory_t memory(unsigned caps)
{
    multi_heap_info_t info;heap_caps_get_info(&info,caps);
    return (memory_t){info.total_free_bytes+info.total_allocated_bytes,info.total_free_bytes,
        info.minimum_free_bytes,info.largest_free_block};
}
static void print_snapshot(const snapshot_t *s)
{
    if(!s->ready){printf("PERF warming_up=1 sample_period_ms=1000\n");return;}
    printf("PERF uptime_ms=%"PRIu64" frames=%"PRIu32" fps=%"PRIu32".%03"PRIu32" cpu_valid=%u cpu0=%u.%u cpu1=%u.%u gui_stack_free=%"PRIu32" poll_gap_max_us=%"PRIu32"\n",
        s->uptime_ms,s->work.frames,s->fps_milli/1000,s->fps_milli%1000,s->cpu_valid,
        s->cpu[0]/10,s->cpu[0]%10,s->cpu[1]/10,s->cpu[1]%10,s->stack_free,s->work.poll_gap_us);
    printf("PERF_HEAP internal_total=%zu internal_free=%zu internal_min=%zu internal_largest=%zu psram_total=%zu psram_free=%zu psram_min=%zu psram_largest=%zu\n",
        s->internal.total,s->internal.free,s->internal.minimum,s->internal.largest,
        s->psram.total,s->psram.free,s->psram.minimum,s->psram.largest);
    static const char *const names[]={"ui","lvgl","render","rotate","present","touch"};
    for(unsigned i=0;i<GUI_METRIC_COUNT;++i){const timing_t *t=&s->work.timing[i];
        printf("PERF_TIME name=%s calls=%"PRIu32" avg_us=%"PRIu64" max_us=%"PRIu32"\n",
            names[i],t->calls,t->calls?t->total_us/t->calls:0,t->max_us);}
    uint64_t ratio=s->work.frames?s->work.rotated_pixels*1000/((uint64_t)s->work.frames*800*480):0;
    printf("PERF_DAMAGE rotated_pixels=%"PRIu64" average_frame_percent=%"PRIu64".%"PRIu64"\n",
        s->work.rotated_pixels,ratio/10,ratio%10);
}
bool gui_port_console_command(const char *command)
{
    if(!command||strncmp(command,"perf",4)||(command[4]&&command[4]!=' '))return false;
    if(!strcmp(command,"perf status")||!strcmp(command,"perf")){
        snapshot_t copy;portENTER_CRITICAL(&s_lock);copy=s_snapshot;portEXIT_CRITICAL(&s_lock);print_snapshot(&copy);
    }else if(!strcmp(command,"perf on")||!strcmp(command,"perf off")){
        bool on=!strcmp(command,"perf on");portENTER_CRITICAL(&s_lock);s_periodic=on;portEXIT_CRITICAL(&s_lock);
        printf("PERF periodic=%u interval_ms=10000\n",on);
    }else if(!strcmp(command,"perf reset")){
        portENTER_CRITICAL(&s_lock);s_reset_requested=true;portEXIT_CRITICAL(&s_lock);printf("PERF reset_requested=1\n");
    }else printf("perf status | perf on | perf off | perf reset\n");
    return true;
}
void gui_port_console_poll(void)
{
    // USB printf may wait for a reader. Keep all telemetry output on the
    // console worker, never on the task that renders and samples touch.
    static int64_t printed;
    int64_t now=esp_timer_get_time();snapshot_t copy;
    portENTER_CRITICAL(&s_lock);
    bool due=s_periodic&&now-printed>=10000000;
    if(due)copy=s_snapshot;
    portEXIT_CRITICAL(&s_lock);
    if(due){printed=now;print_snapshot(&copy);}
}
void gui_metrics_poll(int64_t now)
{
    static int64_t sampled;
    static uint32_t idle_before[2],frames_before;
    if(sampled&&now-sampled<1000000)return;
    bool reset;portENTER_CRITICAL(&s_lock);reset=s_reset_requested;s_reset_requested=false;portEXIT_CRITICAL(&s_lock);
    if(reset){memset(&s_work,0,sizeof(s_work));frames_before=0;sampled=0;s_last_touch=0;}
    snapshot_t next={.work=s_work,.uptime_ms=(uint64_t)now/1000,.ready=true,
        .stack_free=(uint32_t)uxTaskGetStackHighWaterMark(NULL)};
    uint64_t elapsed=sampled?now-sampled:0;
    next.fps_milli=elapsed?(uint32_t)((uint64_t)(s_work.frames-frames_before)*1000000000/elapsed):0;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER && !CONFIG_FREERTOS_SMP
    next.cpu_valid=elapsed>0&&elapsed<=UINT32_MAX;
    for(unsigned core=0;core<2;++core){
        uint32_t idle=(uint32_t)ulTaskGetIdleRunTimeCounterForCore(core);
        if(next.cpu_valid)next.cpu[core]=gui_cpu_busy_permille(idle_before[core],idle,elapsed);
        idle_before[core]=idle;
    }
#else
    (void)idle_before;
#endif
    next.internal=memory(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    next.psram=memory(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    sampled=now;frames_before=s_work.frames;
    portENTER_CRITICAL(&s_lock);s_snapshot=next;portEXIT_CRITICAL(&s_lock);
}
