#if !defined(PLATFORM_BUILDS_MIMALLOC)
#	define PLATFORM_BUILDS_MIMALLOC 0
#endif

#if PLATFORM_BUILDS_MIMALLOC

#define MI_PADDING 0
#define MI_TSAN 0
#define MI_OSX_ZONE 0
#define TARGET_IOS_IPHONE 0
#define TARGET_IOS_SIMULATOR 0

#include "ThirdParty/mimalloc/src/static.c"

#endif