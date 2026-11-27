#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

#if DEBUG_LEVEL > 1
int trace = 1;
#endif

#if defined(USE_EXTRA) && (USE_EXTRA > 0)
int extra = 1;
#endif
