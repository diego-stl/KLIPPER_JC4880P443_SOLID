#pragma once
/* Single-thread host only: console/backend tasks are not running here. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
/* Portable TinyCC predates this C11 keyword; keep the compile-time check. */
#define PORTAL_HOST_CAT_(left,right) left##right
#define PORTAL_HOST_CAT(left,right) PORTAL_HOST_CAT_(left,right)
#define _Static_assert(condition,message) typedef char PORTAL_HOST_CAT(portal_host_static_assert_,__LINE__)[(condition)?1:-1]
