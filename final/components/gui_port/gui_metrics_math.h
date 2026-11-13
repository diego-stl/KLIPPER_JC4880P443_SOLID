#pragma once
#include <stdint.h>
static inline unsigned gui_cpu_busy_permille(uint32_t before,uint32_t after,uint64_t elapsed_us)
{
    if(!elapsed_us)return 0;
    uint32_t idle_delta=after-before; // Unsigned subtraction tolerates counter wrap.
    uint64_t idle=(uint64_t)idle_delta*1000/elapsed_us;
    return idle>=1000?0:1000-(unsigned)idle;
}
