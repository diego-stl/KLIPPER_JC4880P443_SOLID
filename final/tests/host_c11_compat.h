#pragma once
// The portable TinyCC bundled with the renderer predates C11 static assertions.
// Keep the condition enforced at compile time through a sized typedef.
#ifdef __TINYC__
#define _Static_assert(condition,message) typedef char host_static_assert[(condition)?1:-1]
#endif
